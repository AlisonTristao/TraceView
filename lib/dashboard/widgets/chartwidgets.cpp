#include "chartwidgets.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QTimer>
#include <QtMath>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

// Space between the plot's outer chrome (gridlines, legend, unit/value
// labels) and the widget's own edge -- keeps everything from reading as
// flush/glued to the widget's border.
constexpr int kOuterPadding = 12;
// Tighter gap used only between adjacent pieces of axis chrome that belong
// together (the unit strip and the value gutter, the value gutter and the
// plot itself) -- deliberately smaller than kOuterPadding so the unit label
// reads as attached to its axis rather than floating apart from it.
constexpr int kAxisLabelGap = 4;
// Caps how often appendFieldSample() triggers an actual repaint, independent
// of how fast samples arrive (topico 14 PASSO 12: ingestion rate stays
// separate from repaint rate) -- data still gets appended to the buffers on
// every sample.
constexpr int kRepaintIntervalMs = 33; // ~30 Hz
// Shared with paintSeriesLegends()'s swatch dot so plotTopMargin()/
// plotBottomMargin() below can predict the legend rows' real height instead
// of guessing at it.
constexpr int kLegendSwatchSize = 8;
// Horizontal gap between adjacent legend columns (see legendColumns()).
constexpr int kLegendItemGap = 14;

// Thickness of the rotated unit-label strip immediately left of the
// Y-axis value gutter (only reserved when a unit is configured). Sized to
// the actual font height rather than a plain constant like kAxisGutter --
// unlike the numeric labels' width, which tolerates being wider than the
// text, this strip's *thickness* is the rotated text's cap height, so an
// undersized fixed constant would clip descenders at larger font sizes.
int unitStripWidth(const QPainter& painter) {
    return QFontMetrics(painter.font()).height() + 4;
}

// Widest of the three Y-axis value labels' (min/mid/max) rendered widths.
// Callers reserve exactly this much gutter space instead of a fixed
// worst-case width most values never fill -- e.g. "0"/"50"/"100" only needs
// a third of what "-100000"/"0"/"100000" would, so a flat constant either
// wastes space for small ranges or clips large ones.
int axisLabelWidth(const QPainter& painter, double yMin, double yMax) {
    const QFontMetrics fm(painter.font());
    const int maxWidth = fm.horizontalAdvance(QString::number(yMax, 'g', 4));
    const int midWidth = fm.horizontalAdvance(QString::number((yMin + yMax) / 2.0, 'g', 4));
    const int minWidth = fm.horizontalAdvance(QString::number(yMin, 'g', 4));
    return qMax(maxWidth, qMax(midWidth, minWidth));
}

// Rounded to the same curve as the DashboardCell wrapped around `widget` --
// via contentFillPath(), spanning `widget`'s true bounds, not an inset
// rect -- so straight edges stay flush with the cell's true edge and the
// DashboardCell overlay can draw the outer outline above it. A plain
// drawRect() here would run straight into the corner
// well past that curve, leaving a few pixels of straight edge poking out
// past the rounded outline. See "Corner radius" in docs/VISUAL_IDENTITY.md.
void paintBackground(QPainter& painter, const DashboardWidget& widget, const ThemePalette& palette) {
    painter.fillPath(widget.contentFillPath(), palette.surface);

    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(Qt::NoBrush);
    // Inset by 0.5 so the 1px pen renders at full strength instead of half
    // of it landing outside this widget's own paint device and getting
    // clipped -- same idea as DashboardCell's own outline stroke inset,
    // just for this widget's separate, decorative inner border.
    const QRectF strokeRect = QRectF(widget.rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.drawPath(widget.roundedPath(strokeRect));
}

// Three horizontal gridlines (min/mid/max) across plotRect's width, each
// with its value labeled in a gutter to plotRect's left sized to the
// widest of the three (see axisLabelWidth()). Deliberately just 3 -- a
// busier grid would fight the throttled, frequently-redrawn plot lines/bars
// for attention on a widget this small. The value labels (and unit) stay up
// regardless of `showGrid` -- that toggle is about the guide lines across
// the plot, not about losing the ability to read the axis.
void paintYAxis(QPainter& painter, const QRect& plotRect, double yMin, double yMax, const QString& unit,
                 bool showGrid, const ThemePalette& palette) {
    if (plotRect.height() <= 0 || plotRect.width() <= 0) {
        return;
    }
    const int midY = plotRect.center().y();

    if (showGrid) {
        painter.setPen(QPen(palette.border, 1));
        painter.drawLine(plotRect.left(), plotRect.top(), plotRect.right(), plotRect.top());
        painter.drawLine(plotRect.left(), midY, plotRect.right(), midY);
        painter.drawLine(plotRect.left(), plotRect.bottom(), plotRect.right(), plotRect.bottom());
    }

    painter.setPen(palette.textSecondary);
    const QFontMetrics fm(painter.font());
    // Derived from plotRect itself (not a separate running x) so this stays
    // correct regardless of whether the caller reserved extra left space
    // for the rotated unit strip below.
    const int gutterRight = plotRect.left() - kAxisLabelGap;
    const int labelWidth = axisLabelWidth(painter, yMin, yMax);
    const QRect gutter(gutterRight - labelWidth, 0, labelWidth, fm.height());
    auto drawValue = [&](double value, int centerY) {
        painter.drawText(QRect(gutter.x(), centerY - gutter.height() / 2, gutter.width(), gutter.height()),
                          Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'g', 4));
    };
    drawValue(yMax, plotRect.top());
    drawValue((yMin + yMax) / 2.0, midY);
    drawValue(yMin, plotRect.bottom());

    // Unit as its own vertical label in the strip left of the gutter --
    // not glued onto any one number -- centered on the axis's midpoint,
    // rotated to read bottom-to-top like a conventional axis title. Sits
    // right against gutter.left(), which is already sized to the labels'
    // actual width (see above), not a fixed box most values never fill.
    if (!unit.isEmpty()) {
        const int stripWidth = unitStripWidth(painter);
        const int stripCenterX = gutter.left() - kAxisLabelGap - stripWidth / 2;
        painter.save();
        painter.translate(stripCenterX, midY);
        painter.rotate(-90);
        const int textWidth = fm.horizontalAdvance(unit);
        painter.drawText(QRect(-textWidth / 2, -fm.height() / 2, textWidth, fm.height()), Qt::AlignCenter, unit);
        painter.restore();
    }
}

// Vertical gridlines across plotRect's height, spaced by a target pixel
// width rather than pinned to the sample count -- one line per tick would
// pack far too many for a full buffer (or, early on with few samples
// buffered, leave the plot with almost none), so a target spacing keeps the
// grid reading as evenly dense regardless of how much history is currently
// held. Deliberately denser than paintYAxis's fixed 3 lines -- scanning
// how far left in time/samples a feature sits benefits from finer-grained
// lines than the min/mid/max a vertical scan needs.
constexpr int kXGridTargetSpacingPx = 60;
constexpr int kXGridMinLines = 4;
constexpr int kXGridMaxLines = 10;

// Vertical gridlines only -- the "t"/"k" independent-variable label used to
// get its own strip here, but now rides on the bottom value-legend row
// instead (see paintSeriesLegends()).
void paintXAxis(QPainter& painter, const QRect& plotRect, bool showGrid, const ThemePalette& palette) {
    if (!showGrid || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const int lineCount = qBound(kXGridMinLines, plotRect.width() / kXGridTargetSpacingPx, kXGridMaxLines);
    painter.setPen(QPen(palette.border, 1));
    for (int i = 1; i < lineCount; ++i) {
        const int x = plotRect.left() + plotRect.width() * i / lineCount;
        painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
    }
}

// One legend row's height: the taller of the color swatch and the current
// font's line height. Shared by plotTopMargin()/plotBottomMargin() (to
// reserve exactly this much space) and paintSeriesLegends() (to actually
// draw into it), so the reserved margin and the drawn row can never drift
// apart.
int legendRowHeight(const QPainter& painter) {
    return qMax(kLegendSwatchSize, QFontMetrics(painter.font()).height());
}

QString seriesDisplayName(const ChartSeriesConfig& series) {
    return series.name.isEmpty() ? QString("Field %1").arg(series.fieldId) : series.name;
}

// A series buffer's latest sample, formatted like a Y-axis value label (see
// paintYAxis's drawValue) so the two read consistently. "--" for a series
// with no data yet rather than 0 -- an absent reading and an actual zero
// reading need to look different.
QString formatLatestValue(const QVector<double>& buffer) {
    if (buffer.isEmpty()) {
        return QStringLiteral("--");
    }
    return QString::number(buffer.last(), 'g', 4);
}

struct LegendColumn {
    int x = 0;
    int width = 0; // shared width -- see legendColumns()
};

// Column x-positions shared by both rows paintSeriesLegends() draws (series
// names above the plot, latest values below) so their color swatches always
// line up vertically. Every column gets the *same* width -- the widest
// name/value text across all series, not just its own -- so the gap between
// swatches reads as one consistent grid instead of each column snugly
// hugging its own (differently sized) text. Stops adding columns once one
// would cross `rightBound`, same "just stop, don't wrap/elide" overflow
// policy the legend has always used.
QVector<LegendColumn> legendColumns(const QFontMetrics& fm, int left, int rightBound,
                                     const QVector<ChartSeriesConfig>& seriesConfigs, const QStringList& values) {
    int columnWidth = 0;
    for (int i = 0; i < seriesConfigs.size(); ++i) {
        const int nameWidth = fm.horizontalAdvance(seriesDisplayName(seriesConfigs[i]));
        const int valueWidth = i < values.size() ? fm.horizontalAdvance(values[i]) : 0;
        columnWidth = qMax(columnWidth, kLegendSwatchSize + 4 + qMax(nameWidth, valueWidth));
    }

    QVector<LegendColumn> columns;
    int x = left;
    for (int i = 0; i < seriesConfigs.size(); ++i) {
        if (x + columnWidth > rightBound) {
            break;
        }
        columns.append({x, columnWidth});
        x += columnWidth + kLegendItemGap;
    }
    return columns;
}

void paintLegendRow(QPainter& painter, int y, int rowHeight, const QVector<LegendColumn>& columns,
                     const QVector<ChartSeriesConfig>& seriesConfigs, const QStringList& texts,
                     const ThemePalette& palette) {
    for (int i = 0; i < columns.size(); ++i) {
        const int x = columns[i].x;
        painter.setPen(Qt::NoPen);
        painter.setBrush(seriesConfigs[i].color);
        painter.drawEllipse(QRect(x, y + (rowHeight - kLegendSwatchSize) / 2, kLegendSwatchSize, kLegendSwatchSize));

        painter.setPen(palette.textSecondary);
        const int textX = x + kLegendSwatchSize + 4;
        painter.drawText(QRect(textX, y, columns[i].width - (kLegendSwatchSize + 4), rowHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, texts[i]);
    }
}

// Two legend rows bracketing the plot: series names above, each series'
// latest value directly below it at the bottom -- same color swatch and
// same column position in both (see legendColumns()), so following a swatch
// straight down reads as "this name -> this value" without having to match
// colors by eye. The "t"/"k" independent-variable label rides on the bottom
// row's right edge, past the last value column.
void paintSeriesLegends(QPainter& painter, const QRect& area, const QVector<ChartSeriesConfig>& seriesConfigs,
                         const QVector<QVector<double>>& seriesBuffers, ChartXAxisMode xAxisMode,
                         const ThemePalette& palette) {
    const QFontMetrics fm(painter.font());
    const int rowHeight = legendRowHeight(painter);
    const QString xLabel = xAxisMode == ChartXAxisMode::Time ? QStringLiteral("t") : QStringLiteral("k");
    const int xLabelWidth = fm.horizontalAdvance(xLabel);

    // The bottom row's right edge yields to the "t"/"k" tag; the top row has
    // no competing element there, but shares the same rightBound anyway so
    // both rows always show the identical set of series -- otherwise a
    // series that fits in the (slightly wider) name row but not the value
    // row would show a name with no value underneath it.
    const int rightBound = area.right() - kOuterPadding - xLabelWidth - kAxisLabelGap;
    const int left = area.left() + kOuterPadding;

    QStringList names;
    QStringList values;
    for (int i = 0; i < seriesConfigs.size(); ++i) {
        names << seriesDisplayName(seriesConfigs[i]);
        values << formatLatestValue(i < seriesBuffers.size() ? seriesBuffers[i] : QVector<double>());
    }

    const QVector<LegendColumn> columns = legendColumns(fm, left, rightBound, seriesConfigs, values);

    const int topY = area.top() + kOuterPadding;
    paintLegendRow(painter, topY, rowHeight, columns, seriesConfigs, names, palette);

    const int bottomY = area.bottom() - kOuterPadding - rowHeight + 1;
    paintLegendRow(painter, bottomY, rowHeight, columns, seriesConfigs, values, palette);

    painter.setPen(palette.textSecondary);
    const QRect xLabelRect(area.right() - kOuterPadding - xLabelWidth, bottomY, xLabelWidth, rowHeight);
    painter.drawText(xLabelRect, Qt::AlignLeft | Qt::AlignVCenter, xLabel);
}

// Top inset for plotRect: just kOuterPadding, matching the right inset, when
// there's no legend to draw; otherwise enough to clear paintSeriesLegends()'s
// actual row height (which varies with font metrics) plus the same margin
// below it. Previously a flat kLabelMargin * 3 guess -- too much empty space
// above the plot when a chart has no series yet, and not necessarily enough
// to clear a taller legend row, which read as the plot floating unevenly
// inside its widget.
int plotTopMargin(const QPainter& painter, bool hasLegend) {
    if (!hasLegend) {
        return kOuterPadding;
    }
    return kOuterPadding + legendRowHeight(painter) + kOuterPadding;
}

// Left inset for plotRect: kOuterPadding from the widget's own edge, then
// -- when a unit is configured -- the rotated unit strip and a tight
// kAxisLabelGap ahead of the value gutter, then the gutter itself (sized to
// the actual min/mid/max labels for this frame's range, see
// axisLabelWidth()) and another kAxisLabelGap before the plot starts.
// Mirrors the strip/gutter layout paintYAxis() draws into, so plotRect
// always reserves exactly as much space as that call will actually use --
// no more, no less, regardless of how many digits the current range needs.
int plotLeftMargin(const QPainter& painter, bool hasUnit, double yMin, double yMax) {
    const int unitPart = hasUnit ? unitStripWidth(painter) + kAxisLabelGap : 0;
    return kOuterPadding + unitPart + axisLabelWidth(painter, yMin, yMax) + kAxisLabelGap;
}

// Bottom inset for plotRect: mirrors plotTopMargin()'s legend-row math, but
// always reserved (no hasLegend gate) -- paintSeriesLegends()'s "t"/"k" tag
// rides on this row and stays up even with zero series configured, same as
// paintYAxis's value labels staying up regardless of `showGrid`.
int plotBottomMargin(const QPainter& painter) {
    return kOuterPadding + legendRowHeight(painter) + kOuterPadding;
}

Qt::PenStyle qtPenStyleFor(ChartSeriesStyle style) {
    switch (style) {
        case ChartSeriesStyle::Dashed:
            return Qt::DashLine;
        case ChartSeriesStyle::Dotted:
            return Qt::DotLine;
        case ChartSeriesStyle::DashDot:
            return Qt::DashDotLine;
        default:
            return Qt::SolidLine;
    }
}

bool isMarkerStyle(ChartSeriesStyle style) {
    return style == ChartSeriesStyle::Cross || style == ChartSeriesStyle::Asterisk;
}

// Y range to map buffered values into plotRect's height: the configured
// fixed range, or an auto range spanning whatever's currently buffered
// (with a little headroom so extreme points don't touch the plot edges).
QPair<double, double> computeYRange(const ChartConfig& config, const QVector<QVector<double>>& buffers) {
    if (config.yAxisMode == ChartYAxisMode::Fixed) {
        return {config.yMin, qMax(config.yMax, config.yMin + 1e-6)};
    }

    bool any = false;
    double lo = 0.0;
    double hi = 0.0;
    for (const QVector<double>& buffer : buffers) {
        for (double value : buffer) {
            if (!any) {
                lo = hi = value;
                any = true;
            } else {
                lo = qMin(lo, value);
                hi = qMax(hi, value);
            }
        }
    }
    if (!any) {
        return {0.0, 1.0};
    }
    if (qFuzzyCompare(lo, hi)) {
        return {lo - 1.0, hi + 1.0};
    }
    const double pad = (hi - lo) * 0.05;
    return {lo - pad, hi + pad};
}

// Pixel step between adjacent samples, sized against the series' full
// capacity (not however many samples are buffered yet) so points always
// anchor to the right edge and scroll left as new data arrives, rather than
// rescaling every time a not-yet-full buffer grows.
qreal xStepFor(const QRect& plotRect, int capacity) {
    return capacity > 1 ? qreal(plotRect.width()) / qreal(capacity - 1) : 0.0;
}

void paintLineSeries(QPainter& painter, const QRect& plotRect, int capacity, const ChartSeriesConfig& seriesConfig,
                      const QVector<double>& values, double yMin, double yMax) {
    if (values.isEmpty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal step = xStepFor(plotRect, capacity);

    auto pointAt = [&](int i) -> QPointF {
        const qreal x = plotRect.right() - step * (values.size() - 1 - i);
        const qreal t = (values[i] - yMin) / yRange;
        const qreal y = plotRect.bottom() - plotRect.height() * t;
        return QPointF(x, y);
    };

    if (isMarkerStyle(seriesConfig.style)) {
        painter.setPen(QPen(seriesConfig.color, 2));
        constexpr qreal kMarkerSize = 4.0;
        for (int i = 0; i < values.size(); ++i) {
            const QPointF p = pointAt(i);
            painter.drawLine(QPointF(p.x() - kMarkerSize, p.y()), QPointF(p.x() + kMarkerSize, p.y()));
            painter.drawLine(QPointF(p.x(), p.y() - kMarkerSize), QPointF(p.x(), p.y() + kMarkerSize));
            if (seriesConfig.style == ChartSeriesStyle::Asterisk) {
                const qreal d = kMarkerSize * 0.7;
                painter.drawLine(QPointF(p.x() - d, p.y() - d), QPointF(p.x() + d, p.y() + d));
                painter.drawLine(QPointF(p.x() - d, p.y() + d), QPointF(p.x() + d, p.y() - d));
            }
        }
        return;
    }

    QPainterPath path;
    for (int i = 0; i < values.size(); ++i) {
        const QPointF p = pointAt(i);
        if (i == 0) {
            path.moveTo(p);
        } else {
            path.lineTo(p);
        }
    }
    painter.setPen(QPen(seriesConfig.color, 2, qtPenStyleFor(seriesConfig.style)));
    painter.drawPath(path);
}

void paintBarSeries(QPainter& painter, const QRect& plotRect, int capacity,
                     const QVector<ChartSeriesConfig>& seriesConfigs, const QVector<QVector<double>>& buffers,
                     double yMin, double yMax) {
    if (capacity <= 0 || seriesConfigs.isEmpty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal rawStep = xStepFor(plotRect, capacity);
    const qreal step = rawStep > 0 ? rawStep : qreal(plotRect.width()) / qreal(qMax(1, capacity));
    constexpr qreal kGroupGapFraction = 0.15;
    const qreal groupWidth = step * (1.0 - kGroupGapFraction);
    const qreal barWidth = qMax(1.0, groupWidth / seriesConfigs.size());
    const qreal zeroT = qBound(0.0, (0.0 - yMin) / yRange, 1.0);
    const qreal zeroY = plotRect.bottom() - plotRect.height() * zeroT;

    painter.setPen(Qt::NoPen);
    for (int series = 0; series < seriesConfigs.size() && series < buffers.size(); ++series) {
        const QVector<double>& values = buffers[series];
        for (int i = 0; i < values.size(); ++i) {
            const int distanceFromNewest = values.size() - 1 - i;
            if (distanceFromNewest >= capacity) {
                continue;
            }
            const qreal groupRight = plotRect.right() - step * distanceFromNewest;
            const qreal x = groupRight - groupWidth + series * barWidth;
            const qreal t = qBound(0.0, (values[i] - yMin) / yRange, 1.0);
            const qreal barTop = plotRect.bottom() - plotRect.height() * t;

            painter.setBrush(seriesConfigs[series].color);
            painter.drawRect(QRectF(x, qMin(barTop, zeroY), barWidth, qAbs(barTop - zeroY)));
        }
    }
}

} // namespace

void ChartWidgetBase::setConfig(const QJsonObject& config) {
    const ChartConfig newConfig = parseChartConfig(config);
    m_seriesBuffers = resizeChartBuffers(m_seriesBuffers, newConfig);
    m_config = newConfig;
    update();
}

void ChartWidgetBase::appendFieldSample(quint16 fieldId, quint64 timestampUs, double value) {
    traceview::appendFieldSample(m_seriesBuffers, m_config, fieldId, timestampUs, value);
    scheduleRepaint();
}

void ChartWidgetBase::onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                                     double value) {
    if (binding.sourceId != m_config.sourceId || binding.topicId != m_config.topicId) {
        return;
    }
    appendFieldSample(binding.fieldId, timestampUs, value);
}

void ChartWidgetBase::scheduleRepaint() {
    if (m_repaintPending) {
        return;
    }
    m_repaintPending = true;
    QTimer::singleShot(kRepaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

DummyLineChartWidget::DummyLineChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

void DummyLineChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    QVector<QVector<double>> seriesValues;
    seriesValues.reserve(m_seriesBuffers.size());
    for (const TelemetrySeriesBuffer& buffer : m_seriesBuffers) {
        seriesValues.append(buffer.values());
    }

    const auto [yMin, yMax] = computeYRange(m_config, seriesValues);
    const int topMargin = plotTopMargin(painter, !m_config.series.isEmpty());
    const int leftMargin = plotLeftMargin(painter, !m_config.yUnit.isEmpty(), yMin, yMax);
    const int bottomMargin = plotBottomMargin(painter);
    const QRect plotRect = area.adjusted(leftMargin, topMargin, -kOuterPadding, -bottomMargin);
    const int capacity = chartBufferCapacity(m_config);

    paintYAxis(painter, plotRect, yMin, yMax, m_config.yUnit, m_config.showGrid, palette);
    paintXAxis(painter, plotRect, m_config.showGrid, palette);
    for (int i = 0; i < m_config.series.size() && i < seriesValues.size(); ++i) {
        paintLineSeries(painter, plotRect, capacity, m_config.series[i], seriesValues[i], yMin, yMax);
    }

    // No more redundant "Line Chart" corner label -- DashboardCell's header
    // already shows the configured name (TAREFA 1); this corner now carries
    // the per-series legend instead, which the old literal text had no room
    // for anyway.
    paintSeriesLegends(painter, area, m_config.series, seriesValues, m_config.xAxisMode, palette);
}

DummyBarChartWidget::DummyBarChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

void DummyBarChartWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    QVector<QVector<double>> seriesValues;
    seriesValues.reserve(m_seriesBuffers.size());
    for (const TelemetrySeriesBuffer& buffer : m_seriesBuffers) {
        seriesValues.append(buffer.values());
    }

    const auto [yMin, yMax] = computeYRange(m_config, seriesValues);
    const int topMargin = plotTopMargin(painter, !m_config.series.isEmpty());
    const int leftMargin = plotLeftMargin(painter, !m_config.yUnit.isEmpty(), yMin, yMax);
    const int bottomMargin = plotBottomMargin(painter);
    const QRect plotRect = area.adjusted(leftMargin, topMargin, -kOuterPadding, -bottomMargin);
    const int capacity = chartBufferCapacity(m_config);

    paintYAxis(painter, plotRect, yMin, yMax, m_config.yUnit, m_config.showGrid, palette);
    paintXAxis(painter, plotRect, m_config.showGrid, palette);
    paintBarSeries(painter, plotRect, capacity, m_config.series, seriesValues, yMin, yMax);

    // See DummyLineChartWidget::paintEvent for why this is a legend instead
    // of a "Bar Chart" corner label now.
    paintSeriesLegends(painter, area, m_config.series, seriesValues, m_config.xAxisMode, palette);
}

DummyGaugeWidget::DummyGaugeWidget(QWidget* parent) : DashboardWidget(parent) {}

void DummyGaugeWidget::setConfig(const QJsonObject& config) {
    m_config = parseGaugeConfig(config);
    update();
}

void DummyGaugeWidget::appendFieldSample(quint16 fieldId, quint64 timestampUs, double value) {
    Q_UNUSED(timestampUs);  // a gauge only ever shows the current value
    if (fieldId != m_config.fieldId) {
        return;
    }
    m_value = value;
    scheduleRepaint();
}

void DummyGaugeWidget::onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                                      double value) {
    if (binding.sourceId != m_config.sourceId || binding.topicId != m_config.topicId) {
        return;
    }
    appendFieldSample(binding.fieldId, timestampUs, value);
}

void DummyGaugeWidget::scheduleRepaint() {
    if (m_repaintPending) {
        return;
    }
    m_repaintPending = true;
    QTimer::singleShot(kRepaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

void DummyGaugeWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    // No more redundant "Gauge" corner label -- DashboardCell's header
    // already shows the configured name (TAREFA 1), same reasoning as the
    // line/bar charts in TAREFA 3. Unlike those, a gauge has no per-series
    // legend to put in its place, so the reclaimed margin just goes back to
    // the arc instead of being pinned at kOuterPadding * 3.
    const QRect plotRect = area.adjusted(kOuterPadding, kOuterPadding, -kOuterPadding, -kOuterPadding);
    const int side = qMin(plotRect.width(), plotRect.height());
    if (side > 0) {
        const bool hasValue = !qIsNaN(m_value);
        const double range = m_config.max - m_config.min;
        const double fraction = hasValue && range != 0.0 ? qBound(0.0, (m_value - m_config.min) / range, 1.0) : 0.0;

        const QRect arcRect(plotRect.center().x() - side / 2, plotRect.top(), side, side);
        constexpr int kStartAngle = 90 * 16;   // 12 o'clock, Qt angles are in 1/16th degrees
        constexpr int kSpanAngle = -270 * 16;  // sweep 270 degrees clockwise

        QPen trackPen(palette.surfaceAlt, 8, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawArc(arcRect.adjusted(4, 4, -4, -4), kStartAngle, kSpanAngle);

        if (hasValue) {
            QPen valuePen(palette.accent, 8, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(valuePen);
            painter.drawArc(arcRect.adjusted(4, 4, -4, -4), kStartAngle, qRound(kSpanAngle * fraction));
        }

        // The single most important number on this widget -- larger, bold,
        // the one deliberate typography accent in the app (see "Typography"
        // in docs/VISUAL_IDENTITY.md); every other label stays system
        // default weight/size.
        QFont valueFont = painter.font();
        valueFont.setPointSize(valueFont.pointSize() + 6);
        valueFont.setBold(true);
        painter.setFont(valueFont);
        painter.setPen(palette.textPrimary);
        const QString text =
            hasValue ? QString("%1%2").arg(m_value, 0, 'f', m_config.decimals).arg(m_config.unit) : "--";
        painter.drawText(arcRect, Qt::AlignCenter, text);
    }
}

} // namespace traceview
