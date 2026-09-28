#include "audioanalyzerwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

constexpr float kPeakFallDbPerTick = 0.7f;  // ~20 dB/s
constexpr float kMinDb = -120.0f;
constexpr float kMaxDb = 0.0f;
// dB rows in the Divisions tick placement: one every 20 dB.
constexpr int kDbGridDivisions = 6;

constexpr char kPeakKey[] = "peak";
constexpr char kScaleKey[] = "scale";

// The smallest linear axis top: silence still gets a readable axis.
constexpr double kMinLinearTop = 1e-4;

// The 1-2-5 step at or above `value`.
double niceCeil(double value) {
    const double decade = std::pow(10.0, std::floor(std::log10(value)));
    const double m = value / decade;
    return decade * (m <= 1.0 ? 1.0 : m <= 2.0 ? 2.0 : m <= 5.0 ? 5.0 : 10.0);
}

// Enough decimals for a 0..top axis to print distinct labels.
int linearDecimals(double top) {
    return qBound(0, 1 - int(std::floor(std::log10(top))), 6);
}

constexpr char kModeKey[] = "mode";
constexpr char kHistoryKey[] = "history";
constexpr char kFloorKey[] = "floor";
constexpr char kColorsKey[] = "colors";
constexpr char kColorScaleKey[] = "colorScale";

// The color scale right of the plot: gap from the plot, bar width.
constexpr int kColorBarGap = 10;
constexpr int kColorBarWidth = 10;

const int kHistoryChoices[] = {5, 10, 30, 60};
const int kFloorChoices[] = {-60, -80, -100, -120};

// Unlabeled time grid in the Divisions tick placement: about one line every
// 60 px, 4 to 10 of them -- the line chart's rule (timeAxisScale()).
constexpr int kTimeGridSpacingPx = 60;
constexpr int kTimeGridMinLines = 4;
constexpr int kTimeGridMaxLines = 10;

struct ColorStop {
    double at;
    QRgb color;
};

// matplotlib's perceptually uniform maps, sampled every 10%.
const ColorStop kInferno[] = {
    {0.0, 0xff000004}, {0.1, 0xff160b39}, {0.2, 0xff420a68}, {0.3, 0xff6a176e},
    {0.4, 0xff932667}, {0.5, 0xffbc3754}, {0.6, 0xffdd513a}, {0.7, 0xfff37819},
    {0.8, 0xfffca50a}, {0.9, 0xfff6d746}, {1.0, 0xfffcffa4},
};
const ColorStop kViridis[] = {
    {0.0, 0xff440154}, {0.1, 0xff482475}, {0.2, 0xff414487}, {0.3, 0xff355f8d},
    {0.4, 0xff2a788e}, {0.5, 0xff21918c}, {0.6, 0xff22a884}, {0.7, 0xff44bf70},
    {0.8, 0xff7ad151}, {0.9, 0xffbddf26}, {1.0, 0xfffde725},
};

QRgb mix(QRgb a, QRgb b, double t) {
    auto channel = [t](int x, int y) { return int(std::lround(x + (y - x) * t)); };
    return qRgb(channel(qRed(a), qRed(b)), channel(qGreen(a), qGreen(b)),
                channel(qBlue(a), qBlue(b)));
}

template <size_t N>
QRgb sampleStops(const ColorStop (&stops)[N], double t) {
    for (size_t i = 1; i < N; ++i) {
        if (t <= stops[i].at) {
            const double span = stops[i].at - stops[i - 1].at;
            return mix(stops[i - 1].color, stops[i].color, (t - stops[i - 1].at) / span);
        }
    }
    return stops[N - 1].color;
}

}  // namespace

AudioAnalyzerWidget::AudioAnalyzerWidget(QWidget* parent)
    : AudioStreamWidget(parent),
      m_levels(kColumns * kRows, SpectrumAnalyzer::kFloorDb),
      m_image(kColumns, kRows, QImage::Format_ARGB32_Premultiplied),
      m_summaries(kColumns) {
    m_image.fill(Qt::transparent);
    rebuildColors();
}

ChartViewFeatures AudioAnalyzerWidget::viewFeatures() const {
    ChartViewFeatures features = ChartViewFeature::XAxisTitle | ChartViewFeature::XTickLabels |
                                 ChartViewFeature::YAxisTitle | ChartViewFeature::YTickLabels |
                                 ChartViewFeature::HoverCrosshair | ChartViewFeature::InfoRow |
                                 ChartViewFeature::InfoItems;
    if (m_mode == Mode::Spectrum) {
        features |= ChartViewFeature::YTickCount | ChartViewFeature::FillArea |
                    ChartViewFeature::LineWidth;
    } else {
        features |= ChartViewFeature::RangeMarkers;
    }
    return features;
}

QVector<WidgetViewOption> AudioAnalyzerWidget::leadingViewOptions() const {
    WidgetViewOption mode;
    mode.id = QLatin1String(kModeKey);
    mode.label = tr("View:");
    mode.kind = WidgetViewOption::Kind::Choice;
    mode.value =
        m_mode == Mode::Spectrogram ? QStringLiteral("spectrogram") : QStringLiteral("spectrum");
    mode.choices = {{QStringLiteral("spectrum"), tr("Spectrum")},
                    {QStringLiteral("spectrogram"), tr("Spectrogram")}};
    return {mode};
}

QVector<WidgetViewOption> AudioAnalyzerWidget::extraViewOptions() const {
    // Only the current view's own options.
    QVector<WidgetViewOption> options =
        m_mode == Mode::Spectrum ? spectrumViewOptions() : spectrogramViewOptions();
    if (!options.isEmpty()) {
        options.first().startsSection = true;
    }
    return options;
}

void AudioAnalyzerWidget::readExtraView(const QJsonObject& view) {
    m_mode = view.value(QLatin1String(kModeKey)).toString() == QLatin1String("spectrogram")
                 ? Mode::Spectrogram
                 : Mode::Spectrum;
    readSpectrumView(view);
    readSpectrogramView(view);
}

void AudioAnalyzerWidget::writeExtraView(QJsonObject& view) const {
    view[QLatin1String(kModeKey)] =
        m_mode == Mode::Spectrogram ? QStringLiteral("spectrogram") : QStringLiteral("spectrum");
    view[QLatin1String(kPeakKey)] = m_peakHold;
    view[QLatin1String(kScaleKey)] = m_linear ? QStringLiteral("linear") : QStringLiteral("db");
    view[QLatin1String(kHistoryKey)] = QString::number(m_historySeconds);
    view[QLatin1String(kFloorKey)] = QString::number(m_floorDb);
    view[QLatin1String(kColorsKey)] = m_colorMap == ColorMap::Viridis ? QStringLiteral("viridis")
                                      : m_colorMap == ColorMap::Theme ? QStringLiteral("theme")
                                                                      : QStringLiteral("inferno");
    view[QLatin1String(kColorScaleKey)] = m_showColorScale;
}

void AudioAnalyzerWidget::analysisReset() {
    resetSpectrum();
    resetSpectrogram();
}

void AudioAnalyzerWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    paintChartBackground(painter, *this, palette);
    if (paintPlaceholder(painter)) {
        return;
    }
    if (m_mode == Mode::Spectrum) {
        paintSpectrum(painter, palette);
    } else {
        paintSpectrogram(painter, palette);
    }
}

// --- Spectrum -------------------------------------------------------------

QVector<WidgetViewOption> AudioAnalyzerWidget::spectrumViewOptions() const {
    WidgetViewOption peak;
    peak.id = QLatin1String(kPeakKey);
    peak.label = tr("Peak hold");
    peak.value = m_peakHold;

    WidgetViewOption scale;
    scale.id = QLatin1String(kScaleKey);
    scale.label = tr("Level scale:");
    scale.kind = WidgetViewOption::Kind::Choice;
    scale.value = m_linear ? QStringLiteral("linear") : QStringLiteral("db");
    scale.choices = {{QStringLiteral("db"), tr("dBFS")},
                     {QStringLiteral("linear"), tr("Linear amplitude")}};
    return {scale, peak};
}

void AudioAnalyzerWidget::readSpectrumView(const QJsonObject& view) {
    m_peakHold = view.value(QLatin1String(kPeakKey)).toBool(true);
    m_linear = view.value(QLatin1String(kScaleKey)).toString() == QLatin1String("linear");
}

double AudioAnalyzerWidget::levelValue(float db) const {
    return m_linear ? std::pow(10.0, double(db) / 20.0) : double(db);
}

void AudioAnalyzerWidget::resetSpectrum() {
    m_spectrumDb.clear();
    m_peakDb.clear();
    m_linearTop = 1.0;
}

void AudioAnalyzerWidget::refreshSpectrum() {
    if (!fillWindow()) {
        return;  // not enough samples for this FFT size yet
    }
    computeSpectrum(m_spectrumDb);
    if (m_peakDb.size() != m_spectrumDb.size()) {
        m_peakDb = m_spectrumDb;
    } else {
        for (int i = 0; i < m_peakDb.size(); ++i) {
            m_peakDb[i] = std::max(m_spectrumDb[i], m_peakDb[i] - kPeakFallDbPerTick);
        }
    }
}

void AudioAnalyzerWidget::paintSpectrum(QPainter& painter, const ThemePalette& palette) {
    // Laid out like the line chart (layoutCartesianChart): the info row
    // takes the top legend row, the frequency labels the X axis band.
    const ChartStyle& style = chartStyle(effectiveStyle());
    const ChartColors colors = chartColors(style, palette);
    const CartesianChrome chrome = audioChrome(m_linear ? 2 : kDbGridDivisions);
    ChartAxisRequest levelAxis;
    if (m_linear) {
        if (!m_spectrumDb.isEmpty()) {
            const QVector<float>& shown =
                m_peakHold && m_peakDb.size() == m_spectrumDb.size() ? m_peakDb : m_spectrumDb;
            const float loudest = *std::max_element(shown.cbegin(), shown.cend());
            const double target = niceCeil(qMax(kMinLinearTop, levelValue(loudest)));
            if (target > m_linearTop || target * 4.0 < m_linearTop) {
                m_linearTop = target;
            }
        }
        levelAxis.range = {0.0, m_linearTop, AxisRange::Source::Declared};
        levelAxis.configDecimals = linearDecimals(m_linearTop);
        levelAxis.title = tr("Amplitude (FS)");
    } else {
        levelAxis.range = {double(kMinDb), double(kMaxDb), AxisRange::Source::Fixed};
        levelAxis.title = tr("dBFS");
    }
    // The frequency ticks are hand-placed (the log axis is not a linear
    // ValueScale): the factory only makes the layout reserve their band.
    const ChartCartesianLayout layout = layoutCartesianChart(
        painter, rect(), {levelAxis}, chrome, style, [](int, int) { return ValueScale(); });
    const QRect& plotRect = layout.plotRect;
    const QRectF plot(plotRect);
    if (plot.width() < 20 || plot.height() < 20) {
        return;
    }

    double peakHz = -1.0;
    if (m_spectrumDb.size() > 1) {
        const auto loudest = std::max_element(m_spectrumDb.cbegin() + 1, m_spectrumDb.cend());
        peakHz = double(loudest - m_spectrumDb.cbegin()) * sampleRate() / m_analyzer.size();
    }
    paintInfoRow(painter, layout, colors, peakHz, windowFields());
    if (m_view.showInfoRow && extraInfoRows() > 0) {
        paintStatisticsRow(painter, layout, colors);
    }

    QVector<ChartPixelTick> xTicks;
    for (double f : frequencyTicks()) {
        xTicks.append({plot.left() + plot.width() * frequencyFraction(f), frequencyLabel(f)});
    }
    if (chrome.showGrid) {
        paintChartPixelGrid(painter, plotRect, xTicks, {}, style, colors);
    }
    QVector<QRect> occupied;
    paintCartesianAxes(painter, layout, chrome, style, colors, tr("Frequency (Hz)"), {},
                       &occupied);
    paintChartPixelXTicks(painter, layout, chrome, xTicks, style, colors, &occupied);

    if (m_spectrumDb.isEmpty()) {
        return;
    }
    const ValueScale& levelScale = layout.yAxes.first().scale;
    auto yFor = [this, &plotRect, &levelScale](float db) {
        const double value = levelValue(std::clamp(db, SpectrumAnalyzer::kFloorDb, kMaxDb));
        return chartValueToY(plotRect, levelScale,
                             std::clamp(value, levelScale.lo, levelScale.hi));
    };
    // One point per pixel column.
    const int columns = int(plot.width());
    auto curve = [&](const QVector<float>& db) {
        QPainterPath path;
        for (int px = 0; px <= columns; ++px) {
            const float value = levelBetween(db, frequencyAt(double(px) / columns),
                                             frequencyAt(double(px + 1) / columns));
            const QPointF point(plot.left() + px, yFor(value));
            if (px == 0) {
                path.moveTo(point);
            } else {
                path.lineTo(point);
            }
        }
        return path;
    };

    painter.save();
    painter.setClipRect(plotRect.adjusted(-1, -1, 1, 1));
    const QPainterPath line = curve(m_spectrumDb);
    if (m_view.fillArea) {
        QPainterPath fill = line;
        fill.lineTo(plot.right(), plot.bottom());
        fill.lineTo(plot.left(), plot.bottom());
        fill.closeSubpath();
        QColor area = palette.accent;
        area.setAlphaF(0.25);
        painter.fillPath(fill, area);
    }
    painter.setPen(QPen(palette.accent, effectiveLineWidth(m_view, style)));
    painter.drawPath(line);
    if (m_peakHold && m_peakDb.size() == m_spectrumDb.size()) {
        QColor peak = palette.textSecondary;
        peak.setAlphaF(0.7);
        painter.setPen(QPen(peak, 1));
        painter.drawPath(curve(m_peakDb));
    }
    painter.restore();

    if (hoverInside(plotRect)) {
        const double t = (m_hoverPos.x() - plot.left()) / plot.width();
        const double hz = frequencyAt(t);
        const float level =
            levelBetween(m_spectrumDb, hz, frequencyAt(t + 1.0 / qMax(1, columns)));
        const QString levelText = m_linear
                                      ? QString::number(levelValue(level), 'g', 4)
                                      : tr("%1 dB").arg(double(level), 0, 'f', 1);
        paintHoverBalloon(painter, plotRect, {tr("%1 Hz").arg(frequencyLabel(hz)), levelText},
                          true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette.accent);
        painter.drawEllipse(QPointF(m_hoverPos.x(), yFor(level)), 3.5, 3.5);
    }
}

// --- Spectrogram ----------------------------------------------------------

QVector<WidgetViewOption> AudioAnalyzerWidget::spectrogramViewOptions() const {
    WidgetViewOption history;
    history.id = QLatin1String(kHistoryKey);
    history.label = tr("History:");
    history.kind = WidgetViewOption::Kind::Choice;
    history.value = QString::number(m_historySeconds);
    for (int seconds : kHistoryChoices) {
        history.choices.append({QString::number(seconds), tr("%1 s").arg(seconds)});
    }

    WidgetViewOption floor;
    floor.id = QLatin1String(kFloorKey);
    floor.label = tr("Level floor:");
    floor.kind = WidgetViewOption::Kind::Choice;
    floor.value = QString::number(m_floorDb);
    for (int db : kFloorChoices) {
        floor.choices.append({QString::number(db), tr("%1 dB").arg(db)});
    }

    WidgetViewOption colors;
    colors.id = QLatin1String(kColorsKey);
    colors.label = tr("Colors:");
    colors.kind = WidgetViewOption::Kind::Choice;
    colors.value = m_colorMap == ColorMap::Viridis ? QStringLiteral("viridis")
                   : m_colorMap == ColorMap::Theme ? QStringLiteral("theme")
                                                   : QStringLiteral("inferno");
    colors.choices = {{QStringLiteral("inferno"), tr("Inferno")},
                      {QStringLiteral("viridis"), tr("Viridis")},
                      {QStringLiteral("theme"), tr("Theme accent")}};

    WidgetViewOption colorScale;
    colorScale.id = QLatin1String(kColorScaleKey);
    colorScale.label = tr("Show color scale");
    colorScale.value = m_showColorScale;
    return {history, floor, colors, colorScale};
}

void AudioAnalyzerWidget::readSpectrogramView(const QJsonObject& view) {
    const int history =
        view.value(QLatin1String(kHistoryKey)).toString(QStringLiteral("10")).toInt();
    int seconds = 10;
    for (int choice : kHistoryChoices) {
        if (choice == history) {
            seconds = choice;
        }
    }
    const int floorValue =
        view.value(QLatin1String(kFloorKey)).toString(QStringLiteral("-100")).toInt();
    int floorDb = -100;
    for (int choice : kFloorChoices) {
        if (choice == floorValue) {
            floorDb = choice;
        }
    }
    const QString colors = view.value(QLatin1String(kColorsKey)).toString();
    const ColorMap map = colors == QLatin1String("viridis") ? ColorMap::Viridis
                         : colors == QLatin1String("theme") ? ColorMap::Theme
                                                            : ColorMap::Inferno;

    m_showColorScale = view.value(QLatin1String(kColorScaleKey)).toBool(true);

    const bool retimed = seconds != m_historySeconds;
    const bool recolored = floorDb != m_floorDb || map != m_colorMap;
    m_historySeconds = seconds;
    m_floorDb = floorDb;
    m_colorMap = map;
    if (retimed) {
        resetSpectrogram();  // a column now stands for another stretch of time
    } else if (recolored) {
        rebuildColors();
    }
}

void AudioAnalyzerWidget::refreshDataColors() {
    if (m_colorMap == ColorMap::Theme) {
        rebuildColors();
    }
}

void AudioAnalyzerWidget::resetSpectrogram() {
    m_levels.fill(SpectrumAnalyzer::kFloorDb);
    m_image.fill(Qt::transparent);
    m_nextColumn = 0;
    m_filledColumns = 0;
    m_sinceColumn = 0;
    m_columnDb.clear();
    m_summaries.fill(ColumnSummary());
    m_pending = ColumnSummary();
    m_pendingValues.clear();
}

float AudioAnalyzerWidget::newestLevel(int row) const {
    if (m_filledColumns == 0 || row < 0 || row >= kRows) {
        return SpectrumAnalyzer::kFloorDb;
    }
    const int column = (m_nextColumn - 1 + kColumns) % kColumns;
    return m_levels[column * kRows + row];
}

int AudioAnalyzerWidget::hopSamples() const {
    return qMax(1, int(std::lround(sampleRate() * m_historySeconds / kColumns)));
}

void AudioAnalyzerWidget::samplesAppended(int count) {
    const int hop = hopSamples();
    for (int back = count - 1; back >= 0; --back) {
        // Summed into the column being gathered, oldest sample first.
        const float value = historySample(back);
        if (m_pending.count == 0) {
            m_pending.min = value;
            m_pending.max = value;
        }
        m_pending.min = qMin(m_pending.min, value);
        m_pending.max = qMax(m_pending.max, value);
        m_pending.sum += value;
        m_pending.sumSq += double(value) * value;
        ++m_pending.count;
        m_pendingValues.append(value);

        if (++m_sinceColumn < hop) {
            continue;
        }
        m_sinceColumn = 0;
        // The FFT window ends at this column's moment: `back` samples
        // before the newest one.
        const int column = m_nextColumn;
        if (addColumn(back)) {
            auto middle = m_pendingValues.begin() + m_pendingValues.size() / 2;
            std::nth_element(m_pendingValues.begin(), middle, m_pendingValues.end());
            m_pending.median = *middle;
            m_summaries[column] = m_pending;
        }
        m_pending = ColumnSummary();
        m_pendingValues.clear();
    }
}

SeriesStatistics AudioAnalyzerWidget::signalStatistics(int backLo, int backHi) const {
    SeriesStatistics stats;
    backLo = qMax(0, backLo);
    backHi = qMin(m_filledColumns - 1, backHi);
    double sum = 0.0;
    double sumSq = 0.0;
    QVector<double> medians;
    for (int back = backLo; back <= backHi; ++back) {
        const ColumnSummary& column = m_summaries[(m_nextColumn - 1 - back + 2 * kColumns) %
                                                  kColumns];
        if (column.count == 0) {
            continue;
        }
        if (stats.count == 0) {
            stats.min = column.min;
            stats.max = column.max;
        }
        stats.min = qMin(stats.min, double(column.min));
        stats.max = qMax(stats.max, double(column.max));
        sum += column.sum;
        sumSq += column.sumSq;
        stats.count += column.count;
        medians.append(column.median);
    }
    if (stats.count == 0) {
        return stats;
    }
    stats.mean = sum / stats.count;
    stats.rms = std::sqrt(sumSq / stats.count);
    stats.peak = qMax(qAbs(stats.min), qAbs(stats.max));
    stats.median = seriesStatistics(medians, 0, medians.size() - 1).median;
    return stats;
}

bool AudioAnalyzerWidget::addColumn(int endOffset) {
    if (!fillWindow(endOffset)) {
        return false;  // not enough samples for this FFT size yet
    }
    computeSpectrum(m_columnDb);
    const int column = m_nextColumn;
    for (int row = 0; row < kRows; ++row) {
        const double t0 = 1.0 - double(row + 1) / kRows;
        const double t1 = 1.0 - double(row) / kRows;
        const float level = levelBetween(m_columnDb, frequencyAt(t0), frequencyAt(t1));
        m_levels[column * kRows + row] = level;
        reinterpret_cast<QRgb*>(m_image.scanLine(row))[column] = colorFor(level);
    }
    m_nextColumn = (m_nextColumn + 1) % kColumns;
    m_filledColumns = qMin(kColumns, m_filledColumns + 1);
    return true;
}

qreal AudioAnalyzerWidget::markerX(int which) const {
    const QRectF plot(m_plotRect);
    return plot.right() - plot.width() / kColumns * m_markerAgo[which];
}

bool AudioAnalyzerWidget::markersShown() const {
    return m_mode == Mode::Spectrogram && m_view.rangeMarkers && !qIsNaN(m_markerAgo[0]);
}

int AudioAnalyzerWidget::markerAt(const QPoint& pos) const {
    if (!markersShown()) {
        return -1;
    }
    return chartRangeMarkerAt(m_plotRect, markerX(0), markerX(1), pos);
}

void AudioAnalyzerWidget::mousePressEvent(QMouseEvent* event) {
    m_draggedMarker = event->button() == Qt::LeftButton ? markerAt(event->pos()) : -1;
    if (m_draggedMarker >= 0) {
        event->accept();
        return;
    }
    AudioStreamWidget::mousePressEvent(event);
}

void AudioAnalyzerWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_draggedMarker >= 0) {
        const QRectF plot(m_plotRect);
        if (plot.width() > 0) {
            m_markerAgo[m_draggedMarker] = qBound(
                0.0, (plot.right() - event->pos().x()) / (plot.width() / kColumns),
                double(kColumns));
            update();
        }
        event->accept();
        return;
    }
    if (markerAt(event->pos()) >= 0) {
        setCursor(Qt::SizeHorCursor);
    } else {
        unsetCursor();
    }
    AudioStreamWidget::mouseMoveEvent(event);
}

void AudioAnalyzerWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (m_draggedMarker >= 0) {
        m_draggedMarker = -1;
        event->accept();
        return;
    }
    AudioStreamWidget::mouseReleaseEvent(event);
}

int AudioAnalyzerWidget::extraInfoRows() const {
    for (const QString& item : m_view.infoItems) {
        if (isChartInfoStatistic(item)) {
            return 1;
        }
    }
    return 0;
}

QVector<ChartInfoField> AudioAnalyzerWidget::windowFields() const {
    // What the picture covers, in samples and seconds, and the A..B span.
    const int hop = hopSamples();
    const double columnSeconds = double(m_historySeconds) / kColumns;
    QVector<ChartInfoField> fields;
    if (m_view.infoItems.contains(QLatin1String("samples"))) {
        fields.append({tr("%1 samples"), QString::number(qint64(m_filledColumns) * hop),
                       QString(QString::number(qint64(kColumns) * hop).size(),
                               QLatin1Char('0'))});
    }
    if (m_view.infoItems.contains(QLatin1String("span"))) {
        fields.append({tr("%1 s"), QString::number(m_filledColumns * columnSeconds, 'f', 1),
                       QStringLiteral("00.0")});
    }
    if (markersShown()) {
        fields.append({tr("A-B %1 s"),
                       QString::number(qAbs(m_markerAgo[0] - m_markerAgo[1]) * columnSeconds,
                                       'f', 2),
                       QStringLiteral("00.00")});
    }
    return fields;
}

void AudioAnalyzerWidget::paintStatisticsRow(QPainter& painter,
                                                const ChartCartesianLayout& layout,
                                                const ChartColors& colors) const {
    // Over the whole history, or between the markers: the columns whose
    // middle lies inside A..B.
    int backLo = 0;
    int backHi = kColumns - 1;
    if (markersShown()) {
        backLo = int(std::ceil(qMin(m_markerAgo[0], m_markerAgo[1]) - 0.5));
        backHi = int(std::floor(qMax(m_markerAgo[0], m_markerAgo[1]) - 0.5));
    }
    const SeriesStatistics stats = signalStatistics(backLo, backHi);
    // Levels in dBFS (a full-scale sine's RMS reads 0), the rest in
    // full-scale units.
    auto dbfs = [](double value) { return value > 0.0 ? 20.0 * std::log10(value) : -140.0; };
    const QString fsWidest = QStringLiteral("-0.000");
    const QString dbWidest = QStringLiteral("-000.0");
    QVector<ChartInfoField> fields;
    for (const QString& item : m_view.infoItems) {
        QString format;
        double value = 0.0;
        bool decibels = false;
        if (item == QLatin1String("min")) {
            format = tr("min %1");
            value = stats.min;
        } else if (item == QLatin1String("max")) {
            format = tr("max %1");
            value = stats.max;
        } else if (item == QLatin1String("p2p")) {
            format = tr("p-p %1");
            value = stats.peakToPeak();
        } else if (item == QLatin1String("peak")) {
            format = tr("peak %1 dBFS");
            value = dbfs(stats.peak);
            decibels = true;
        } else if (item == QLatin1String("mean")) {
            format = tr("mean %1");
            value = stats.mean;
        } else if (item == QLatin1String("median")) {
            format = tr("median %1");
            value = stats.median;
        } else if (item == QLatin1String("rms")) {
            format = tr("rms %1 dBFS");
            value = dbfs(stats.rms * std::sqrt(2.0));
            decibels = true;
        } else {
            continue;
        }
        const QString text = stats.count == 0 ? QStringLiteral("--")
                             : decibels       ? QString::number(value, 'f', 1)
                                              : QString::number(value, 'f', 3);
        fields.append({format, text, decibels ? dbWidest : fsWidest});
    }
    paintChartInfoRow(painter, layout, 1, fields, QString(), colors);
}

QRgb AudioAnalyzerWidget::colorFor(float db) const {
    const float t = std::clamp((db - float(m_floorDb)) / float(-m_floorDb), 0.0f, 1.0f);
    return m_lut[int(t * (m_lut.size() - 1) + 0.5f)];
}

void AudioAnalyzerWidget::rebuildColors() {
    m_lut.resize(256);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    for (int i = 0; i < m_lut.size(); ++i) {
        const double t = double(i) / (m_lut.size() - 1);
        switch (m_colorMap) {
            case ColorMap::Inferno:
                m_lut[i] = sampleStops(kInferno, t);
                break;
            case ColorMap::Viridis:
                m_lut[i] = sampleStops(kViridis, t);
                break;
            case ColorMap::Theme:
                // The chart surface, through the accent, to the text color.
                m_lut[i] = t < 0.7 ? mix(palette.surface.rgb(), palette.accent.rgb(), t / 0.7)
                                   : mix(palette.accent.rgb(), palette.textPrimary.rgb(),
                                         (t - 0.7) / 0.3);
                break;
        }
    }
    for (int column = 0; column < m_filledColumns; ++column) {
        const int index = (m_nextColumn - 1 - column + 2 * kColumns) % kColumns;
        for (int row = 0; row < kRows; ++row) {
            reinterpret_cast<QRgb*>(m_image.scanLine(row))[index] =
                colorFor(m_levels[index * kRows + row]);
        }
    }
    update();
}

int AudioAnalyzerWidget::colorScaleWidth(const QFontMetrics& fm, const QFontMetrics& tickFm,
                                            const ChartStyle& style) const {
    if (!m_showColorScale) {
        return 0;
    }
    // The floor is the longest label ("-120").
    int width = kColorBarWidth + style.tickLabelOffset +
                tickFm.horizontalAdvance(formatTick(m_floorDb, 0));
    if (m_view.showYAxisTitle) {
        width += kChartAxisLabelGap + fm.height() + 4;  // a rotated title strip
    }
    return width;
}

void AudioAnalyzerWidget::paintColorScale(QPainter& painter, const QRect& plotRect,
                                             const QFont& tickFont, const ChartStyle& style,
                                             const ChartColors& colors) {
    const QRectF bar(plotRect.right() + 1 + kColorBarGap, plotRect.top(), kColorBarWidth,
                     plotRect.height());
    if (bar.height() < 20) {
        return;
    }
    // The map itself, 0 dB on top.
    QImage ramp(1, m_lut.size(), QImage::Format_RGB32);
    for (int i = 0; i < m_lut.size(); ++i) {
        reinterpret_cast<QRgb*>(ramp.scanLine(i))[0] = m_lut[m_lut.size() - 1 - i];
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(bar, ramp);
    painter.restore();
    painter.setPen(QPen(colors.frame, style.frameWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(crispCoord(bar.left()), crispCoord(bar.top()), bar.width() - 1,
                            bar.height() - 1));

    // Round-number dB ticks, outward toward their labels.
    const QFont baseFont = painter.font();
    const QFontMetrics tickFm(tickFont);
    const NiceScale nice = niceScale(m_floorDb, 0.0,
                                     maxTicksFor(int(bar.height()), tickFm.height() * 2), false);
    const qreal barRight = bar.right();
    const int labelLeft = int(barRight) + style.tickLabelOffset;
    int labelWidth = 0;
    painter.setFont(tickFont);
    for (double tick : nice.ticks) {
        const qreal y = crispCoord(bar.bottom() - bar.height() * (tick - m_floorDb) / -m_floorDb);
        painter.setPen(QPen(colors.frame, style.frameWidth));
        painter.drawLine(QPointF(barRight, y),
                         QPointF(barRight + qMin(style.tickLength, style.tickLabelOffset - 1), y));
        const QString text = formatTick(tick, nice.decimals);
        labelWidth = qMax(labelWidth, tickFm.horizontalAdvance(text));
        painter.setPen(colors.tickLabel);
        painter.drawText(QRect(labelLeft, qRound(y) - tickFm.height() / 2,
                               tickFm.horizontalAdvance(text) + 1, tickFm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, text);
    }
    painter.setFont(baseFont);

    // Same rotated title as a Y axis', on the far side of the labels.
    if (m_view.showYAxisTitle) {
        const QFontMetrics fm(baseFont);
        const QString title = tr("dBFS");
        const int stripWidth = fm.height() + 4;
        const int centerX = labelLeft + qMax(labelWidth, tickFm.horizontalAdvance(
                                                             formatTick(m_floorDb, 0))) +
                            kChartAxisLabelGap + stripWidth / 2;
        painter.save();
        painter.setPen(colors.axisTitle);
        painter.translate(centerX, bar.center().y());
        painter.rotate(-90);
        const int textWidth = fm.horizontalAdvance(title);
        painter.drawText(QRect(-textWidth / 2, -fm.height() / 2, textWidth, fm.height()),
                         Qt::AlignCenter, title);
        painter.restore();
    }
}

void AudioAnalyzerWidget::paintSpectrogram(QPainter& painter, const ThemePalette& palette) {
    // Laid out like the line chart: the info row in the top legend row, the
    // time axis below the plot as a line chart's Time axis ("-10 ... 0 s").
    const ChartStyle& style = chartStyle(effectiveStyle());
    const ChartColors colors = chartColors(style, palette);
    const CartesianChrome chrome = audioChrome(2);
    ChartAxisRequest frequencyAxis;
    // Only reserves the label column: the ticks are hand-placed below.
    frequencyAxis.range = {0.0, topFrequency(), AxisRange::Source::Fixed};
    frequencyAxis.title = tr("Frequency (Hz)");
    const double span = double(m_historySeconds);
    const bool xLabeled = chrome.xTickLabels;
    // The color scale takes its room off the layout's right side; the plot's
    // own right margin already holds the gap to it, in part.
    const QFont tickFont = chartTickFont(painter.font(), style);
    const int scaleWidth =
        colorScaleWidth(QFontMetrics(painter.font()), QFontMetrics(tickFont), style);
    const QRect area =
        scaleWidth > 0 ? rect().adjusted(0, 0, -(scaleWidth + kColorBarGap - kChartAxisLabelGap), 0)
                       : rect();
    const ChartCartesianLayout layout = layoutCartesianChart(
        painter, area, {frequencyAxis}, chrome, style,
        [span, &style, xLabeled](int plotWidth, int maxTicks) {
            ValueScale scale;
            scale.lo = -span;
            scale.hi = 0.0;
            if (style.ticks == ChartTickPlacement::Divisions && !xLabeled) {
                const int lines = qBound(kTimeGridMinLines, plotWidth / kTimeGridSpacingPx,
                                         kTimeGridMaxLines);
                for (int i = 1; i < lines; ++i) {
                    scale.gridTicks.append(scale.lo + span * i / lines);
                }
                return scale;
            }
            const NiceScale nice = niceScale(scale.lo, scale.hi, maxTicks, false);
            scale.ticks = nice.ticks;
            scale.gridTicks = nice.ticks;
            scale.decimals = nice.decimals;
            return scale;
        });
    const QRect& plotRect = layout.plotRect;
    const QRectF plot(plotRect);
    if (plot.width() < 20 || plot.height() < 20) {
        return;
    }
    m_plotRect = plotRect;
    if (m_view.rangeMarkers && qIsNaN(m_markerAgo[0])) {
        // First shown: A at a quarter of the history, B at three quarters.
        m_markerAgo[0] = 0.75 * kColumns;
        m_markerAgo[1] = 0.25 * kColumns;
    }

    double peakHz = -1.0;
    if (m_columnDb.size() > 1) {
        const auto loudest = std::max_element(m_columnDb.cbegin() + 1, m_columnDb.cend());
        peakHz = double(loudest - m_columnDb.cbegin()) * sampleRate() / m_analyzer.size();
    }
    paintInfoRow(painter, layout, colors, peakHz, windowFields());
    if (m_view.showInfoRow && extraInfoRows() > 0) {
        paintStatisticsRow(painter, layout, colors);
    }

    // Grid first, under the picture: over the colors it only adds noise, so
    // it shows where no column has been drawn yet.
    QVector<ChartPixelTick> xGrid;
    for (qreal x : chartXGridPixels(layout)) {
        xGrid.append({x, QString()});
    }
    QVector<ChartPixelTick> yTicks;
    for (double f : frequencyTicks()) {
        yTicks.append({plot.bottom() - plot.height() * frequencyFraction(f), frequencyLabel(f)});
    }
    if (chrome.showGrid) {
        paintChartPixelGrid(painter, plotRect, xGrid, yTicks, style, colors);
    }

    // The picture: oldest column on the left, the newest on the right edge.
    // Before the axes, so the frame stays on top of it.
    const qreal columnWidth = plot.width() / kColumns;
    if (m_filledColumns > 0) {
        painter.save();
        painter.setClipRect(plotRect);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const int oldest = (m_nextColumn - m_filledColumns + kColumns) % kColumns;
        const int firstRun = qMin(m_filledColumns, kColumns - oldest);
        const QRectF first(plot.right() - m_filledColumns * columnWidth, plot.top(),
                           firstRun * columnWidth, plot.height());
        painter.drawImage(first, m_image, QRectF(oldest, 0, firstRun, kRows));
        if (m_filledColumns > firstRun) {
            const QRectF second(first.right(), plot.top(),
                                (m_filledColumns - firstRun) * columnWidth, plot.height());
            painter.drawImage(second, m_image, QRectF(0, 0, m_filledColumns - firstRun, kRows));
        }
        painter.restore();
    }

    // The grid is drawn: the axes keep their frame, marks and labels only.
    ChartCartesianLayout axesLayout = layout;
    axesLayout.yAxes.first().scale.ticks.clear();
    axesLayout.yAxes.first().scale.gridTicks.clear();
    axesLayout.xScale.gridTicks.clear();
    QVector<QRect> occupied;
    paintCartesianAxes(painter, axesLayout, chrome, style, colors,
                       chartTimeAxisTitle(ChartXAxisMode::Time), {}, &occupied);
    paintChartPixelYTicks(painter, axesLayout, chrome, yTicks, style, colors, &occupied);
    if (scaleWidth > 0) {
        paintColorScale(painter, plotRect, tickFont, style, colors);
    }
    if (m_view.rangeMarkers) {
        paintChartRangeMarkers(painter, plotRect, markerX(0), markerX(1), palette);
    }

    if (hoverInside(plotRect)) {
        const double fromRight = (plot.right() - m_hoverPos.x()) / columnWidth;
        const int back = int(fromRight);  // columns before the newest
        const double fraction = (plot.bottom() - m_hoverPos.y()) / plot.height();
        const int row = qBound(0, int((1.0 - fraction) * kRows), kRows - 1);
        QStringList lines;
        lines << tr("%1 s").arg(-fromRight * span / kColumns, 0, 'f', 2)
              << tr("%1 Hz").arg(frequencyLabel(frequencyAt(fraction)));
        if (back < m_filledColumns) {
            const int column = (m_nextColumn - 1 - back + 2 * kColumns) % kColumns;
            lines << tr("%1 dB").arg(double(m_levels[column * kRows + row]), 0, 'f', 1);
        }
        paintHoverBalloon(painter, plotRect, lines, true);
    }
}

}  // namespace traceview
