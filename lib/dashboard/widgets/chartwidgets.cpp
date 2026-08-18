#include "chartwidgets.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QTimer>
#include <QtMath>

#include "dashboard/paintframecounter.h"
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

// Line chart's Y grid: just min/mid/max (see paintYAxis()'s `gridDivisions`
// param) -- a busier grid would fight the throttled, frequently-redrawn plot
// lines for attention on a widget this small.
constexpr int kLineYGridDivisions = 2;
// Bar chart's Y grid: one line every 10% of the configured range (11 lines,
// 10 bands) -- unlike the line chart, snapshot bars have nothing else
// competing for attention in the plot area, and reading a bar's height off a
// fine-grained ruler is the whole point of the fixed 0-100%-style range these
// are normally configured with.
constexpr int kBarYGridDivisions = 10;

// `gridDivisions` evenly spaced horizontal gridlines across plotRect's
// height (gridDivisions + 1 lines, including the top/bottom edges), plus the
// min/mid/max value labels in a gutter to plotRect's left sized to the
// widest of the three (see axisLabelWidth()) -- always exactly 3 labels
// regardless of how many gridlines are drawn, so a denser bar-chart grid
// doesn't turn into a wall of overlapping numbers. The value labels (and
// unit) stay up regardless of `showGrid` -- that toggle is about the guide
// lines across the plot, not about losing the ability to read the axis.
void paintYAxis(QPainter& painter, const QRect& plotRect, double yMin, double yMax, const QString& unit,
                 bool showGrid, int gridDivisions, int labelWidth, const ThemePalette& palette) {
    if (plotRect.height() <= 0 || plotRect.width() <= 0) {
        return;
    }
    const int midY = plotRect.center().y();

    if (showGrid) {
        painter.setPen(QPen(palette.border, 1));
        for (int i = 0; i <= gridDivisions; ++i) {
            const int y = plotRect.bottom() - plotRect.height() * i / gridDivisions;
            painter.drawLine(plotRect.left(), y, plotRect.right(), y);
        }
    }

    painter.setPen(palette.textSecondary);
    const QFontMetrics fm(painter.font());
    // Derived from plotRect itself (not a separate running x) so this stays
    // correct regardless of whether the caller reserved extra left space
    // for the rotated unit strip below. `labelWidth` comes from the caller
    // (axisLabelWidth(), same yMin/yMax) instead of being recomputed here --
    // plotLeftMargin() already needs that exact value to size the gutter
    // this paints into, so computing it twice per frame was pure duplicate
    // work for an identical result.
    const int gutterRight = plotRect.left() - kAxisLabelGap;
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

// Pixel X positions of the vertical gridlines paintXAxis() draws, spaced by
// kXGridTargetSpacingPx the same way paintXAxis() itself used to compute
// them inline. Pulled out so paintGridPointMarkers() below can drop a marker
// at exactly the same X a gridline is (or would be) drawn at -- computing the
// spacing twice risked the two drifting apart by a rounding difference.
QVector<int> xGridLines(const QRect& plotRect) {
    QVector<int> lines;
    if (plotRect.width() <= 0 || plotRect.height() <= 0) {
        return lines;
    }
    const int lineCount = qBound(kXGridMinLines, plotRect.width() / kXGridTargetSpacingPx, kXGridMaxLines);
    lines.reserve(lineCount - 1);
    for (int i = 1; i < lineCount; ++i) {
        lines.append(plotRect.left() + plotRect.width() * i / lineCount);
    }
    return lines;
}

// Vertical gridlines only -- the "t"/"k" independent-variable label used to
// get its own strip here, but now rides on the bottom value-legend row
// instead (see paintSeriesLegends()).
void paintXAxis(QPainter& painter, const QRect& plotRect, const QVector<int>& xLines, bool showGrid,
                 const ThemePalette& palette) {
    if (!showGrid) {
        return;
    }
    painter.setPen(QPen(palette.border, 1));
    for (int x : xLines) {
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

// Free function (file-scope, not a member of any QObject-derived class) --
// tr() isn't callable here, so this and gaugeSeriesDisplayName() below use
// QCoreApplication::translate() with an explicit context instead, same idiom
// as the earlier ThemePalette/WidgetRegistry/ProjectStore fixes. Every other
// user-facing string in this file lives in an actual class member function
// (ChartWidgetBase/DummyGaugeWidget/etc.) and uses plain tr() as usual.
QString seriesDisplayName(const ChartSeriesConfig& series) {
    return series.name.isEmpty() ? QCoreApplication::translate("ChartWidgets", "Field %1").arg(series.fieldId)
                                  : series.name;
}

// A series buffer's latest sample, formatted like a Y-axis value label (see
// paintYAxis's drawValue) so the two read consistently. "--" for a series
// with no data yet rather than 0 -- an absent reading and an actual zero
// reading need to look different.
QString formatLatestValue(const QVector<double>& buffer) {
    if (buffer.isEmpty()) {
        // Free function -- see seriesDisplayName() above for why this uses
        // QCoreApplication::translate() instead of tr().
        return QCoreApplication::translate("ChartWidgets", "--");
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

// `hiddenSeries[i]` (ChartWidgetBase::m_seriesHidden, toggled by clicking the
// legend -- see mousePressEvent() and legend hit-rects below) dims that
// series' swatch+text and flattens the swatch to a neutral gray instead of
// its own color, so a hidden series still shows its name (the click target
// to bring it back) but reads as "off" at a glance.
void paintLegendRow(QPainter& painter, int y, int rowHeight, const QVector<LegendColumn>& columns,
                     const QVector<ChartSeriesConfig>& seriesConfigs, const QStringList& texts,
                     const QVector<bool>& hiddenSeries, const ThemePalette& palette) {
    for (int i = 0; i < columns.size(); ++i) {
        const int x = columns[i].x;
        const bool hidden = i < hiddenSeries.size() && hiddenSeries[i];
        painter.setOpacity(hidden ? 0.4 : 1.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(hidden ? palette.textSecondary : seriesConfigs[i].color);
        painter.drawEllipse(QRect(x, y + (rowHeight - kLegendSwatchSize) / 2, kLegendSwatchSize, kLegendSwatchSize));

        painter.setPen(palette.textSecondary);
        const int textX = x + kLegendSwatchSize + 4;
        painter.drawText(QRect(textX, y, columns[i].width - (kLegendSwatchSize + 4), rowHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, texts[i]);
    }
    painter.setOpacity(1.0);
}

// Two legend rows bracketing the plot: series names above, each series'
// latest value directly below it at the bottom -- same color swatch and
// same column position in both (see legendColumns()), so following a swatch
// straight down reads as "this name -> this value" without having to match
// colors by eye. The "t"/"k" independent-variable label rides on the bottom
// row's right edge, past the last value column.
// `showLastValueRow` gates only the bottom row's swatch+value draw (the
// header gear's "Show last value" toggle, ChartWidgetBase::showsLastValueRow())
// -- the top name legend and the "t"/"k" axis tag stay up regardless, and
// plotBottomMargin() keeps reserving the same space either way, so toggling
// this never moves/resizes the plot.
// `hiddenSeries` grays out a toggled-off series in both rows (see
// paintLegendRow()) rather than dropping it from the legend entirely -- its
// name/swatch needs to stay clickable so the same click can bring it back.
// `outHitRects`, when non-null, is filled with one clickable rect per series
// (empty QRect() for any series that didn't fit within rightBound) spanning
// from the top row down through whichever rows are actually drawn --
// ChartWidgetBase::mousePressEvent() hit-tests against exactly this so the
// click target can never drift from what got painted.
void paintSeriesLegends(QPainter& painter, const QRect& area, const QVector<ChartSeriesConfig>& seriesConfigs,
                         const QVector<QVector<double>>& seriesBuffers, ChartXAxisMode xAxisMode,
                         bool showLastValueRow, bool showXAxisTag, const QVector<bool>& hiddenSeries,
                         const ThemePalette& palette, QVector<QRect>* outHitRects = nullptr) {
    const QFontMetrics fm(painter.font());
    const int rowHeight = legendRowHeight(painter);
    // Free function -- see seriesDisplayName() above for why this uses
    // QCoreApplication::translate() instead of tr().
    const QString xLabel = xAxisMode == ChartXAxisMode::Time ? QCoreApplication::translate("ChartWidgets", "t")
                                                              : QCoreApplication::translate("ChartWidgets", "k");
    // Only reserved/drawn for the line chart (showXAxisTag) -- the bar
    // chart's X axis is now one value label per bar (paintBarSnapshot()),
    // not a scrolling sample/time count, so the "t"/"k" tag has nothing left
    // to refer to there.
    const int xLabelWidth = showXAxisTag ? fm.horizontalAdvance(xLabel) : 0;

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
    const int bottomY = area.bottom() - kOuterPadding - rowHeight + 1;

    if (outHitRects) {
        outHitRects->clear();
        outHitRects->reserve(seriesConfigs.size());
        const int hitBottom = (showLastValueRow ? bottomY : topY) + rowHeight;
        for (int i = 0; i < seriesConfigs.size(); ++i) {
            outHitRects->append(i < columns.size() ? QRect(columns[i].x, topY, columns[i].width, hitBottom - topY)
                                                     : QRect());
        }
    }

    paintLegendRow(painter, topY, rowHeight, columns, seriesConfigs, names, hiddenSeries, palette);

    if (showLastValueRow) {
        paintLegendRow(painter, bottomY, rowHeight, columns, seriesConfigs, values, hiddenSeries, palette);
    }

    if (showXAxisTag) {
        painter.setPen(palette.textSecondary);
        const QRect xLabelRect(area.right() - kOuterPadding - xLabelWidth, bottomY, xLabelWidth, rowHeight);
        painter.drawText(xLabelRect, Qt::AlignLeft | Qt::AlignVCenter, xLabel);
    }
}

// Top inset for plotRect: mirrors plotBottomMargin()'s legend-row math, and
// like it is always reserved (no hasLegend gate) -- a chart with no series
// configured yet still gets this row's worth of space, so the plot doesn't
// jump up and resize the moment the first series is added. Previously a flat
// kLabelMargin * 3 guess -- too much empty space above the plot when a chart
// has no series yet, and not necessarily enough to clear a taller legend
// row, which read as the plot floating unevenly inside its widget.
int plotTopMargin(const QPainter& painter) {
    return kOuterPadding + legendRowHeight(painter) + kOuterPadding;
}

// Left inset for plotRect: kOuterPadding from the widget's own edge, then
// -- when a unit is configured -- the rotated unit strip and a tight
// kAxisLabelGap ahead of the value gutter, then the gutter itself (sized to
// `labelWidth`, the caller's own axisLabelWidth() for this frame's range)
// and another kAxisLabelGap before the plot starts. Mirrors the strip/gutter
// layout paintYAxis() draws into, so plotRect always reserves exactly as
// much space as that call will actually use -- no more, no less, regardless
// of how many digits the current range needs. Takes `labelWidth` instead of
// yMin/yMax directly so the caller can compute it once and hand the same
// value to both this and paintYAxis() rather than each deriving its own.
int plotLeftMargin(const QPainter& painter, bool hasUnit, int labelWidth) {
    const int unitPart = hasUnit ? unitStripWidth(painter) + kAxisLabelGap : 0;
    return kOuterPadding + unitPart + labelWidth + kAxisLabelGap;
}

// Bottom inset for plotRect: mirrors plotTopMargin()'s legend-row math, but
// always reserved (no hasLegend gate) -- paintSeriesLegends()'s "t"/"k" tag
// rides on this row and stays up even with zero series configured, same as
// paintYAxis's value labels staying up regardless of `showGrid`.
int plotBottomMargin(const QPainter& painter) {
    return kOuterPadding + legendRowHeight(painter) + kOuterPadding;
}

// --- Gauge geometry ---------------------------------------------------
//
// A gauge draws one ring per configured series, all sharing the same 270
// degree sweep (12 o'clock clockwise to 9 o'clock, leaving a 90 degree gap
// at the bottom) but at successively smaller radii -- ring 0 outermost,
// each following ring nested inside it. See DummyGaugeWidget::paintEvent().

// 12 o'clock, sweeping 270 degrees clockwise -- in plain degrees (for the
// trig in pointOnGaugeArc()) and in Qt's 1/16th-degree drawArc()/drawPie()
// units (for the arcs themselves). Both pairs describe the same sweep; kept
// as separate constants only because the two APIs want different units.
constexpr double kGaugeStartAngleDeg = 90.0;
constexpr double kGaugeSpanAngleDeg = -270.0;
constexpr int kGaugeQtStartAngle = 90 * 16;
constexpr int kGaugeQtSpanAngle = -270 * 16;

// Ring pen width caps out at 8px (matching the original single-ring gauge)
// when there's room; kGaugeRingGap is the visible gap this leaves between
// two adjacent ring strokes at that width. kGaugeDefaultRingPitch is the
// center-to-center spacing used whenever there's enough outer radius to
// afford it; gaugeRingPitch() shrinks it (down to kGaugeMinRingPitch) only
// once enough rings are configured that they'd otherwise run past
// kGaugeMinInnerRadius.
constexpr double kGaugeMaxRingPenWidth = 8.0;
constexpr double kGaugeRingGap = 3.0;
constexpr double kGaugeDefaultRingPitch = 16.0;
constexpr double kGaugeMinRingPitch = 6.0;
constexpr double kGaugeMinInnerRadius = 18.0;

// Center-to-center spacing between successive rings. A single ring ignores
// this for radius (there's nothing to space it from) but still uses it to
// derive its own pen width -- see the penWidth computation in paintEvent().
double gaugeRingPitch(double outerRadius, int ringCount) {
    if (ringCount <= 1) {
        return kGaugeDefaultRingPitch;
    }
    const double available = qMax(0.0, outerRadius - kGaugeMinInnerRadius);
    return qBound(kGaugeMinRingPitch, available / (ringCount - 1), kGaugeDefaultRingPitch);
}

// A point at `fraction` along the gauge's sweep (0 = start/12 o'clock, 1 =
// end/9 o'clock the long way around), `radius` out from `center`. Shared by
// the ruler ticks and the value pointer below -- both are just a line
// between two radii at the same fraction.
QPointF pointOnGaugeArc(const QPointF& center, double radius, double fraction) {
    const double angleRad = qDegreesToRadians(kGaugeStartAngleDeg + kGaugeSpanAngleDeg * fraction);
    return QPointF(center.x() + radius * qCos(angleRad), center.y() - radius * qSin(angleRad));
}

void paintGaugeRadialTick(QPainter& painter, const QPointF& center, double fraction, double innerRadius,
                           double outerRadius) {
    painter.drawLine(pointOnGaugeArc(center, innerRadius, fraction), pointOnGaugeArc(center, outerRadius, fraction));
}

// One graduation mark every 10% of the configured range, just outside the
// ring's track -- a curved ruler around the arc so the eye has a scale to
// read the fill against, not just the bare filled/unfilled split.
constexpr int kGaugeTickDivisions = 10;
constexpr double kGaugeTickLength = 4.0;

void paintGaugeTicks(QPainter& painter, const QPointF& center, double ringRadius, double penWidth,
                      const QColor& color) {
    painter.setPen(QPen(color, 1));
    const double outer = ringRadius + penWidth / 2.0;
    for (int i = 0; i <= kGaugeTickDivisions; ++i) {
        paintGaugeRadialTick(painter, center, double(i) / kGaugeTickDivisions, outer, outer + kGaugeTickLength);
    }
}

// How far the pointer pokes past each edge of its ring -- long enough to
// read as a needle tip crossing the track, not just another (slightly
// thicker) tick.
constexpr double kGaugePointerOvershoot = 4.0;

// The exact-value marker: a short, bright line crossing the ring right at
// the current value's angle -- distinct from the filled value arc (which
// shows magnitude via its sweep length, hard to judge precisely by eye) and
// from the ruler ticks above (which mark the scale, not the reading).
void paintGaugePointer(QPainter& painter, const QPointF& center, double ringRadius, double penWidth,
                        double fraction, const QColor& color) {
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
    const double inner = ringRadius - penWidth / 2.0 - kGaugePointerOvershoot;
    const double outer = ringRadius + penWidth / 2.0 + kGaugePointerOvershoot;
    paintGaugeRadialTick(painter, center, fraction, inner, outer);
}

// Free function -- see seriesDisplayName() above for why this uses
// QCoreApplication::translate() instead of tr().
QString gaugeSeriesDisplayName(const GaugeSeriesConfig& series) {
    return series.name.isEmpty() ? QCoreApplication::translate("ChartWidgets", "Field %1").arg(series.fieldId)
                                  : series.name;
}

// Column x-positions for paintGaugeLegend()'s single row -- same "widest
// item sets every column's width, stop once one would cross rightBound"
// policy as legendColumns() (line/bar charts), just against one
// pre-formatted "name  value" string per ring instead of separate name/value
// rows, since a gauge ring has no second row to keep aligned with.
QVector<LegendColumn> gaugeLegendColumns(const QFontMetrics& fm, int left, int rightBound, const QStringList& texts) {
    int columnWidth = 0;
    for (const QString& text : texts) {
        columnWidth = qMax(columnWidth, kLegendSwatchSize + 6 + fm.horizontalAdvance(text));
    }

    QVector<LegendColumn> columns;
    int x = left;
    for (int i = 0; i < texts.size(); ++i) {
        if (x + columnWidth > rightBound) {
            break;
        }
        columns.append({x, columnWidth});
        x += columnWidth + kLegendItemGap;
    }
    return columns;
}

// Space paintGaugeLegend() below needs: a single legendRowHeight() row, its
// items laid out side by side left to right. Only reserved by paintEvent()
// once there's more than one ring -- see the comment there.
int gaugeLegendHeight(const QPainter& painter) {
    return legendRowHeight(painter);
}

// The name + current value of every ring, side by side in one row above the
// arc -- same layout the line/bar charts' series legend uses (see
// paintSeriesLegends()) -- takes over the "what number is this" job the
// single big centered label handled for a one-ring gauge, since there's no
// room left in the center for more than one such label once rings start
// nesting.
void paintGaugeLegend(QPainter& painter, const QRect& area, const GaugeConfig& config, const QVector<double>& values,
                       const ThemePalette& palette) {
    const QFontMetrics fm(painter.font());
    const int rowHeight = legendRowHeight(painter);

    QStringList texts;
    for (int i = 0; i < config.series.size(); ++i) {
        const double value = i < values.size() ? values[i] : qQNaN();
        // Free function -- see seriesDisplayName() above for why this uses
        // QCoreApplication::translate() instead of tr().
        const QString valueText = qIsNaN(value) ? QCoreApplication::translate("ChartWidgets", "--")
                                                  : QCoreApplication::translate("ChartWidgets", "%1%2")
                                                        .arg(value, 0, 'f', config.decimals)
                                                        .arg(config.unit);
        // "%1  %2" fixes name-before-value order -- a translator wanting to
        // swap that order for a given language would need to reorder these
        // placeholders; not attempted for this pass.
        texts << QCoreApplication::translate("ChartWidgets", "%1  %2")
                     .arg(gaugeSeriesDisplayName(config.series[i]), valueText);
    }

    const QVector<LegendColumn> columns = gaugeLegendColumns(fm, area.left(), area.right(), texts);
    for (int i = 0; i < columns.size(); ++i) {
        const int x = columns[i].x;
        painter.setPen(Qt::NoPen);
        painter.setBrush(config.series[i].color);
        painter.drawEllipse(QRect(x, area.top() + (rowHeight - kLegendSwatchSize) / 2, kLegendSwatchSize,
                                   kLegendSwatchSize));

        painter.setPen(palette.textSecondary);
        const int textX = x + kLegendSwatchSize + 6;
        painter.drawText(QRect(textX, area.top(), columns[i].width - (kLegendSwatchSize + 6), rowHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, texts[i]);
    }
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

// Baseline pixel Y for value 0 -- the anchor Stem interpolation draws each
// sample's lollipop up/down from, and (below) paintBarSeries() anchors each
// bar to. Clamped into plotRect so a fixed Y range that excludes 0 still
// gives Stem/bars a sane, inside-the-plot baseline instead of one computed
// off-screen.
qreal zeroBaselineY(const QRect& plotRect, double yMin, double yMax) {
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal zeroT = qBound(0.0, (0.0 - yMin) / yRange, 1.0);
    return plotRect.bottom() - plotRect.height() * zeroT;
}

// Renders one series' buffered values into plotRect, either as the discrete
// per-sample glyphs a Cross/Asterisk ChartSeriesStyle always draws (unaffected
// by `interpolation` -- those series declare themselves point-only regardless
// of the chart-wide setting) or, for every other style, per `interpolation`
// (ChartWidgetBase::lineInterpolation(), the header gear menu's select box):
// Linear's original straight-segment path, ZeroOrderHold's step/staircase
// path, Stem's per-sample lollipop up from zeroBaselineY(), or None's bare
// per-sample dot with no connecting line/baseline at all.
void paintLineSeries(QPainter& painter, const QRect& plotRect, int capacity, const ChartSeriesConfig& seriesConfig,
                      const QVector<double>& values, double yMin, double yMax,
                      ChartLineInterpolation interpolation) {
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

    if (interpolation == ChartLineInterpolation::Stem) {
        const qreal baselineY = zeroBaselineY(plotRect, yMin, yMax);
        painter.setPen(QPen(seriesConfig.color, 2, qtPenStyleFor(seriesConfig.style)));
        // Invariant across every sample -- was being re-set on every loop
        // iteration for no reason (same QColor each time), just extra
        // QBrush construction + painter state churn on a per-sample hot path.
        painter.setBrush(seriesConfig.color);
        constexpr qreal kDotRadius = 2.5;
        for (int i = 0; i < values.size(); ++i) {
            const QPointF p = pointAt(i);
            painter.drawLine(QPointF(p.x(), baselineY), p);
            painter.drawEllipse(p, kDotRadius, kDotRadius);
        }
        return;
    }

    if (interpolation == ChartLineInterpolation::None) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(seriesConfig.color);
        constexpr qreal kDotRadius = 2.5;
        for (int i = 0; i < values.size(); ++i) {
            painter.drawEllipse(pointAt(i), kDotRadius, kDotRadius);
        }
        return;
    }

    // A plain connect-the-dots polyline (Linear) or a polyline with an extra
    // held point ahead of each sample (ZeroOrderHold's staircase) -- both are
    // straight segments only, so this used to go through QPainterPath purely
    // for its moveTo()/lineTo() convenience. QPainterPath carries a heavier,
    // curve-capable element list (grown one lineTo() at a time, repeatedly
    // reallocating) for generality this shape never uses; a reserved
    // QPolygonF fed straight to drawPolyline() is the more direct API for a
    // segment chain and measurably cheaper for a 100+ point series repainted
    // every frame (see tools/chart_benchmark -- this was the single biggest
    // gap between the line chart's per-frame cost and the bar/gauge widgets',
    // which don't build a path at all).
    QPolygonF points;
    points.reserve(interpolation == ChartLineInterpolation::ZeroOrderHold ? values.size() * 2 - 1 : values.size());
    for (int i = 0; i < values.size(); ++i) {
        const QPointF p = pointAt(i);
        if (i > 0 && interpolation == ChartLineInterpolation::ZeroOrderHold) {
            points.append(QPointF(p.x(), points.last().y()));
        }
        points.append(p);
    }
    painter.setPen(QPen(seriesConfig.color, 2, qtPenStyleFor(seriesConfig.style)));
    painter.drawPolyline(points);
}

// Interpolates a line series' value (and its plot-space Y) at pixel column
// `x`, using the exact same per-sample placement as paintLineSeries()'s own
// pointAt() -- so a marker dropped at some x always lands exactly on the
// line drawn there instead of drifting off by a rounding difference. Returns
// false (leaving *outY/*outValue untouched) where the line doesn't reach yet
// -- short of the oldest buffered sample's x position -- same as
// paintLineSeries() simply not drawing anything out that far.
bool lineValueAtX(const QRect& plotRect, int capacity, const QVector<double>& values, double yMin, double yMax,
                   qreal x, qreal* outY, double* outValue) {
    if (values.size() < 2) {
        return false;
    }
    const qreal step = xStepFor(plotRect, capacity);
    if (step <= 0.0) {
        return false;
    }

    // Inverse of pointAt()'s `x = plotRect.right() - step * (size-1-i)`,
    // solved for the (possibly fractional) sample index i at column x.
    const qreal idx = (values.size() - 1) - (plotRect.right() - x) / step;
    if (idx < 0.0 || idx > values.size() - 1) {
        return false;
    }

    const int i0 = qBound(0, int(qFloor(idx)), values.size() - 1);
    const int i1 = qMin(i0 + 1, values.size() - 1);
    const qreal frac = idx - i0;
    const double value = values[i0] + (values[i1] - values[i0]) * frac;

    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal t = (value - yMin) / yRange;
    *outY = plotRect.bottom() - plotRect.height() * t;
    *outValue = value;
    return true;
}

// Marker-style (Cross/Asterisk) counterpart to lineValueAtX() above: those
// series have no continuous line to interpolate a value from, only discrete
// per-sample glyphs, so this snaps to whichever buffered sample's own pointAt()
// position is nearest `x` instead -- exact value, no interpolation, and
// *outX comes back at that sample's real pixel X (which may sit a little off
// `x`) rather than assuming it lands exactly under the cursor like a line
// series would. Same out-of-range behavior as lineValueAtX() -- false short
// of the oldest buffered sample or past the newest.
bool nearestValueAtX(const QRect& plotRect, int capacity, const QVector<double>& values, double yMin, double yMax,
                      qreal x, qreal* outX, qreal* outY, double* outValue) {
    if (values.isEmpty()) {
        return false;
    }
    const qreal step = xStepFor(plotRect, capacity);
    if (step <= 0.0) {
        return false;
    }

    const qreal idx = (values.size() - 1) - (plotRect.right() - x) / step;
    if (idx < 0.0 || idx > values.size() - 1) {
        return false;
    }

    const int i = qBound(0, qRound(idx), values.size() - 1);
    const double value = values[i];
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal t = (value - yMin) / yRange;
    *outX = plotRect.right() - step * (values.size() - 1 - i);
    *outY = plotRect.bottom() - plotRect.height() * t;
    *outValue = value;
    return true;
}

// ZeroOrderHold counterpart to lineValueAtX(): unlike Linear, a ZOH step
// function IS still well-defined at any `x` between two samples (it's just
// flat there, held at the earlier sample's value -- see paintLineSeries()'s
// own ZeroOrderHold path), so this returns *outX == x unchanged, like
// lineValueAtX(), rather than snapping to a sample position like
// nearestValueAtX(). Same out-of-range behavior as both of those.
bool heldValueAtX(const QRect& plotRect, int capacity, const QVector<double>& values, double yMin, double yMax,
                   qreal x, qreal* outY, double* outValue) {
    if (values.isEmpty()) {
        return false;
    }
    const qreal step = xStepFor(plotRect, capacity);
    if (step <= 0.0) {
        return false;
    }

    const qreal idx = (values.size() - 1) - (plotRect.right() - x) / step;
    if (idx < 0.0 || idx > values.size() - 1) {
        return false;
    }

    const int i0 = qBound(0, int(qFloor(idx)), values.size() - 1);
    const double value = values[i0];
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const qreal t = (value - yMin) / yRange;
    *outY = plotRect.bottom() - plotRect.height() * t;
    *outValue = value;
    return true;
}

// Single entry point paintGridPointMarkers()/paintHoverCrosshair() both
// dispatch through, so what those two draw always matches what
// paintLineSeries() actually rendered at `x` for this series: marker-style
// (Cross/Asterisk) series and Stem/None interpolation (no continuous shape to
// read a value from at an arbitrary `x`) snap to the nearest buffered sample
// via nearestValueAtX(); ZeroOrderHold reads the held value at the exact `x`
// via heldValueAtX(); everything else (Linear) interpolates via
// lineValueAtX(). *outX comes back equal to `x` for Linear/ZeroOrderHold (a
// value exists at that exact column) and at the snapped sample's own position
// otherwise -- callers should always use *outX, not their original `x`, to
// place whatever they draw.
bool valueAtX(ChartLineInterpolation interpolation, bool markerStyle, const QRect& plotRect, int capacity,
              const QVector<double>& values, double yMin, double yMax, qreal x, qreal* outX, qreal* outY,
              double* outValue) {
    if (markerStyle || interpolation == ChartLineInterpolation::Stem || interpolation == ChartLineInterpolation::None) {
        return nearestValueAtX(plotRect, capacity, values, yMin, yMax, x, outX, outY, outValue);
    }
    *outX = x;
    if (interpolation == ChartLineInterpolation::ZeroOrderHold) {
        return heldValueAtX(plotRect, capacity, values, yMin, yMax, x, outY, outValue);
    }
    return lineValueAtX(plotRect, capacity, values, yMin, yMax, x, outY, outValue);
}

// One dot -- plus its value, in a small text pill for legibility over the
// line/gridline it sits on -- everywhere a series crosses one of
// xGridLines()'s vertical gridlines (topico: gear-menu "Show grid point
// values" toggle, ChartWidgetBase::showsGridPointMarkers()). A fixed,
// series-independent color -- not each series' own color -- so the dot reads
// clearly against whatever it's sitting on top of (the line, a gridline,
// another series) rather than blending into a same-colored line. Reads each
// series' value via valueAtX() -- see that function for how marker-style
// series and each `interpolation` mode affect where a "crossing" comes from
// (which may land a little off the gridline's exact X for anything that
// falls back to a nearest-sample snap).
void paintGridPointMarkers(QPainter& painter, const QRect& plotRect, int capacity, const QVector<int>& xLines,
                            const QVector<ChartSeriesConfig>& seriesConfigs, const QVector<QVector<double>>& buffers,
                            double yMin, double yMax, ChartLineInterpolation interpolation,
                            const QVector<bool>& hiddenSeries, const ThemePalette& palette) {
    if (xLines.isEmpty()) {
        return;
    }
    constexpr qreal kDotRadius = 3.0;
    constexpr int kLabelGap = 4;
    constexpr int kLabelPadding = 3;
    const QColor markerColor = palette.textPrimary;
    const QFontMetrics fm(painter.font());
    const int textHeight = fm.height();

    for (int series = 0; series < seriesConfigs.size() && series < buffers.size(); ++series) {
        if (series < hiddenSeries.size() && hiddenSeries[series]) {
            continue;
        }
        const QVector<double>& values = buffers[series];
        const bool marker = isMarkerStyle(seriesConfigs[series].style);
        for (int gridX : xLines) {
            qreal x = 0.0;
            qreal y = 0.0;
            double value = 0.0;
            if (!valueAtX(interpolation, marker, plotRect, capacity, values, yMin, yMax, gridX, &x, &y, &value)) {
                continue;
            }

            const QString text = QString::number(value, 'g', 4);
            const int textWidth = fm.horizontalAdvance(text);
            // Anchored above the dot when there's room, otherwise below --
            // keeps the label inside plotRect near its top gridline instead
            // of clipping past the plot's own top edge.
            const bool above = y - kDotRadius - kLabelGap - textHeight >= plotRect.top();
            const int labelTop = above ? qRound(y - kDotRadius - kLabelGap - textHeight)
                                        : qRound(y + kDotRadius + kLabelGap);
            QRect labelRect(qRound(x) - textWidth / 2 - kLabelPadding, labelTop, textWidth + kLabelPadding * 2,
                             textHeight);
            if (labelRect.left() < plotRect.left()) {
                labelRect.moveLeft(plotRect.left());
            }
            if (labelRect.right() > plotRect.right()) {
                labelRect.moveRight(plotRect.right());
            }

            painter.setPen(Qt::NoPen);
            painter.setBrush(palette.surface);
            painter.setOpacity(0.85);
            painter.drawRoundedRect(labelRect, 2, 2);
            painter.setOpacity(1.0);

            painter.setPen(markerColor);
            painter.drawText(labelRect, Qt::AlignCenter, text);

            painter.setPen(Qt::NoPen);
            painter.setBrush(markerColor);
            painter.drawEllipse(QPointF(x, y), kDotRadius, kDotRadius);
        }
    }
}

// Vertical guide line at the mouse's X, plus a tooltip balloon beside the
// cursor listing every series' value there -- the header gear's "Show hover
// crosshair" toggle, ChartWidgetBase::showsHoverCrosshair(). Unlike
// paintGridPointMarkers() (a label per gridline crossing, scattered across
// the plot), this bundles every series into one balloon that follows the
// cursor, so comparing values at an arbitrary X doesn't mean hunting around
// for the nearest gridline. Reads each series' value via valueAtX() -- see
// that function for how marker-style series and each `interpolation` mode
// affect where a series' dot lands (which may sit a little off the cursor's
// exact X for anything that falls back to a nearest-sample snap). A no-op
// unless `mousePos` actually sits inside plotRect (ChartWidgetBase::
// m_hasHoverPos also gates this at the call site, for "mouse isn't over the
// widget at all").
void paintHoverCrosshair(QPainter& painter, const QRect& plotRect, int capacity, const QPoint& mousePos,
                          const QVector<ChartSeriesConfig>& seriesConfigs, const QVector<QVector<double>>& buffers,
                          double yMin, double yMax, ChartLineInterpolation interpolation,
                          const QVector<bool>& hiddenSeries, const ThemePalette& palette) {
    if (!plotRect.contains(mousePos)) {
        return;
    }
    const qreal hoverX = mousePos.x();

    struct HoverRow {
        const ChartSeriesConfig* series;
        double value;
        qreal x;
        qreal y;
    };
    QVector<HoverRow> rows;
    rows.reserve(seriesConfigs.size());
    for (int i = 0; i < seriesConfigs.size() && i < buffers.size(); ++i) {
        if (i < hiddenSeries.size() && hiddenSeries[i]) {
            continue;
        }
        qreal x = 0.0;
        qreal y = 0.0;
        double value = 0.0;
        const bool found = valueAtX(interpolation, isMarkerStyle(seriesConfigs[i].style), plotRect, capacity,
                                     buffers[i], yMin, yMax, hoverX, &x, &y, &value);
        if (found) {
            rows.append({&seriesConfigs[i], value, x, y});
        }
    }
    if (rows.isEmpty()) {
        return;
    }

    painter.setPen(QPen(palette.textSecondary, 1, Qt::DashLine));
    painter.drawLine(qRound(hoverX), plotRect.top(), qRound(hoverX), plotRect.bottom());

    for (const HoverRow& row : rows) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(row.series->color);
        painter.drawEllipse(QPointF(row.x, row.y), 3.5, 3.5);
    }

    // Balloon sized to its widest "name: value" row, same one-shared-width
    // idea as legendColumns() -- every row lines up on the same left edge
    // instead of each hugging its own text width.
    const QFontMetrics fm(painter.font());
    const int rowHeight = qMax(kLegendSwatchSize, fm.height());
    constexpr int kBalloonPadding = 6;
    constexpr int kBalloonGap = 14;  // clear of the cursor hotspot
    constexpr int kSwatchTextGap = 5;

    QStringList lines;
    int textWidth = 0;
    for (const HoverRow& row : rows) {
        // Free function -- see seriesDisplayName() above for why this uses
        // QCoreApplication::translate() instead of tr(). "%1: %2" fixes
        // name-before-value order -- a translator wanting to swap that order
        // for a given language would need to reorder these placeholders; not
        // attempted for this pass.
        const QString text = QCoreApplication::translate("ChartWidgets", "%1: %2")
                                  .arg(seriesDisplayName(*row.series), QString::number(row.value, 'g', 4));
        lines << text;
        textWidth = qMax(textWidth, fm.horizontalAdvance(text));
    }

    const int balloonWidth = kBalloonPadding * 2 + kLegendSwatchSize + kSwatchTextGap + textWidth;
    const int balloonHeight = kBalloonPadding * 2 + rowHeight * rows.size();

    QRect balloonRect(mousePos.x() + kBalloonGap, mousePos.y() - balloonHeight / 2, balloonWidth, balloonHeight);
    if (balloonRect.right() > plotRect.right()) {
        // No room to the cursor's right -- flip to its left instead.
        balloonRect.moveLeft(mousePos.x() - kBalloonGap - balloonWidth);
    }
    if (balloonRect.left() < plotRect.left()) {
        balloonRect.moveLeft(plotRect.left());
    }
    if (balloonRect.right() > plotRect.right()) {
        balloonRect.moveRight(plotRect.right());
    }
    if (balloonRect.top() < plotRect.top()) {
        balloonRect.moveTop(plotRect.top());
    }
    if (balloonRect.bottom() > plotRect.bottom()) {
        balloonRect.moveBottom(plotRect.bottom());
    }

    painter.setPen(QPen(palette.border, 1));
    painter.setBrush(palette.surface);
    painter.setOpacity(0.95);
    painter.drawRoundedRect(balloonRect, 4, 4);
    painter.setOpacity(1.0);

    for (int i = 0; i < rows.size(); ++i) {
        const int y = balloonRect.top() + kBalloonPadding + i * rowHeight;
        painter.setPen(Qt::NoPen);
        painter.setBrush(rows[i].series->color);
        painter.drawEllipse(QRect(balloonRect.left() + kBalloonPadding, y + (rowHeight - kLegendSwatchSize) / 2,
                                   kLegendSwatchSize, kLegendSwatchSize));

        painter.setPen(palette.textPrimary);
        const QRect textRect(balloonRect.left() + kBalloonPadding + kLegendSwatchSize + kSwatchTextGap, y, textWidth,
                              rowHeight);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }
}

// One fixed-position bar per configured series -- unlike the line chart
// (or this function's scrolling predecessor, paintBarSeries()), there is no
// sample history to lay out along X: every series gets exactly one bar,
// always at the same X slot, redrawn from that series' latest buffered
// value alone. `area` is the widget's full rect (not plotRect) so the
// per-bar value label below can sit in the bottom margin band under
// plotRect -- the same band plotBottomMargin() reserves for the line
// chart's legend row, just filled differently here (see the call site's
// showLastValueRow=false/showXAxisTag=false).
//
// `hiddenSeries[i]` (toggled by clicking that series' legend entry) drops it
// from the slot layout entirely rather than just skipping its draw -- `slot`
// below is sized against the *visible* count, so the remaining bars widen
// and re-center into the space a hidden one would have left behind instead
// of leaving a blank gap where it used to sit.
void paintBarSnapshot(QPainter& painter, const QRect& plotRect, const QRect& area,
                       const QVector<ChartSeriesConfig>& seriesConfigs, const QVector<QVector<double>>& buffers,
                       double yMin, double yMax, const QVector<bool>& hiddenSeries, const ThemePalette& palette) {
    QVector<int> visible;
    visible.reserve(seriesConfigs.size());
    for (int i = 0; i < seriesConfigs.size(); ++i) {
        if (i >= hiddenSeries.size() || !hiddenSeries[i]) {
            visible.append(i);
        }
    }
    if (visible.isEmpty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
    const int barCount = visible.size();
    const qreal slot = qreal(plotRect.width()) / qreal(barCount);
    constexpr qreal kBarGapFraction = 0.3;
    const qreal barWidth = qMax(1.0, slot * (1.0 - kBarGapFraction));
    const qreal zeroY = zeroBaselineY(plotRect, yMin, yMax);

    const int rowHeight = legendRowHeight(painter);
    const int labelY = area.bottom() - kOuterPadding - rowHeight + 1;

    for (int slotIndex = 0; slotIndex < barCount; ++slotIndex) {
        const int series = visible[slotIndex];
        const qreal slotLeft = plotRect.left() + slot * slotIndex;
        const qreal x = slotLeft + (slot - barWidth) / 2.0;

        const QVector<double>& values = series < buffers.size() ? buffers[series] : QVector<double>();
        const bool hasValue = !values.isEmpty();
        if (hasValue) {
            const double value = values.last();
            const qreal t = qBound(0.0, (value - yMin) / yRange, 1.0);
            const qreal barTop = plotRect.bottom() - plotRect.height() * t;

            painter.setPen(Qt::NoPen);
            painter.setBrush(seriesConfigs[series].color);
            painter.drawRect(QRectF(x, qMin(barTop, zeroY), barWidth, qAbs(barTop - zeroY)));
        }

        painter.setPen(palette.textSecondary);
        // Free function -- see seriesDisplayName() above for why this uses
        // QCoreApplication::translate() instead of tr().
        const QString text = hasValue ? QString::number(values.last(), 'g', 4)
                                       : QCoreApplication::translate("ChartWidgets", "--");
        const QRect labelRect(qRound(slotLeft), labelY, qRound(slot), rowHeight);
        painter.drawText(labelRect, Qt::AlignCenter, text);
    }
}

} // namespace

void ChartWidgetBase::setConfig(const QJsonObject& config) {
    const ChartConfig newConfig = parseChartConfig(config);
    m_seriesBuffers = resizeChartBuffers(m_seriesBuffers, newConfig);
    // Carried over by row position, same as resizeChartBuffers() above -- a
    // series still at the same row keeps whatever hidden/shown state the user
    // last clicked to; rows past the old series count start visible.
    QVector<bool> hidden(newConfig.series.size(), false);
    for (int i = 0; i < hidden.size() && i < m_seriesHidden.size(); ++i) {
        hidden[i] = m_seriesHidden[i];
    }
    m_seriesHidden = hidden;
    m_config = newConfig;
    update();
}

void ChartWidgetBase::setPaused(bool paused) {
    m_paused = paused;
}

void ChartWidgetBase::clearChartData() {
    for (TelemetrySeriesBuffer& buffer : m_seriesBuffers) {
        buffer.clear();
    }
    update();
}

void ChartWidgetBase::setShowsLastValueRow(bool show) {
    if (m_showLastValueRow == show) {
        return;
    }
    m_showLastValueRow = show;
    update();
}

void ChartWidgetBase::setShowsGridPointMarkers(bool show) {
    if (m_showGridPointMarkers == show) {
        return;
    }
    m_showGridPointMarkers = show;
    update();
}

void ChartWidgetBase::setShowsHoverCrosshair(bool show) {
    if (m_showHoverCrosshair == show) {
        return;
    }
    m_showHoverCrosshair = show;
    update();
}

void ChartWidgetBase::setLineInterpolation(const QString& id) {
    const ChartLineInterpolation mode = chartLineInterpolationFromId(id);
    if (m_lineInterpolation == mode) {
        return;
    }
    m_lineInterpolation = mode;
    update();
}

void ChartWidgetBase::mouseMoveEvent(QMouseEvent* event) {
    if (m_showHoverCrosshair) {
        m_hoverPos = event->position().toPoint();
        m_hasHoverPos = true;
        update();
    }
    DashboardWidget::mouseMoveEvent(event);
}

void ChartWidgetBase::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const QPoint pos = event->position().toPoint();
        for (int i = 0; i < m_legendHitRects.size(); ++i) {
            if (m_legendHitRects[i].contains(pos)) {
                if (i >= m_seriesHidden.size()) {
                    m_seriesHidden.resize(i + 1);
                }
                m_seriesHidden[i] = !m_seriesHidden[i];
                update();
                event->accept();
                return;
            }
        }
    }
    DashboardWidget::mousePressEvent(event);
}

void ChartWidgetBase::leaveEvent(QEvent* event) {
    if (m_hasHoverPos) {
        m_hasHoverPos = false;
        update();
    }
    DashboardWidget::leaveEvent(event);
}

void ChartWidgetBase::appendFieldSample(quint16 fieldId, quint64 timestampUs, double value) {
    if (m_paused) {
        return;
    }
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
    QTimer::singleShot(m_repaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

DummyLineChartWidget::DummyLineChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

void DummyLineChartWidget::paintEvent(QPaintEvent*) {
    notePaintFrame();
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
    // Computed once and handed to both plotLeftMargin() (to size the reserved
    // gutter) and paintYAxis() (to actually draw into it) -- both used to call
    // axisLabelWidth() separately with the same yMin/yMax, redoing the same
    // font-metrics work twice a frame for an identical result.
    const int labelWidth = axisLabelWidth(painter, yMin, yMax);
    const int topMargin = plotTopMargin(painter);
    const int leftMargin = plotLeftMargin(painter, !m_config.yUnit.isEmpty(), labelWidth);
    const int bottomMargin = plotBottomMargin(painter);
    const QRect plotRect = area.adjusted(leftMargin, topMargin, -kOuterPadding, -bottomMargin);
    const int capacity = chartBufferCapacity(m_config);
    const QVector<int> xLines = xGridLines(plotRect);

    paintYAxis(painter, plotRect, yMin, yMax, m_config.yUnit, m_config.showGrid, kLineYGridDivisions, labelWidth,
               palette);
    paintXAxis(painter, plotRect, xLines, m_config.showGrid, palette);
    for (int i = 0; i < m_config.series.size() && i < seriesValues.size(); ++i) {
        // A series hidden via its legend entry (see mousePressEvent()) is
        // dropped from the plot entirely, not just faded -- only the legend
        // itself keeps showing it, grayed, so the same click can restore it.
        if (i < m_seriesHidden.size() && m_seriesHidden[i]) {
            continue;
        }
        paintLineSeries(painter, plotRect, capacity, m_config.series[i], seriesValues[i], yMin, yMax,
                         m_lineInterpolation);
    }
    // Gated on showGrid too -- the markers are dots at the gridline
    // crossings, so they lose their reference entirely once the gridlines
    // themselves are hidden.
    if (m_showGridPointMarkers && m_config.showGrid) {
        paintGridPointMarkers(painter, plotRect, capacity, xLines, m_config.series, seriesValues, yMin, yMax,
                               m_lineInterpolation, m_seriesHidden, palette);
    }
    if (m_showHoverCrosshair && m_hasHoverPos) {
        paintHoverCrosshair(painter, plotRect, capacity, m_hoverPos, m_config.series, seriesValues, yMin, yMax,
                             m_lineInterpolation, m_seriesHidden, palette);
    }

    // No more redundant "Line Chart" corner label -- DashboardCell's header
    // already shows the configured name (TAREFA 1); this corner now carries
    // the per-series legend instead, which the old literal text had no room
    // for anyway.
    paintSeriesLegends(painter, area, m_config.series, seriesValues, m_config.xAxisMode, m_showLastValueRow,
                       /*showXAxisTag=*/true, m_seriesHidden, palette, &m_legendHitRects);
}

DummyBarChartWidget::DummyBarChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

void DummyBarChartWidget::paintEvent(QPaintEvent*) {
    notePaintFrame();
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
    // See DummyLineChartWidget::paintEvent() -- shared once instead of each
    // of plotLeftMargin()/paintYAxis() recomputing it.
    const int labelWidth = axisLabelWidth(painter, yMin, yMax);
    const int topMargin = plotTopMargin(painter);
    const int leftMargin = plotLeftMargin(painter, !m_config.yUnit.isEmpty(), labelWidth);
    const int bottomMargin = plotBottomMargin(painter);
    const QRect plotRect = area.adjusted(leftMargin, topMargin, -kOuterPadding, -bottomMargin);

    // No paintXAxis()/xGridLines() here -- those are time/sample gridlines,
    // and this chart no longer has a time/sample axis: it's a fixed bar per
    // series, each labeled with its own current value directly below it
    // (paintBarSnapshot() below) rather than scrolling through history.
    paintYAxis(painter, plotRect, yMin, yMax, m_config.yUnit, m_config.showGrid, kBarYGridDivisions, labelWidth,
               palette);
    paintBarSnapshot(painter, plotRect, area, m_config.series, seriesValues, yMin, yMax, m_seriesHidden, palette);

    // Top name row only -- no bottom "last value" row or "t"/"k" tag, since
    // each bar already carries its own current value (see above). See
    // DummyLineChartWidget::paintEvent for why this is a legend instead of a
    // "Bar Chart" corner label.
    paintSeriesLegends(painter, area, m_config.series, seriesValues, m_config.xAxisMode, /*showLastValueRow=*/false,
                       /*showXAxisTag=*/false, m_seriesHidden, palette, &m_legendHitRects);
}

DummyGaugeWidget::DummyGaugeWidget(QWidget* parent) : DashboardWidget(parent) {}

void DummyGaugeWidget::setConfig(const QJsonObject& config) {
    m_config = parseGaugeConfig(config);
    // Carried over by row position, same as resizeChartBuffers() -- a ring
    // still at the same row keeps showing its last value instead of
    // flashing to "--" while waiting for the next sample; rings past the
    // old series count start at NaN like a fresh widget would.
    QVector<double> values(m_config.series.size(), qQNaN());
    for (int i = 0; i < values.size() && i < m_values.size(); ++i) {
        values[i] = m_values[i];
    }
    m_values = values;
    update();
}

void DummyGaugeWidget::clearChartData() {
    for (double& value : m_values) {
        value = qQNaN();
    }
    update();
}

void DummyGaugeWidget::appendFieldSample(quint16 fieldId, quint64 timestampUs, double value) {
    Q_UNUSED(timestampUs);  // a gauge only ever shows the current value
    if (m_paused) {
        return;
    }
    bool matched = false;
    for (int i = 0; i < m_config.series.size() && i < m_values.size(); ++i) {
        if (m_config.series[i].fieldId == fieldId) {
            m_values[i] = value;
            matched = true;
        }
    }
    if (matched) {
        scheduleRepaint();
    }
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
    QTimer::singleShot(m_repaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

void DummyGaugeWidget::paintEvent(QPaintEvent*) {
    notePaintFrame();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const QRect area = rect();

    paintBackground(painter, *this, palette);

    // No more redundant "Gauge" corner label -- DashboardCell's header
    // already shows the configured name (TAREFA 1), same reasoning as the
    // line/bar charts in TAREFA 3.
    const int seriesCount = m_config.series.size();
    // A single ring keeps the original large centered value label -- once
    // there's more than one ring, that space goes to a name/value legend
    // instead (see paintGaugeLegend()), since one big number can no longer
    // speak for the whole widget. The legend sits above the arc, one row
    // with every ring's swatch side by side -- same top-of-widget placement
    // as the line/bar charts' series legend, instead of stacking one ring
    // per row below the arc.
    const int legendHeight = seriesCount > 1 ? gaugeLegendHeight(painter) + kOuterPadding : 0;

    QRect plotRect = area.adjusted(kOuterPadding, kOuterPadding, -kOuterPadding, -kOuterPadding);
    QRect legendRect;
    if (legendHeight > 0 && plotRect.height() - legendHeight > 0) {
        legendRect = QRect(plotRect.left(), plotRect.top(), plotRect.width(), legendHeight);
        plotRect.setTop(legendRect.bottom() + 1);
    }

    const int side = qMin(plotRect.width(), plotRect.height());
    if (side > 0) {
        const QRect arcRect(plotRect.center().x() - side / 2, plotRect.top(), side, side);
        const QPointF center = arcRect.center();
        const double outerRadius = side / 2.0 - 4.0;
        const double pitch = gaugeRingPitch(outerRadius, qMax(1, seriesCount));

        // Innermost-first in z-order (i == 0 painted first) so an outer
        // ring's pointer/ticks never get buried under an inner one drawn on
        // top of it -- rings are laid out outer-to-inner by index, so
        // painting in the same order means each later ring sits *inside*
        // the ones already drawn, never over them.
        for (int i = 0; i < seriesCount; ++i) {
            const GaugeSeriesConfig& seriesConfig = m_config.series[i];
            const double ringRadius = outerRadius - i * pitch;
            if (ringRadius < kGaugeMinInnerRadius / 2.0) {
                break;  // out of room -- further rings would invert/overlap
            }
            const double penWidth = qBound(3.0, pitch - kGaugeRingGap, kGaugeMaxRingPenWidth);
            const QRectF ringRect(center.x() - ringRadius, center.y() - ringRadius, ringRadius * 2.0,
                                   ringRadius * 2.0);

            const double value = i < m_values.size() ? m_values[i] : qQNaN();
            const bool hasValue = !qIsNaN(value);
            const double range = m_config.max - m_config.min;
            const double fraction =
                hasValue && range != 0.0 ? qBound(0.0, (value - m_config.min) / range, 1.0) : 0.0;

            // Flat caps, not round -- a round cap left a little rounded blob
            // sticking out past the arc's true end, most visible where the
            // value arc stops mid-ring; flat keeps both the track and the
            // value fill cut straight across at their endpoints.
            QPen trackPen(palette.surfaceAlt, penWidth, Qt::SolidLine, Qt::FlatCap);
            painter.setPen(trackPen);
            painter.drawArc(ringRect, kGaugeQtStartAngle, kGaugeQtSpanAngle);

            // The curved ruler: graduation ticks around the track, same role
            // as a speedometer's minor marks -- a sense of scale independent
            // of the value fill.
            paintGaugeTicks(painter, center, ringRadius, penWidth, palette.border);

            if (hasValue) {
                QPen valuePen(seriesConfig.color, penWidth, Qt::SolidLine, Qt::FlatCap);
                painter.setPen(valuePen);
                painter.drawArc(ringRect, kGaugeQtStartAngle, qRound(kGaugeQtSpanAngle * fraction));

                // The pointer: a sharp mark crossing the ring exactly at the
                // value's angle, like a needle tip -- legible on its own even
                // when the filled arc's length is hard to judge by eye.
                paintGaugePointer(painter, center, ringRadius, penWidth, fraction, palette.textPrimary);
            }
        }

        if (seriesCount == 1) {
            // The single most important number on this widget -- larger,
            // bold, the one deliberate typography accent in the app (see
            // "Typography" in docs/VISUAL_IDENTITY.md); every other label
            // stays system default weight/size.
            const double value = m_values.value(0, qQNaN());
            const bool hasValue = !qIsNaN(value);
            QFont valueFont = painter.font();
            valueFont.setPointSize(valueFont.pointSize() + 6);
            valueFont.setBold(true);
            painter.setFont(valueFont);
            painter.setPen(palette.textPrimary);
            // Class member function -- plain tr() works here (unlike the
            // free-function helpers above it in the anonymous namespace).
            const QString text = hasValue
                                      ? tr("%1%2").arg(value, 0, 'f', m_config.decimals).arg(m_config.unit)
                                      : tr("--");
            painter.drawText(arcRect, Qt::AlignCenter, text);
        }
    }

    if (!legendRect.isNull()) {
        paintGaugeLegend(painter, legendRect, m_config, m_values, palette);
    }
}

} // namespace traceview
