#pragma once

#include <QImage>
#include <QJsonObject>
#include <QRgb>
#include <QVector>

#include "audiostreamwidget.h"

namespace traceview {

// Plays a stream topic and shows it one of two ways, picked in the gear
// menu ("View:"):
//
//   Spectrum    -- the FFT of the newest fftSize samples, repainted up to 30
//                  times a second, on a log or linear frequency axis from
//                  20 Hz to half the sample rate, in dBFS or linear
//                  amplitude, with a slowly falling peak-hold line.
//   Spectrogram -- time left to right (the newest column on the right
//                  edge), frequency bottom to top, each cell colored by its
//                  level. A column is the FFT of the fftSize samples ending
//                  at that moment, one every history/columns seconds, so the
//                  picture spans the chosen history whatever the rate. A
//                  color scale right of the plot reads the colors back as
//                  dBFS, and A/B range markers can bound the statistics.
//
// Both analyses always run, so switching views is instant. Each spectrogram
// column also keeps a summary of the samples it covers: the info row's
// signal statistics come from those, in either view. See AudioStreamWidget
// for the stream and the sound.
class AudioAnalyzerWidget : public AudioStreamWidget {
    Q_OBJECT

public:
    enum class Mode { Spectrum, Spectrogram };

    static constexpr int kColumns = 480;
    static constexpr int kRows = 256;

    explicit AudioAnalyzerWidget(QWidget* parent = nullptr);

    Mode mode() const {
        return m_mode;
    }

    // --- Spectrum ---------------------------------------------------------
    // The newest spectrum (dB per bin), empty before the first block.
    const QVector<float>& spectrumDb() const {
        return m_spectrumDb;
    }
    // Computes the spectrum now instead of on the next repaint tick.
    void refreshSpectrum();

    // --- Spectrogram ------------------------------------------------------
    // Columns drawn so far (at most kColumns), for tests.
    int filledColumns() const {
        return m_filledColumns;
    }
    // The level (dB) of `row` (0 = the top frequency) in the newest column.
    float newestLevel(int row) const;
    // The signal between `backLo` and `backHi` columns before the newest
    // one (inclusive), in full-scale units: min/max/mean/rms/peak are exact,
    // the median is the median of the columns' medians.
    SeriesStatistics signalStatistics(int backLo, int backHi) const;

protected:
    ChartViewFeatures viewFeatures() const override;
    void paintEvent(QPaintEvent* event) override;
    void refreshDataColors() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    int extraInfoRows() const override;

    void analysisTick() override {
        refreshSpectrum();
    }
    void samplesAppended(int count) override;
    void analysisReset() override;
    QVector<WidgetViewOption> leadingViewOptions() const override;
    QVector<WidgetViewOption> extraViewOptions() const override;
    void readExtraView(const QJsonObject& view) override;
    void writeExtraView(QJsonObject& view) const override;

private:
    enum class ColorMap { Inferno, Viridis, Theme };

    // What one column's stretch of samples was: enough to combine columns
    // into exact min/max/mean/rms, plus the column's own median.
    struct ColumnSummary {
        float min = 0.0f;
        float max = 0.0f;
        float median = 0.0f;
        double sum = 0.0;
        double sumSq = 0.0;
        int count = 0;
    };

    void paintSpectrum(QPainter& painter, const ThemePalette& palette);
    void paintSpectrogram(QPainter& painter, const ThemePalette& palette);
    QVector<WidgetViewOption> spectrumViewOptions() const;
    QVector<WidgetViewOption> spectrogramViewOptions() const;
    void readSpectrumView(const QJsonObject& view);
    void readSpectrogramView(const QJsonObject& view);
    void resetSpectrum();
    void resetSpectrogram();

    // The Y value of `db` on the spectrum's level scale.
    double levelValue(float db) const;

    int hopSamples() const;
    bool addColumn(int endOffset);
    // The A/B markers are the spectrogram's: shown and live in that view.
    bool markersShown() const;
    qreal markerX(int which) const;
    int markerAt(const QPoint& pos) const;
    // The info row past the audio readouts: what the analysis covers and
    // the A-B span; then the signal statistics on a row of their own.
    QVector<ChartInfoField> windowFields() const;
    void paintStatisticsRow(QPainter& painter, const ChartCartesianLayout& layout,
                            const ChartColors& colors) const;
    void rebuildColors();
    QRgb colorFor(float db) const;
    // Width the color scale takes right of the plot (bar, labels, title),
    // 0 while it is hidden.
    int colorScaleWidth(const QFontMetrics& fm, const QFontMetrics& tickFm,
                        const ChartStyle& style) const;
    void paintColorScale(QPainter& painter, const QRect& plotRect, const QFont& tickFont,
                         const ChartStyle& style, const ChartColors& colors);

    Mode m_mode = Mode::Spectrum;

    // Spectrum.
    bool m_peakHold = true;
    // Level scale: dBFS (-120..0) or linear amplitude, where a full-scale
    // sine reads 1.0. The linear axis tops out at a 1-2-5 step above the
    // loudest bin; it grows at once and shrinks only once the signal is two
    // steps below it, so the ticks don't flicker.
    bool m_linear = false;
    double m_linearTop = 1.0;
    QVector<float> m_spectrumDb;
    QVector<float> m_peakDb;

    // Spectrogram.
    int m_historySeconds = 10;
    int m_floorDb = -100;
    ColorMap m_colorMap = ColorMap::Inferno;
    bool m_showColorScale = true;
    // kColumns x kRows levels, column by column (row 0 = the top frequency),
    // and the same picture colored.
    QVector<float> m_levels;
    QImage m_image;
    int m_nextColumn = 0;
    int m_filledColumns = 0;
    int m_sinceColumn = 0;               // samples since the newest column
    QVector<float> m_columnDb;           // the newest column's spectrum, per bin
    QVector<ColumnSummary> m_summaries;  // one per column, same ring as m_levels
    ColumnSummary m_pending;             // the column being gathered
    QVector<float> m_pendingValues;      // its samples, for the median
    QVector<QRgb> m_lut;                 // 256 colors, floor to 0 dB

    // The A/B markers in columns before the newest one; NaN until shown.
    double m_markerAgo[2] = {qQNaN(), qQNaN()};
    int m_draggedMarker = -1;
    QRect m_plotRect;  // from the last spectrogram paint, for the markers
};

}  // namespace traceview
