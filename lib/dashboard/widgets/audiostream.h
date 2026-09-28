#pragma once

#include <QMutex>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// The signal side of the Audio Spectrum widget, free of Qt Multimedia and of
// painting so it can be tested on its own (tests/test_audiostream.cpp).

// Magnitude spectrum of the newest `size` samples: Hann window, radix-2 FFT,
// in dB relative to a full-scale sine (a sine of amplitude 1.0 reads 0 dB at
// its bin). `size` is a power of two, 256..32768.
class SpectrumAnalyzer {
public:
    static constexpr int kMinSize = 256;
    static constexpr int kMaxSize = 32768;
    static constexpr float kFloorDb = -140.0f;

    explicit SpectrumAnalyzer(int size = 4096);

    // Rounds `size` down to a power of two within kMinSize..kMaxSize.
    void setSize(int size);
    int size() const {
        return m_size;
    }
    // Bins in the result: size / 2 + 1, bin i at i * sampleRate / size Hz.
    int binCount() const {
        return m_size / 2 + 1;
    }

    // `samples` holds exactly size() values (normalized, full scale 1.0);
    // `outDb` receives binCount() values.
    void compute(const float* samples, float* outDb);

private:
    int m_size = 0;
    QVector<float> m_window;
    float m_windowGain = 1.0f;  // sum of the window: a full-scale sine's bin height
    QVector<float> m_re;
    QVector<float> m_im;
};

// Jitter buffer + resampler between a stream of sample blocks (the device's
// rate, bursty arrival) and a sound card pulling at its own rate.
//
//   * push() appends a block (main thread); pushSilence() fills a gap the
//     block counter revealed, so the timeline stays true.
//   * pull() produces output frames (the audio thread): linear interpolation
//     at inputRate / outputRate, after a 4th-order low-pass at 0.45 x the
//     output rate when the input is faster (83 kHz in, 48 kHz out).
//   * Drift: the two clocks never agree exactly. The step is nudged (at most
//     +-0.5 %, far below audible pitch) to hold the buffered audio near
//     targetMs; an empty buffer mutes until it refills to the target, a
//     buffer past maxMs drops its oldest audio back to the target.
//
// Thread-safe: push/pull may run on different threads.
class AudioStreamBuffer {
public:
    static constexpr int kDefaultTargetMs = 80;
    static constexpr int kDefaultMaxMs = 400;
    static constexpr double kMaxDrift = 0.005;

    struct Stats {
        double inputRate = 0.0;
        double bufferedMs = 0.0;
        double drift = 1.0;  // current step multiplier
        quint64 underruns = 0;
        quint64 overflows = 0;
    };

    AudioStreamBuffer();

    // Sets the sound card's rate; resets the buffer.
    void setOutputRate(int hz);
    void setLatency(int targetMs, int maxMs);

    // A block at `inputRate` Hz. A different rate than the previous blocks
    // resets the buffer (and the filters) first.
    void push(const float* samples, int count, double inputRate);
    void pushSilence(int count);
    void clear();

    // Always fills `frames` values (silence while muted/refilling).
    void pull(float* out, int frames);

    Stats stats() const;

private:
    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1 = 0, z2 = 0;
        float run(float x) {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void resetLocked();
    void configureFilterLocked();
    int availableLocked() const {
        return m_count;
    }
    void appendLocked(float value);
    void dropLocked(int count);
    float atLocked(int index) const;

    mutable QMutex m_mutex;
    int m_outputRate = 48000;
    double m_inputRate = 0.0;
    int m_targetMs = kDefaultTargetMs;
    int m_maxMs = kDefaultMaxMs;

    QVector<float> m_ring;
    int m_head = 0;           // index of the oldest sample
    int m_count = 0;          // samples buffered
    double m_position = 0.0;  // fractional read position from m_head
    bool m_playing = false;
    double m_smoothedFill = 0.0;
    double m_drift = 1.0;
    bool m_filterOn = false;
    Biquad m_lowPass[2];
    quint64 m_underruns = 0;
    quint64 m_overflows = 0;
};

}  // namespace traceview
