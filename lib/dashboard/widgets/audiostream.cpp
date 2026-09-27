#include "audiostream.h"

#include <QMutexLocker>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace traceview {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Drift controller: step correction per unit of relative fill error, and how
// fast the measured fill follows the real one (per pull()).
constexpr double kDriftGain = 0.02;
constexpr double kFillSmoothing = 0.05;
// Blocks whose rate differs by more than this start a new timeline (the
// device changed its sample rate); smaller changes are the measured rate
// settling and only update the step.
constexpr double kRateChangeTolerance = 0.005;

[[maybe_unused]] bool isPowerOfTwo(int value) {
    return value > 0 && (value & (value - 1)) == 0;
}

}  // namespace

// ---- SpectrumAnalyzer -------------------------------------------------------

SpectrumAnalyzer::SpectrumAnalyzer(int size) {
    setSize(size);
}

void SpectrumAnalyzer::setSize(int size) {
    size = qBound(kMinSize, size, kMaxSize);
    int power = kMinSize;
    while (power * 2 <= size) {
        power *= 2;
    }
    if (power == m_size) {
        return;
    }
    m_size = power;
    m_window.resize(m_size);
    double sum = 0.0;
    for (int i = 0; i < m_size; ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (m_size - 1));
        m_window[i] = float(w);
        sum += w;
    }
    m_windowGain = float(sum);
    m_re.resize(m_size);
    m_im.resize(m_size);
}

void SpectrumAnalyzer::compute(const float* samples, float* outDb) {
    Q_ASSERT(isPowerOfTwo(m_size));
    const int n = m_size;
    for (int i = 0; i < n; ++i) {
        m_re[i] = samples[i] * m_window[i];
        m_im[i] = 0.0f;
    }

    // Iterative radix-2 FFT: bit-reversal permutation, then the butterflies.
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(m_re[i], m_re[j]);
            std::swap(m_im[i], m_im[j]);
        }
    }
    for (int length = 2; length <= n; length <<= 1) {
        const double angle = -2.0 * kPi / length;
        const float wRe = float(std::cos(angle));
        const float wIm = float(std::sin(angle));
        for (int start = 0; start < n; start += length) {
            float curRe = 1.0f;
            float curIm = 0.0f;
            for (int k = 0; k < length / 2; ++k) {
                const int a = start + k;
                const int b = a + length / 2;
                const float tRe = m_re[b] * curRe - m_im[b] * curIm;
                const float tIm = m_re[b] * curIm + m_im[b] * curRe;
                m_re[b] = m_re[a] - tRe;
                m_im[b] = m_im[a] - tIm;
                m_re[a] += tRe;
                m_im[a] += tIm;
                const float nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }

    // A sine of amplitude A peaks at A * sum(window) / 2: scale so A = 1 is 0 dB.
    const float scale = 2.0f / m_windowGain;
    for (int i = 0; i <= n / 2; ++i) {
        const float magnitude = std::sqrt(m_re[i] * m_re[i] + m_im[i] * m_im[i]) * scale;
        outDb[i] = magnitude > 0.0f ? std::max(kFloorDb, 20.0f * std::log10(magnitude)) : kFloorDb;
    }
}

// ---- AudioStreamBuffer -----------------------------------------------------

AudioStreamBuffer::AudioStreamBuffer() = default;

void AudioStreamBuffer::setOutputRate(int hz) {
    QMutexLocker lock(&m_mutex);
    m_outputRate = qMax(1, hz);
    resetLocked();
}

void AudioStreamBuffer::setLatency(int targetMs, int maxMs) {
    QMutexLocker lock(&m_mutex);
    m_targetMs = qMax(10, targetMs);
    m_maxMs = qMax(m_targetMs * 2, maxMs);
    resetLocked();
}

void AudioStreamBuffer::clear() {
    QMutexLocker lock(&m_mutex);
    resetLocked();
}

void AudioStreamBuffer::resetLocked() {
    m_head = 0;
    m_count = 0;
    m_position = 0.0;
    m_playing = false;
    m_smoothedFill = 0.0;
    m_drift = 1.0;
    if (m_inputRate > 0.0) {
        // Room for twice the maximum, so a burst never overwrites unread audio.
        const int capacity = int(m_inputRate * m_maxMs * 2 / 1000.0) + 4096;
        m_ring.resize(capacity);
    }
    configureFilterLocked();
}

void AudioStreamBuffer::configureFilterLocked() {
    m_filterOn = m_inputRate > m_outputRate * 1.02;
    if (!m_filterOn) {
        return;
    }
    // 4th-order Butterworth low-pass as two biquads (RBJ cookbook).
    const double qs[2] = {0.54119610, 1.30656296};
    const double w0 = 2.0 * kPi * (0.45 * m_outputRate) / m_inputRate;
    for (int i = 0; i < 2; ++i) {
        const double alpha = std::sin(w0) / (2.0 * qs[i]);
        const double cosW = std::cos(w0);
        const double a0 = 1.0 + alpha;
        Biquad& f = m_lowPass[i];
        f.b0 = float((1.0 - cosW) / 2.0 / a0);
        f.b1 = float((1.0 - cosW) / a0);
        f.b2 = f.b0;
        f.a1 = float(-2.0 * cosW / a0);
        f.a2 = float((1.0 - alpha) / a0);
        f.z1 = f.z2 = 0.0f;
    }
}

void AudioStreamBuffer::appendLocked(float value) {
    if (m_ring.isEmpty()) {
        return;
    }
    if (m_count == m_ring.size()) {
        dropLocked(1);
    }
    m_ring[(m_head + m_count) % m_ring.size()] = value;
    ++m_count;
}

void AudioStreamBuffer::dropLocked(int count) {
    count = qMin(count, m_count);
    if (count <= 0) {
        return;
    }
    m_head = (m_head + count) % m_ring.size();
    m_count -= count;
}

float AudioStreamBuffer::atLocked(int index) const {
    return m_ring[(m_head + index) % m_ring.size()];
}

void AudioStreamBuffer::push(const float* samples, int count, double inputRate) {
    if (count <= 0 || inputRate <= 0.0) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    if (m_inputRate <= 0.0 ||
        std::fabs(inputRate - m_inputRate) > m_inputRate * kRateChangeTolerance) {
        m_inputRate = inputRate;
        resetLocked();
    } else {
        m_inputRate = inputRate;
    }
    for (int i = 0; i < count; ++i) {
        float x = samples[i];
        if (m_filterOn) {
            x = m_lowPass[1].run(m_lowPass[0].run(x));
        }
        appendLocked(x);
    }
    const int maxSamples = int(m_inputRate * m_maxMs / 1000.0);
    if (m_count > maxSamples) {
        // Far behind (the sound card stalled, or the stream arrived in a
        // burst): catch up to the target instead of staying late forever.
        dropLocked(m_count - int(m_inputRate * m_targetMs / 1000.0));
        m_position = 0.0;
        ++m_overflows;
    }
}

void AudioStreamBuffer::pushSilence(int count) {
    if (count <= 0) {
        return;
    }
    QMutexLocker lock(&m_mutex);
    if (m_inputRate <= 0.0) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        appendLocked(m_filterOn ? m_lowPass[1].run(m_lowPass[0].run(0.0f)) : 0.0f);
    }
}

void AudioStreamBuffer::pull(float* out, int frames) {
    QMutexLocker lock(&m_mutex);
    int written = 0;
    const double target = m_inputRate * m_targetMs / 1000.0;
    if (m_inputRate > 0.0 && !m_playing && m_count >= target && m_count >= 2) {
        m_playing = true;
        m_position = 0.0;
        m_smoothedFill = m_count;
    }
    if (m_playing) {
        m_smoothedFill += kFillSmoothing * (m_count - m_smoothedFill);
        const double error = (m_smoothedFill - target) / target;
        m_drift = 1.0 + qBound(-kMaxDrift, kDriftGain * error, kMaxDrift);
        const double step = m_inputRate / m_outputRate * m_drift;
        for (; written < frames; ++written) {
            const int i = int(m_position);
            if (i + 1 >= m_count) {
                // Ran dry: mute and wait for the target again.
                m_playing = false;
                ++m_underruns;
                break;
            }
            const float a = atLocked(i);
            const float b = atLocked(i + 1);
            out[written] = a + (b - a) * float(m_position - i);
            m_position += step;
        }
        const int consumed = qMin(int(m_position), m_count);
        dropLocked(consumed);
        m_position -= consumed;
    }
    std::fill(out + written, out + frames, 0.0f);
}

AudioStreamBuffer::Stats AudioStreamBuffer::stats() const {
    QMutexLocker lock(&m_mutex);
    Stats stats;
    stats.inputRate = m_inputRate;
    stats.bufferedMs = m_inputRate > 0.0 ? m_count * 1000.0 / m_inputRate : 0.0;
    stats.drift = m_drift;
    stats.underruns = m_underruns;
    stats.overflows = m_overflows;
    return stats;
}

}  // namespace traceview
