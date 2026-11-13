#pragma once

#include <QAudioFormat>
#include <QObject>
#include <QString>

#include "audiostream.h"

class QAudioSink;

namespace traceview {

// Plays an AudioStreamBuffer on the system's default audio output. The sink
// pulls (Qt's audio thread, or its own timer) from a small QIODevice that
// asks the buffer for frames and fans the mono stream out to every output
// channel in the device's preferred format.
class AudioPlayback : public QObject {
    Q_OBJECT

public:
    explicit AudioPlayback(QObject* parent = nullptr);
    ~AudioPlayback() override;

    AudioStreamBuffer& buffer() {
        return m_buffer;
    }

    // Opens the default output. False when there is none (or it refused
    // every format): errorString() says why.
    bool start();
    void stop();
    bool isRunning() const {
        return m_sink != nullptr;
    }

    // 0.0..1.0, applied at once (and kept for the next start()).
    void setVolume(double volume);

    QString deviceName() const {
        return m_deviceName;
    }
    QString errorString() const {
        return m_error;
    }

private:
    class Source;

    AudioStreamBuffer m_buffer;
    QAudioSink* m_sink = nullptr;
    Source* m_source = nullptr;
    double m_volume = 1.0;
    QString m_deviceName;
    QString m_error;
};

}  // namespace traceview
