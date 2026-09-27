#include "audioplayback.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QIODevice>
#include <QMediaDevices>
#include <QVector>
#include <algorithm>

namespace traceview {

namespace {

// What the sound card holds on top of AudioStreamBuffer's own target.
constexpr qint64 kSinkBufferUs = 40000;

}  // namespace

class AudioPlayback::Source : public QIODevice {
public:
    Source(AudioStreamBuffer& buffer, const QAudioFormat& format, QObject* parent)
        : QIODevice(parent), m_buffer(buffer), m_format(format) {}

    bool isSequential() const override {
        return true;
    }
    qint64 bytesAvailable() const override {
        // Always "ready": the buffer produces silence while it refills.
        return m_format.bytesForDuration(kSinkBufferUs) + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override {
        const int bytesPerFrame = m_format.bytesPerFrame();
        const int channels = m_format.channelCount();
        const int frames = int(maxSize / bytesPerFrame);
        if (frames <= 0) {
            return 0;
        }
        m_mono.resize(frames);
        m_buffer.pull(m_mono.data(), frames);
        const int bytesPerSample = m_format.bytesPerSample();
        char* out = data;
        for (int i = 0; i < frames; ++i) {
            const float v = std::clamp(m_mono[i], -1.0f, 1.0f);
            for (int c = 0; c < channels; ++c) {
                switch (m_format.sampleFormat()) {
                    case QAudioFormat::Float:
                        *reinterpret_cast<float*>(out) = v;
                        break;
                    case QAudioFormat::Int32:
                        *reinterpret_cast<qint32*>(out) = qint32(v * 2147483647.0f);
                        break;
                    case QAudioFormat::Int16:
                        *reinterpret_cast<qint16*>(out) = qint16(v * 32767.0f);
                        break;
                    case QAudioFormat::UInt8:
                        *reinterpret_cast<quint8*>(out) = quint8(128 + int(v * 127.0f));
                        break;
                    default:
                        std::fill(out, out + bytesPerSample, char(0));
                        break;
                }
                out += bytesPerSample;
            }
        }
        return qint64(frames) * bytesPerFrame;
    }
    qint64 writeData(const char*, qint64) override {
        return -1;
    }

private:
    AudioStreamBuffer& m_buffer;
    QAudioFormat m_format;
    QVector<float> m_mono;
};

AudioPlayback::AudioPlayback(QObject* parent) : QObject(parent) {}

AudioPlayback::~AudioPlayback() {
    stop();
}

bool AudioPlayback::start() {
    if (m_sink != nullptr) {
        return true;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        m_error = tr("No audio output device");
        return false;
    }
    QAudioFormat format = device.preferredFormat();
    QAudioFormat floatFormat = format;
    floatFormat.setSampleFormat(QAudioFormat::Float);
    if (device.isFormatSupported(floatFormat)) {
        format = floatFormat;
    }
    if (!format.isValid() || format.sampleRate() <= 0 || format.channelCount() <= 0) {
        m_error = tr("The audio output reports no usable format");
        return false;
    }

    m_buffer.setOutputRate(format.sampleRate());
    m_source = new Source(m_buffer, format, this);
    m_source->open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(device, format, this);
    m_sink->setBufferSize(format.bytesForDuration(kSinkBufferUs));
    m_sink->setVolume(m_volume);
    m_sink->start(m_source);
    if (m_sink->error() != QAudio::NoError) {
        m_error = tr("The audio output could not start");
        stop();
        return false;
    }
    m_deviceName = device.description();
    m_error.clear();
    return true;
}

void AudioPlayback::stop() {
    if (m_sink != nullptr) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    if (m_source != nullptr) {
        m_source->close();
        delete m_source;
        m_source = nullptr;
    }
    m_buffer.clear();
}

void AudioPlayback::setVolume(double volume) {
    m_volume = std::clamp(volume, 0.0, 1.0);
    if (m_sink != nullptr) {
        m_sink->setVolume(m_volume);
    }
}

}  // namespace traceview
