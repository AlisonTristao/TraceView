#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "audiostream.h"
#include "dashboard/dashboardwidget.h"
#include "telemetry/telemetrybinding.h"

class QTimer;

namespace traceview {

class AudioPlayback;

struct AudioSpectrumConfig {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    // A BTPDevice stream topic's fields (Topics::stream(): seq, rate,
    // samples, ids 1..3). The config editor fills them from the catalogue.
    quint16 seqFieldId = 1;
    quint16 rateFieldId = 2;
    quint16 samplesFieldId = 3;
    // The samples' full scale (the field's declared range): 1.0 after it.
    double fullScale = 2048.0;
    int volumePercent = 80;
};

AudioSpectrumConfig parseAudioSpectrumConfig(const QJsonObject& json);

// Gear-menu state (config["view"]).
struct AudioSpectrumView {
    bool play = true;
    int fftSize = 4096;
    bool logFrequency = true;
    bool peakHold = true;
};

// Plays a stream topic -- blocks of samples, each with its rate and a block
// counter -- on the default audio output and shows its spectrum: the FFT of
// the newest fftSize samples, repainted up to 30 times a second, on a log or
// linear frequency axis from 20 Hz to half the sample rate, with a slowly
// falling peak-hold line. A gap in the block counter is played as silence
// and counted in the info strip. Pause freezes the picture; the sound goes
// on (Play audio in the gear menu mutes it).
class AudioSpectrumWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit AudioSpectrumWidget(QWidget* parent = nullptr);
    ~AudioSpectrumWidget() override;

    void setConfig(const QJsonObject& config) override;
    const AudioSpectrumConfig& config() const {
        return m_config;
    }

    HeaderControls headerControls() const override {
        return HeaderControl::Pause | HeaderControl::Clear | HeaderControl::Settings;
    }
    bool isPaused() const override {
        return m_paused;
    }
    void setPaused(bool paused) override;
    void clearChartData() override;

    QVector<WidgetViewOption> viewOptions() const override;
    QJsonObject viewConfigWith(const QString& id, const QVariant& value) const override;
    void setViewConfig(const QJsonObject& view) override;

    // For tests and the info strip.
    double sampleRate() const {
        return m_rate;
    }
    quint64 lostBlocks() const {
        return m_lostBlocks;
    }
    quint64 receivedBlocks() const {
        return m_receivedBlocks;
    }
    // The newest spectrum (dB per bin), empty before the first block.
    const QVector<float>& spectrumDb() const {
        return m_spectrumDb;
    }
    // Computes the spectrum now instead of on the next repaint tick.
    void refreshSpectrum();

public slots:
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value);
    void onArraySample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       const QVector<float>& values);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool matches(const TelemetryFieldBinding& binding) const {
        return binding.sourceId == m_config.sourceId && binding.topicId == m_config.topicId &&
               m_config.topicId != 0;
    }
    void applyPlayback();
    void resetStream();
    float xForFrequency(double hz, const QRectF& plot) const;

    AudioSpectrumConfig m_config;
    AudioSpectrumView m_view;
    bool m_paused = false;

    // The stream.
    double m_rate = 0.0;
    bool m_haveSeq = false;
    quint32 m_lastSeq = 0;
    quint32 m_pendingGap = 0;
    quint64 m_lostBlocks = 0;
    quint64 m_receivedBlocks = 0;
    float m_blockDb = -140.0f;  // RMS of the newest block, dBFS

    // Analysis: the newest SpectrumAnalyzer::kMaxSize samples.
    QVector<float> m_history;
    int m_historyPos = 0;
    int m_historyFill = 0;
    bool m_dirty = false;
    SpectrumAnalyzer m_analyzer;
    QVector<float> m_window;
    QVector<float> m_spectrumDb;
    QVector<float> m_peakDb;
    QTimer* m_repaintTimer = nullptr;

    AudioPlayback* m_playback = nullptr;
    QElapsedTimer m_playbackRetry;
    QString m_playbackNote;
};

}  // namespace traceview
