#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

#include "audiostream.h"
#include "dashboard/widgets/chartwidgets.h"
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

// Gear-menu state every audio widget shares, kept in config["view"] next to
// the chart keys (chartstyle.h's ChartViewKey).
struct AudioStreamView {
    bool play = true;
    int fftSize = 4096;
    bool logFrequency = true;
};

// The audio widgets' common ground (AudioAnalyzerWidget): a stream
// topic -- blocks of samples, each with its rate and a block counter --
// played on the default audio output, the newest samples kept for the FFT,
// and a chart's gear menu (style, axis toggles) plus the audio options.
// A gap in the block counter is played as silence and counted in the info
// row. Pause freezes the picture; the sound goes on (Play audio in the gear
// menu mutes it).
//
// Subclasses paint, and turn samples into pictures through the hooks below.
class AudioStreamWidget : public StyledChartWidget {
    Q_OBJECT

public:
    explicit AudioStreamWidget(QWidget* parent = nullptr);
    ~AudioStreamWidget() override;

    void setConfig(const QJsonObject& config) override;
    const AudioSpectrumConfig& config() const {
        return m_config;
    }

    void setPaused(bool paused) override;
    void clearChartData() override;

    QVector<WidgetViewOption> viewOptions() const override;
    QJsonObject viewConfigWith(const QString& id, const QVariant& value) const override;
    void setViewConfig(const QJsonObject& view) override;

    // For tests and the info row.
    double sampleRate() const {
        return m_rate;
    }
    quint64 lostBlocks() const {
        return m_lostBlocks;
    }
    quint64 receivedBlocks() const {
        return m_receivedBlocks;
    }
    const AudioStreamView& audioView() const {
        return m_audio;
    }

    // Frequency label for an axis tick: "500", "2k", "12.5k".
    static QString frequencyLabel(double hz);

public slots:
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value);
    void onArraySample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       const QVector<float>& values);

protected:
    // --- Hooks ------------------------------------------------------------
    // `count` new samples are in the history (never called while paused).
    virtual void samplesAppended(int count) {
        Q_UNUSED(count);
    }
    // About 30 times a second while new samples keep arriving and the
    // widget is not paused.
    virtual void analysisTick() {}
    // What the history means changed (rate, FFT size, axis, a cleared
    // chart): drop whatever was derived from it.
    virtual void analysisReset() {}
    // Gear options of the subclass -- before everything else (leading) and
    // after the shared ones (extra) -- and their state in config["view"].
    virtual QVector<WidgetViewOption> leadingViewOptions() const {
        return {};
    }
    virtual QVector<WidgetViewOption> extraViewOptions() const {
        return {};
    }
    virtual void readExtraView(const QJsonObject& view) {
        Q_UNUSED(view);
    }
    virtual void writeExtraView(QJsonObject& view) const {
        Q_UNUSED(view);
    }

    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // --- Analysis helpers -------------------------------------------------
    // The history sample `back` samples before the newest one (0 = newest).
    float historySample(int back) const;
    // Copies the analyzer's size() samples ending `endOffset` samples before
    // the newest one into m_window; false while the history is shorter.
    bool fillWindow(int endOffset = 0);
    // Runs the FFT on m_window into `outDb` (resized to binCount()).
    void computeSpectrum(QVector<float>& outDb);
    // The highest frequency shown: half the sample rate.
    double topFrequency() const;
    // Frequency at `t` (0 = lowest shown, 1 = topFrequency()) on the
    // current axis, and back.
    double frequencyAt(double t) const;
    double frequencyFraction(double hz) const;
    // The level of `db` (one value per bin) over [f0, f1): the loudest bin
    // inside it, or the interpolated value where the span is narrower than
    // a bin (low frequencies on the log axis).
    float levelBetween(const QVector<float>& db, double f0, double f1) const;
    // Axis ticks for the current frequency axis: decades 1-2-5 (log) or
    // even steps (linear).
    QVector<double> frequencyTicks() const;

    // --- Paint helpers ----------------------------------------------------
    // Draws the "no topic"/"waiting" message and returns true when there is
    // nothing else to draw yet.
    bool paintPlaceholder(QPainter& painter);
    // The chart chrome the gear menu asks for (no legend rows: the info row
    // is the only one).
    CartesianChrome audioChrome(int gridDivisions) const;
    // Info rows below the first one (series statistics...), when shown.
    virtual int extraInfoRows() const {
        return 0;
    }
    // The info row (rate, loudest frequency, block level | losses, output),
    // when the gear menu shows it. `peakHz` < 0 leaves the loudest
    // frequency out.
    // `extra` fields follow the audio ones.
    void paintInfoRow(QPainter& painter, const ChartCartesianLayout& layout,
                      const ChartColors& colors, double peakHz,
                      const QVector<ChartInfoField>& extra = {});
    // The dashed hover line at the cursor and a balloon with `lines`.
    void paintHoverBalloon(QPainter& painter, const QRect& plotRect, const QStringList& lines,
                           bool verticalLine);
    bool hoverInside(const QRect& plotRect) const {
        return m_view.showHoverCrosshair && m_hasHoverPos && plotRect.contains(m_hoverPos);
    }

    AudioSpectrumConfig m_config;
    AudioStreamView m_audio;
    SpectrumAnalyzer m_analyzer;
    QVector<float> m_window;
    QPoint m_hoverPos;
    bool m_hasHoverPos = false;

private:
    bool matches(const TelemetryFieldBinding& binding) const {
        return binding.sourceId == m_config.sourceId && binding.topicId == m_config.topicId &&
               m_config.topicId != 0;
    }
    void applyPlayback();
    void resetStream();
    void resetAnalysis();
    QJsonObject currentViewJson() const;
    bool isChartViewKey(const QString& id) const;

    // The stream.
    double m_rate = 0.0;
    bool m_haveSeq = false;
    quint32 m_lastSeq = 0;
    quint32 m_pendingGap = 0;
    quint64 m_lostBlocks = 0;
    quint64 m_receivedBlocks = 0;
    float m_blockDb = -140.0f;  // RMS of the newest block, dBFS

    // The newest SpectrumAnalyzer::kMaxSize samples.
    QVector<float> m_history;
    int m_historyPos = 0;
    int m_historyFill = 0;
    bool m_dirty = false;
    QTimer* m_analysisTimer = nullptr;

    AudioPlayback* m_playback = nullptr;
    QElapsedTimer m_playbackRetry;
    QString m_playbackNote;
};

}  // namespace traceview
