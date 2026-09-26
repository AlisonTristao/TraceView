#include "chartwidgets.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPair>
#include <QTimer>
#include <QtMath>
#include <cmath>

#include "dashboard/paintframecounter.h"
#include "preferences/appsettings.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// Line chart's Y grid in the Divisions tick placement (Dashboard style):
// just min/mid/max -- a busier grid would fight the throttled, frequently-
// redrawn plot lines for attention on a widget this small. The Nice styles
// put a gridline on every tick instead.
constexpr int kLineYGridDivisions = 2;
// Bar chart's Y grid in the Divisions tick placement: one line every 10% of
// the configured range (11 lines, 10 bands) -- unlike the line chart,
// snapshot bars have nothing else competing for attention in the plot area,
// and reading a bar's height off a fine-grained ruler is the whole point of
// the fixed 0-100%-style range these are normally configured with.
constexpr int kBarYGridDivisions = 10;

// Free function (file-scope, not a member of any QObject-derived class) --
// tr() isn't callable here, so this and the other user-facing strings below
// use QCoreApplication::translate() with an explicit context instead.
QString seriesDisplayName(const QString& name, quint16 fieldId) {
    return name.isEmpty() ? QCoreApplication::translate("ChartWidgets", "Field %1").arg(fieldId)
                          : name;
}

// A series buffer's latest sample, formatted with the chart's own decimals.
// "--" for a series with no data yet rather than 0 -- an absent reading and
// an actual zero reading need to look different.
QString formatLatestValue(const QVector<double>& buffer, int decimals) {
    if (buffer.isEmpty()) {
        return QCoreApplication::translate("ChartWidgets", "--");
    }
    return QString::number(buffer.last(), 'f', decimals);
}

// Pixel step between adjacent samples, sized against the series' full
// capacity (not however many samples are buffered yet) so points always
// anchor to the right edge and scroll left as new data arrives, rather than
// rescaling every time a not-yet-full buffer grows. Matches
// chartValueToX() over timeAxisScale(), so the X ticks sit exactly under
// the samples they label.
qreal xStepFor(const QRect& plotRect, int capacity) {
    return capacity > 1 ? qreal(plotRect.width()) / qreal(capacity - 1) : 0.0;
}

// Baseline pixel Y for value 0 -- the anchor Stem interpolation draws each
// sample's lollipop up/down from, and paintBarSnapshot() anchors each bar
// to. Clamped into plotRect so a fixed Y range that excludes 0 still gives
// Stem/bars a sane, inside-the-plot baseline instead of one computed
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
// (the header gear menu's select box): Linear's straight-segment path,
// ZeroOrderHold's step/staircase path, Stem's per-sample lollipop up from
// zeroBaselineY(), or None's bare per-sample dot with no connecting
// line/baseline at all. `lineWidth` is the style's series stroke.
void paintLineSeries(QPainter& painter, const QRect& plotRect, int capacity,
                     const ChartSeriesConfig& seriesConfig, const QVector<double>& values,
                     double yMin, double yMax, ChartLineInterpolation interpolation,
                     qreal lineWidth) {
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

    if (isChartMarkerStyle(seriesConfig.style)) {
        painter.setPen(QPen(seriesConfig.color, lineWidth));
        constexpr qreal kMarkerSize = 4.0;
        for (int i = 0; i < values.size(); ++i) {
            paintChartMarker(painter, pointAt(i), seriesConfig.style, kMarkerSize);
        }
        return;
    }

    if (interpolation == ChartLineInterpolation::Stem) {
        const qreal baselineY = zeroBaselineY(plotRect, yMin, yMax);
        painter.setPen(QPen(seriesConfig.color, lineWidth, chartPenStyle(seriesConfig.style)));
        // Invariant across every sample -- set once, not per sample on this
        // hot path.
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
    // held point ahead of each sample (ZeroOrderHold's staircase) -- a
    // reserved QPolygonF fed straight to drawPolyline() is measurably cheaper
    // than a QPainterPath for a 100+ point series repainted every frame (see
    // tools/chart_benchmark).
    QPolygonF points;
    points.reserve(interpolation == ChartLineInterpolation::ZeroOrderHold ? values.size() * 2 - 1
                                                                          : values.size());
    for (int i = 0; i < values.size(); ++i) {
        const QPointF p = pointAt(i);
        if (i > 0 && interpolation == ChartLineInterpolation::ZeroOrderHold) {
            points.append(QPointF(p.x(), points.last().y()));
        }
        points.append(p);
    }
    painter.setPen(QPen(seriesConfig.color, lineWidth, chartPenStyle(seriesConfig.style)));
    painter.drawPolyline(points);
}

// Interpolates a line series' value (and its plot-space Y) at pixel column
// `x`, using the exact same per-sample placement as paintLineSeries()'s own
// pointAt() -- so a marker dropped at some x always lands exactly on the
// line drawn there instead of drifting off by a rounding difference. Returns
// false (leaving *outY/*outValue untouched) where the line doesn't reach yet
// -- short of the oldest buffered sample's x position -- same as
// paintLineSeries() simply not drawing anything out that far.
bool lineValueAtX(const QRect& plotRect, int capacity, const QVector<double>& values, double yMin,
                  double yMax, qreal x, qreal* outY, double* outValue) {
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
bool nearestValueAtX(const QRect& plotRect, int capacity, const QVector<double>& values,
                     double yMin, double yMax, qreal x, qreal* outX, qreal* outY,
                     double* outValue) {
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
bool heldValueAtX(const QRect& plotRect, int capacity, const QVector<double>& values, double yMin,
                  double yMax, qreal x, qreal* outY, double* outValue) {
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
bool valueAtX(ChartLineInterpolation interpolation, bool markerStyle, const QRect& plotRect,
              int capacity, const QVector<double>& values, double yMin, double yMax, qreal x,
              qreal* outX, qreal* outY, double* outValue) {
    if (markerStyle || interpolation == ChartLineInterpolation::Stem ||
        interpolation == ChartLineInterpolation::None) {
        return nearestValueAtX(plotRect, capacity, values, yMin, yMax, x, outX, outY, outValue);
    }
    *outX = x;
    if (interpolation == ChartLineInterpolation::ZeroOrderHold) {
        return heldValueAtX(plotRect, capacity, values, yMin, yMax, x, outY, outValue);
    }
    return lineValueAtX(plotRect, capacity, values, yMin, yMax, x, outY, outValue);
}

// One dot -- plus its value, in a small text pill for legibility over the
// line/gridline it sits on -- everywhere a series crosses one of the
// vertical X gridlines (gear-menu "Show grid point values" toggle). A fixed,
// series-independent color -- not each series' own color -- so the dot reads
// clearly against whatever it's sitting on top of (the line, a gridline,
// another series) rather than blending into a same-colored line. Reads each
// series' value via valueAtX() -- see that function for how marker-style
// series and each `interpolation` mode affect where a "crossing" comes from
// (which may land a little off the gridline's exact X for anything that
// falls back to a nearest-sample snap).
void paintGridPointMarkers(QPainter& painter, const QRect& plotRect, int capacity,
                           const QVector<qreal>& xLines,
                           const QVector<ChartSeriesConfig>& seriesConfigs,
                           const QVector<QVector<double>>& buffers, const QVector<double>& yMins,
                           const QVector<double>& yMaxs, ChartLineInterpolation interpolation,
                           int decimals, const QVector<bool>& hiddenSeries,
                           const ThemePalette& palette) {
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
        const double yMin = yMins[series];
        const double yMax = yMaxs[series];
        const bool marker = isChartMarkerStyle(seriesConfigs[series].style);
        for (qreal gridX : xLines) {
            qreal x = 0.0;
            qreal y = 0.0;
            double value = 0.0;
            if (!valueAtX(interpolation, marker, plotRect, capacity, values, yMin, yMax,
                          qRound(gridX), &x, &y, &value)) {
                continue;
            }

            const QString text = QString::number(value, 'f', decimals);
            const int textWidth = fm.horizontalAdvance(text);
            // Anchored above the dot when there's room, otherwise below --
            // keeps the label inside plotRect near its top gridline instead
            // of clipping past the plot's own top edge.
            const bool above = y - kDotRadius - kLabelGap - textHeight >= plotRect.top();
            const int labelTop = above ? qRound(y - kDotRadius - kLabelGap - textHeight)
                                       : qRound(y + kDotRadius + kLabelGap);
            QRect labelRect(qRound(x) - textWidth / 2 - kLabelPadding, labelTop,
                            textWidth + kLabelPadding * 2, textHeight);
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
// crosshair" toggle. Unlike paintGridPointMarkers() (a label per gridline
// crossing, scattered across the plot), this bundles every series into one
// balloon that follows the cursor, so comparing values at an arbitrary X
// doesn't mean hunting around for the nearest gridline. Reads each series'
// value via valueAtX(). A no-op unless `mousePos` actually sits inside
// plotRect (ChartWidgetBase::m_hasHoverPos also gates this at the call site,
// for "mouse isn't over the widget at all").
void paintHoverCrosshair(QPainter& painter, const QRect& plotRect, int capacity,
                         const QPoint& mousePos, const QVector<ChartSeriesConfig>& seriesConfigs,
                         const QVector<QVector<double>>& buffers, const QVector<double>& yMins,
                         const QVector<double>& yMaxs, ChartLineInterpolation interpolation,
                         int decimals, const QVector<bool>& hiddenSeries,
                         const ThemePalette& palette) {
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
        const bool found = valueAtX(interpolation, isChartMarkerStyle(seriesConfigs[i].style),
                                    plotRect, capacity, buffers[i], yMins[i], yMaxs[i], hoverX,
                                    &x, &y, &value);
        if (found) {
            rows.append({&seriesConfigs[i], value, x, y});
        }
    }
    if (rows.isEmpty()) {
        return;
    }

    painter.setPen(QPen(palette.textSecondary, 1, Qt::DashLine));
    painter.drawLine(QPointF(crispCoord(hoverX), plotRect.top()),
                     QPointF(crispCoord(hoverX), plotRect.bottom()));

    for (const HoverRow& row : rows) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(row.series->color);
        painter.drawEllipse(QPointF(row.x, row.y), 3.5, 3.5);
    }

    // Balloon sized to its widest "name: value" row, every row on the same
    // left edge instead of each hugging its own text width.
    constexpr int kSwatch = 8;
    const QFontMetrics fm(painter.font());
    const int rowHeight = qMax(kSwatch, fm.height());
    constexpr int kBalloonPadding = 6;
    constexpr int kBalloonGap = 14;  // clear of the cursor hotspot
    constexpr int kSwatchTextGap = 5;

    QStringList lines;
    int textWidth = 0;
    for (const HoverRow& row : rows) {
        // "%1: %2" fixes name-before-value order -- a translator wanting to
        // swap that order for a given language would need to reorder these
        // placeholders; not attempted for this pass.
        const QString text =
            QCoreApplication::translate("ChartWidgets", "%1: %2")
                .arg(seriesDisplayName(row.series->name, row.series->fieldId),
                     QString::number(row.value, 'f', decimals));
        lines << text;
        textWidth = qMax(textWidth, fm.horizontalAdvance(text));
    }

    const int balloonWidth = kBalloonPadding * 2 + kSwatch + kSwatchTextGap + textWidth;
    const int balloonHeight = kBalloonPadding * 2 + rowHeight * rows.size();

    QRect balloonRect(mousePos.x() + kBalloonGap, mousePos.y() - balloonHeight / 2, balloonWidth,
                      balloonHeight);
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
        painter.drawEllipse(QRect(balloonRect.left() + kBalloonPadding,
                                  y + (rowHeight - kSwatch) / 2, kSwatch, kSwatch));

        painter.setPen(palette.textPrimary);
        const QRect textRect(balloonRect.left() + kBalloonPadding + kSwatch + kSwatchTextGap, y,
                             textWidth, rowHeight);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
    }
}

// Bar slot geometry shared by paintBarSnapshot() and the category ticks
// under each bar: one slot per *visible* series, so hiding one (legend
// click) re-centers the rest instead of leaving a gap.
struct BarSlots {
    QVector<int> visible;  // series index per slot
    qreal slotWidth = 0.0;
};

BarSlots barSlots(const QRect& plotRect, int seriesCount, const QVector<bool>& hiddenSeries) {
    BarSlots bars;
    for (int i = 0; i < seriesCount; ++i) {
        if (i >= hiddenSeries.size() || !hiddenSeries[i]) {
            bars.visible.append(i);
        }
    }
    if (!bars.visible.isEmpty()) {
        bars.slotWidth = qreal(plotRect.width()) / qreal(bars.visible.size());
    }
    return bars;
}

// One fixed-position bar per visible series, each redrawn from that
// series' latest buffered value alone, with that value printed in the
// bottom legend band under its bar when `showValues`.
void paintBarSnapshot(QPainter& painter, const ChartCartesianLayout& layout, const BarSlots& bars,
                      const QVector<ChartSeriesConfig>& seriesConfigs,
                      const QVector<QVector<double>>& buffers, const QVector<double>& shown,
                      const QVector<double>& yMins, const QVector<double>& yMaxs, int decimals,
                      bool showValues, const ChartStyle& style, const ChartColors& colors) {
    const QRect& plotRect = layout.plotRect;
    if (bars.visible.isEmpty() || plotRect.width() <= 0 || plotRect.height() <= 0) {
        return;
    }
    const qreal barWidth = qMax(1.0, bars.slotWidth * style.barWidthFraction);

    for (int slotIndex = 0; slotIndex < bars.visible.size(); ++slotIndex) {
        const int series = bars.visible[slotIndex];
        const double yMin = yMins[series];
        const double yMax = yMaxs[series];
        const double yRange = (yMax - yMin) != 0.0 ? (yMax - yMin) : 1.0;
        const qreal zeroY = zeroBaselineY(plotRect, yMin, yMax);
        const qreal slotLeft = plotRect.left() + bars.slotWidth * slotIndex;
        const qreal x = slotLeft + (bars.slotWidth - barWidth) / 2.0;

        const QVector<double>& values =
            series < buffers.size() ? buffers[series] : QVector<double>();
        const bool hasValue = !values.isEmpty();
        if (hasValue) {
            // The bar height eases (StyledChartWidget::easedValues()); the
            // printed value below is always the real reading.
            const double value = !qIsNaN(shown.value(series, qQNaN())) ? shown[series]
                                                                       : values.last();
            const qreal t = qBound(0.0, (value - yMin) / yRange, 1.0);
            const qreal barTop = plotRect.bottom() - plotRect.height() * t;
            const QColor color = seriesConfigs[series].color;
            if (style.barOutline) {
                painter.setPen(QPen(color.darker(140), 1));
            } else {
                painter.setPen(Qt::NoPen);
            }
            painter.setBrush(color);
            painter.drawRect(QRectF(x, qMin(barTop, zeroY), barWidth, qAbs(barTop - zeroY)));
        }

        if (showValues) {
            painter.setPen(colors.tickLabel);
            const QString text = hasValue ? QString::number(values.last(), 'f', decimals)
                                          : QCoreApplication::translate("ChartWidgets", "--");
            const QRect labelRect(qRound(slotLeft), layout.bottomLegendTop,
                                  qRound(bars.slotWidth), layout.legendRowHeight);
            painter.drawText(labelRect, Qt::AlignCenter, text);
        }
    }
}

CartesianChrome cartesianChrome(const ChartViewOptions& view, const ChartConfig& config,
                                int gridDivisions) {
    CartesianChrome chrome;
    chrome.xTickLabels = view.showXTickLabels;
    chrome.xTitle = view.showXAxisTitle;
    chrome.yTickLabels = view.showYTickLabels;
    chrome.yTitle = view.showYAxisTitle;
    chrome.showGrid = config.showGrid;
    chrome.gridDivisions = gridDivisions;
    chrome.yTickCount = view.yTickCount;
    const bool outside = view.legendPlacement == ChartLegendPlacement::Outside;
    chrome.topLegendRow = outside;
    chrome.bottomLegendRow = outside;
    return chrome;
}

// --- Gauge geometry ---------------------------------------------------
//
// The Ring and HalfCircle gauge shapes draw one arc per configured series,
// all sharing one sweep at successively smaller radii -- ring 0 outermost,
// each following ring nested inside it. See DummyGaugeWidget::paintEvent().

// Where a gauge's arc starts and how far it sweeps, in plain degrees (0 = 3
// o'clock, counter-clockwise positive, so a negative span runs clockwise).
// Qt's drawArc() wants 1/16th degrees -- see qtAngle().
struct GaugeSweep {
    double startDeg;
    double spanDeg;
};

// 12 o'clock clockwise to 9 o'clock, a 90 degree gap at the bottom.
constexpr GaugeSweep kRingSweep{90.0, -270.0};
// 9 o'clock over the top to 3 o'clock -- a speedometer, flat side down.
constexpr GaugeSweep kHalfSweep{180.0, -180.0};

int qtAngle(double degrees) {
    return qRound(degrees * 16.0);
}

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
// derive its own pen width -- see paintGaugeDial().
double gaugeRingPitch(double outerRadius, int ringCount) {
    if (ringCount <= 1) {
        return kGaugeDefaultRingPitch;
    }
    const double available = qMax(0.0, outerRadius - kGaugeMinInnerRadius);
    return qBound(kGaugeMinRingPitch, available / (ringCount - 1), kGaugeDefaultRingPitch);
}

// A point at `fraction` along `sweep` (0 = its start, 1 = its end),
// `radius` out from `center`. Shared by the ruler ticks, the scale labels
// and the value pointer below.
QPointF pointOnGaugeArc(const QPointF& center, double radius, double fraction,
                        const GaugeSweep& sweep) {
    const double angleRad = qDegreesToRadians(sweep.startDeg + sweep.spanDeg * fraction);
    return QPointF(center.x() + radius * qCos(angleRad), center.y() - radius * qSin(angleRad));
}

void paintGaugeRadialTick(QPainter& painter, const QPointF& center, double fraction,
                          double innerRadius, double outerRadius, const GaugeSweep& sweep) {
    painter.drawLine(pointOnGaugeArc(center, innerRadius, fraction, sweep),
                     pointOnGaugeArc(center, outerRadius, fraction, sweep));
}

// Divisions-mode ruler: one graduation mark every 10% of the configured
// range -- a curved ruler around the arc so the eye has a scale to read the
// fill against, not just the bare filled/unfilled split.
constexpr int kGaugeTickDivisions = 10;
constexpr double kGaugeTickLength = 4.0;
constexpr double kGaugeMinorTickLength = 2.0;
constexpr double kGaugeLabelGap = 3.0;
// Scale labels on a 270 degree arc need more room each than on a straight
// axis, so a gauge gets fewer ticks than its diameter alone would allow.
constexpr int kGaugeMaxNiceTicks = 7;

// The gauge's scale as sweep fractions (0..1): the major tick positions,
// the minor ones between them (Nice placement only), and the labels for the
// major ones. Divisions placement without labels keeps the original ten
// equal steps.
struct GaugeScale {
    QVector<double> majors;
    QVector<double> minors;
    QStringList labels;  // same order as majors, empty when unlabeled
};

GaugeScale gaugeScale(const GaugeConfig& config, const ChartStyle& style, bool labeled) {
    GaugeScale scale;
    const double range = config.max - config.min;
    if ((style.ticks == ChartTickPlacement::Divisions && !labeled) || range == 0.0) {
        for (int i = 0; i <= kGaugeTickDivisions; ++i) {
            scale.majors.append(double(i) / kGaugeTickDivisions);
        }
        return scale;
    }
    const NiceScale nice = niceScale(config.min, config.max, kGaugeMaxNiceTicks, false);
    for (double tick : nice.ticks) {
        scale.majors.append((tick - config.min) / range);
        if (labeled) {
            scale.labels.append(formatTick(tick, nice.decimals));
        }
    }
    // Five minor steps per major step, skipped when they would crowd.
    const double minorStep = nice.step / 5.0;
    if (range / minorStep <= 60.0) {
        const double first = std::ceil(qMin(config.min, config.max) / minorStep) * minorStep;
        for (double v = first; v <= qMax(config.min, config.max) + minorStep * 1e-9;
             v += minorStep) {
            scale.minors.append((v - config.min) / range);
        }
    }
    return scale;
}

// Minor ticks only on the outermost ring (`withMinors`) -- repeated on
// every nested ring they turn into noise, and the rings share one scale.
void paintGaugeTicks(QPainter& painter, const QPointF& center, double ringRadius, double penWidth,
                     const GaugeScale& scale, bool withMinors, const QColor& color,
                     const GaugeSweep& sweep) {
    painter.setPen(QPen(color, 1));
    const double outer = ringRadius + penWidth / 2.0;
    if (withMinors) {
        for (double fraction : scale.minors) {
            paintGaugeRadialTick(painter, center, fraction, outer, outer + kGaugeMinorTickLength,
                                 sweep);
        }
    }
    for (double fraction : scale.majors) {
        paintGaugeRadialTick(painter, center, fraction, outer, outer + kGaugeTickLength, sweep);
    }
}

// Room the scale labels need outside the outermost ring's ticks.
double gaugeLabelExtent(const QFontMetrics& fm, const GaugeScale& scale) {
    if (scale.labels.isEmpty()) {
        return 0.0;
    }
    int widest = 0;
    for (const QString& label : scale.labels) {
        widest = qMax(widest, fm.horizontalAdvance(label));
    }
    return kGaugeTickLength + kGaugeLabelGap + qMax(widest, fm.height());
}

// Each label centered just past its tick: pushed out by half of its own
// extent along the radial direction, so wide labels at 3/9 o'clock and tall
// ones at 12/6 o'clock clear the tick equally.
void paintGaugeScaleLabels(QPainter& painter, const QPointF& center, double edgeRadius,
                           const GaugeScale& scale, const QColor& color, const GaugeSweep& sweep) {
    const QFontMetrics fm(painter.font());
    painter.setPen(color);
    for (int i = 0; i < scale.majors.size() && i < scale.labels.size(); ++i) {
        const double fraction = scale.majors[i];
        const double angle = qDegreesToRadians(sweep.startDeg + sweep.spanDeg * fraction);
        const int width = fm.horizontalAdvance(scale.labels[i]);
        const double radialHalf =
            qAbs(qCos(angle)) * width / 2.0 + qAbs(qSin(angle)) * fm.height() / 2.0;
        const QPointF at = pointOnGaugeArc(
            center, edgeRadius + kGaugeTickLength + kGaugeLabelGap + radialHalf, fraction, sweep);
        const QRectF rect(at.x() - width / 2.0, at.y() - fm.height() / 2.0, width, fm.height());
        painter.drawText(rect, Qt::AlignCenter, scale.labels[i]);
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
                       double fraction, const QColor& color, const GaugeSweep& sweep) {
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
    const double inner = ringRadius - penWidth / 2.0 - kGaugePointerOvershoot;
    const double outer = ringRadius + penWidth / 2.0 + kGaugePointerOvershoot;
    paintGaugeRadialTick(painter, center, fraction, inner, outer, sweep);
}

// A gauge value as text: "--" before the first sample, else the value with
// the gauge's decimals and unit.
QString gaugeValueText(const GaugeConfig& config, double value) {
    return qIsNaN(value) ? QStringLiteral("--")
                         : QStringLiteral("%1%2").arg(value, 0, 'f', config.decimals)
                               .arg(config.unit);
}

double gaugeFraction(const GaugeConfig& config, double value) {
    const double range = config.max - config.min;
    return !qIsNaN(value) && range != 0.0 ? qBound(0.0, (value - config.min) / range, 1.0) : 0.0;
}

// The name + current value of every ring, side by side in one row above the
// arc -- same legend building blocks as the line/bar charts -- takes over the
// "what number is this" job the single big centered label handles for a
// one-ring gauge, since there's no room left in the center for more than one
// such label once rings start nesting.
void paintGaugeLegend(QPainter& painter, const QRect& area, const GaugeConfig& config,
                      const QVector<double>& values, const ChartStyle& style,
                      const ChartColors& colors, const ThemePalette& palette) {
    const QFontMetrics fm(painter.font());
    const int rowHeight = chartLegendRowHeight(fm);

    QStringList texts;
    QVector<ChartLegendEntry> entries;
    for (int i = 0; i < config.series.size(); ++i) {
        const double value = i < values.size() ? values[i] : qQNaN();
        const QString valueText = qIsNaN(value)
                                      ? QCoreApplication::translate("ChartWidgets", "--")
                                      : QCoreApplication::translate("ChartWidgets", "%1%2")
                                            .arg(value, 0, 'f', config.decimals)
                                            .arg(config.unit);
        // "%1  %2" fixes name-before-value order -- a translator wanting to
        // swap that order for a given language would need to reorder these
        // placeholders; not attempted for this pass.
        texts << QCoreApplication::translate("ChartWidgets", "%1  %2")
                     .arg(seriesDisplayName(config.series[i].name, config.series[i].fieldId),
                          valueText);
        entries.append({config.series[i].color, ChartSeriesStyle::Solid, false});
    }

    const QVector<ChartLegendColumn> columns = chartLegendColumns(
        fm, area.left(), area.right(), config.series.size(), {texts}, style);
    paintChartLegendRow(painter, area.top(), rowHeight, columns, entries, texts, style, colors,
                        palette);
}

}  // namespace

// --- StyledChartWidget ---------------------------------------------------

StyledChartWidget::StyledChartWidget(QWidget* parent) : DashboardWidget(parent) {
    m_repaintIntervalMs = AppSettings::instance().repaintIntervalMs();
    connect(&AppSettings::instance(), &AppSettings::dashboardPreferencesChanged, this,
            [this] { m_repaintIntervalMs = AppSettings::instance().repaintIntervalMs(); });
    connect(&AppSettings::instance(), &AppSettings::chartStyleChanged, this, [this] {
        if (m_view.followAppStyle) {
            update();
        }
    });
    // Every appearance change (data colors included) arrives as
    // themeChanged; the repaint itself comes from the stylesheet re-apply.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) {
                refreshDataColors();
                update();
            });
}

// Easing time constant: ~95% of the way in 3 tau (about 120ms).
constexpr double kEaseTauMs = 40.0;

QVector<double> StyledChartWidget::easedValues(const QVector<double>& targets, double range) {
    if (ThemeManager::instance().reduceMotion() || m_eased.size() != targets.size()) {
        m_eased = targets;
        m_easeClock.restart();
        return m_eased;
    }
    const double dt = qMin<double>(100.0, m_easeClock.isValid() ? m_easeClock.restart() : 16.0);
    const double step = 1.0 - std::exp(-dt / kEaseTauMs);
    const double settle = qMax(1e-9, qAbs(range) * 0.002);
    bool settled = true;
    for (int i = 0; i < targets.size(); ++i) {
        const double target = targets[i];
        double& shown = m_eased[i];
        if (qIsNaN(target) || qIsNaN(shown)) {
            shown = target;
            continue;
        }
        shown += (target - shown) * step;
        if (qAbs(target - shown) <= settle) {
            shown = target;
        } else {
            settled = false;
        }
    }
    if (!settled && !m_easeRepaintPending) {
        m_easeRepaintPending = true;
        QTimer::singleShot(16, this, [this] {
            m_easeRepaintPending = false;
            update();
        });
    }
    return m_eased;
}

ChartStyleId StyledChartWidget::effectiveStyle() const {
    return m_view.followAppStyle ? chartStyleFromId(AppSettings::instance().chartStyleId())
                                 : m_view.style;
}

QVector<WidgetViewOption> StyledChartWidget::viewOptions() const {
    return chartViewOptionList(m_view, viewFeatures());
}

QJsonObject StyledChartWidget::viewConfigWith(const QString& id, const QVariant& value) const {
    return chartViewOptionsToJson(withChartViewOption(m_view, id, value));
}

void StyledChartWidget::setViewConfig(const QJsonObject& view) {
    m_view = parseChartViewOptions(view);
    update();
}

void StyledChartWidget::applyViewFromConfig(const QJsonObject& config) {
    m_view = parseChartViewOptions(config.value(QLatin1String("view")).toObject());
}

void StyledChartWidget::scheduleRepaint() {
    if (m_repaintPending) {
        return;
    }
    m_repaintPending = true;
    QTimer::singleShot(m_repaintIntervalMs, this, [this]() {
        m_repaintPending = false;
        update();
    });
}

// --- ChartWidgetBase -----------------------------------------------------

ChartWidgetBase::ChartWidgetBase(QWidget* parent) : StyledChartWidget(parent) {
    // Needed for mouseMoveEvent() to fire on plain cursor movement (no
    // button held) -- that's how the hover crosshair tracks the mouse across
    // the plot.
    setMouseTracking(true);
}

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
    m_ownColors.clear();
    for (const ChartSeriesConfig& series : m_config.series) {
        m_ownColors.append(series.color);
    }
    refreshDataColors();
    applyViewFromConfig(config);
    update();
}

void ChartWidgetBase::refreshDataColors() {
    for (int i = 0; i < m_config.series.size() && i < m_ownColors.size(); ++i) {
        m_config.series[i].color = ThemeManager::instance().seriesColor(i, m_ownColors[i]);
    }
}

void ChartWidgetBase::clearChartData() {
    for (TelemetrySeriesBuffer& buffer : m_seriesBuffers) {
        buffer.clear();
    }
    update();
}

void ChartWidgetBase::mouseMoveEvent(QMouseEvent* event) {
    if (m_view.showHoverCrosshair) {
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

void ChartWidgetBase::onFieldSample(const traceview::TelemetryFieldBinding& binding,
                                    quint64 timestampUs, double value) {
    if (binding.sourceId != m_config.sourceId || binding.topicId != m_config.topicId) {
        return;
    }
    appendFieldSample(binding.fieldId, timestampUs, value);
}

void ChartWidgetBase::seedFromHistory(const FieldHistoryLookup& lookup) {
    const int capacity = chartBufferCapacity(m_config);
    for (int i = 0; i < m_config.series.size() && i < m_seriesBuffers.size(); ++i) {
        TelemetrySeriesBuffer seeded;
        if (const TelemetrySeriesBuffer* history = lookup(m_config.series[i].fieldId)) {
            seeded = *history;
        }
        seeded.setCapacity(capacity);
        m_seriesBuffers[i] = seeded;
    }
    update();
}

QVector<QVector<double>> ChartWidgetBase::seriesValues() const {
    QVector<QVector<double>> values;
    values.reserve(m_seriesBuffers.size());
    for (const TelemetrySeriesBuffer& buffer : m_seriesBuffers) {
        values.append(buffer.values());
    }
    return values;
}

ChartWidgetBase::SeriesAxes ChartWidgetBase::seriesAxes(
    const QVector<QVector<double>>& values) const {
    SeriesAxes axes;
    axes.axisOfSeries.fill(0, m_config.series.size());

    if (!m_config.autoAxis) {
        ChartAxisRequest request;
        if (m_config.yAxisMode == ChartYAxisMode::Fixed) {
            request.range = {m_config.yMin, m_config.yMax, AxisRange::Source::Fixed};
        } else {
            QVector<double> declaredMins;
            QVector<double> declaredMaxs;
            for (const ChartSeriesConfig& series : m_config.series) {
                declaredMins.append(series.declaredMin);
                declaredMaxs.append(series.declaredMax);
            }
            request.range = seriesDataRange(values, declaredMins, declaredMaxs);
        }
        request.title = m_config.yUnit;
        request.configDecimals = m_config.decimals;
        axes.requests.append(request);
        return axes;
    }

    // One axis per unit (ChartConfig::autoAxis), each auto-ranged from its
    // own member series only. Once there's more than one, each axis is
    // tinted with its first series' color so the eye can match "this axis"
    // to "this line" without a separate legend.
    const QVector<ChartAxisGroup> groups = chartAxisGroups(m_config);
    for (int g = 0; g < groups.size(); ++g) {
        const ChartAxisGroup& group = groups[g];
        QVector<QVector<double>> memberValues;
        QVector<double> declaredMins;
        QVector<double> declaredMaxs;
        for (int idx : group.seriesIndices) {
            memberValues.append(idx < values.size() ? values[idx] : QVector<double>());
            declaredMins.append(m_config.series[idx].declaredMin);
            declaredMaxs.append(m_config.series[idx].declaredMax);
            axes.axisOfSeries[idx] = g;
        }
        ChartAxisRequest request;
        request.range = seriesDataRange(memberValues, declaredMins, declaredMaxs);
        request.title = group.unit;
        request.configDecimals = m_config.decimals;
        if (groups.size() > 1 && !group.seriesIndices.isEmpty()) {
            request.labelColor = m_config.series[group.seriesIndices.first()].color;
        }
        axes.requests.append(request);
    }
    return axes;
}

void ChartWidgetBase::paintLegends(QPainter& painter, const ChartCartesianLayout& layout,
                                   const QVector<QVector<double>>& values, bool withValues,
                                   const ChartStyle& style, const ChartColors& colors,
                                   const ThemePalette& palette) {
    m_legendHitRects.clear();
    const ChartLegendPlacement placement = m_view.legendPlacement;
    if (placement == ChartLegendPlacement::Hidden) {
        return;
    }
    const QFontMetrics fm(painter.font());
    const QRect& area = layout.area;
    const int rowHeight = layout.legendRowHeight;
    const int rightBound = area.right() - chartOuterPadding();
    const int left = area.left() + chartOuterPadding();

    QStringList names;
    QStringList latest;
    QVector<ChartLegendEntry> entries;
    for (int i = 0; i < m_config.series.size(); ++i) {
        const ChartSeriesConfig& series = m_config.series[i];
        names << seriesDisplayName(series.name, series.fieldId);
        latest << formatLatestValue(i < values.size() ? values[i] : QVector<double>(),
                                    m_config.decimals);
        entries.append({series.color, series.style,
                         i < m_seriesHidden.size() && m_seriesHidden[i]});
    }

    if (placement != ChartLegendPlacement::Outside) {
        QStringList texts = names;
        if (withValues) {
            for (int i = 0; i < texts.size(); ++i) {
                // "%1  %2" fixes name-before-value order, same as the gauge
                // legend.
                texts[i] = QCoreApplication::translate("ChartWidgets", "%1  %2")
                               .arg(names[i], latest[i]);
            }
        }
        m_legendHitRects =
            paintChartInsetLegend(painter, layout.plotRect, placement, entries, texts,
                                  m_view.legendOpacity, style, colors, palette);
        return;
    }

    // Both rows share one column layout, so both always show the identical
    // set of series; otherwise a series that fits in the name row but not
    // the value row would show a name with no value underneath it.
    const QVector<ChartLegendColumn> columns = chartLegendColumns(
        fm, left, rightBound, m_config.series.size(), {names, latest}, style);

    const int topY = area.top() + chartOuterPadding();
    const int bottomY = layout.bottomLegendTop;

    const int hitBottom = (withValues ? bottomY : topY) + rowHeight;
    for (int i = 0; i < m_config.series.size(); ++i) {
        m_legendHitRects.append(i < columns.size() ? QRect(columns[i].x, topY, columns[i].width,
                                                           hitBottom - topY)
                                                   : QRect());
    }

    paintChartLegendRow(painter, topY, rowHeight, columns, entries, names, style, colors,
                        palette);
    if (withValues) {
        paintChartLegendRow(painter, bottomY, rowHeight, columns, entries, latest, style, colors,
                            palette);
    }
}

// --- DummyLineChartWidget ------------------------------------------------

DummyLineChartWidget::DummyLineChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

ChartViewFeatures DummyLineChartWidget::viewFeatures() const {
    return ChartViewFeature::XAxisTitle | ChartViewFeature::XTickLabels |
           ChartViewFeature::YAxisTitle | ChartViewFeature::YTickLabels |
           ChartViewFeature::YTickCount | ChartViewFeature::Legend |
           ChartViewFeature::LastValue | ChartViewFeature::GridPoints |
           ChartViewFeature::HoverCrosshair | ChartViewFeature::LineWidth |
           ChartViewFeature::Interpolation;
}

void DummyLineChartWidget::paintEvent(QPaintEvent*) {
    notePaintFrame();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const ChartStyle& style = chartStyle(effectiveStyle());
    const ChartColors colors = chartColors(style, palette);

    paintChartBackground(painter, *this, palette);

    const QVector<QVector<double>> values = seriesValues();
    const SeriesAxes axes = seriesAxes(values);
    const CartesianChrome chrome = cartesianChrome(m_view, m_config, kLineYGridDivisions);
    const bool xLabeled = chrome.xTickLabels;
    const ChartConfig& config = m_config;
    const ChartCartesianLayout layout = layoutCartesianChart(
        painter, rect(), axes.requests, chrome, style,
        [&config, &style, xLabeled](int plotWidth, int maxTicks) {
            return timeAxisScale(config, style, xLabeled, plotWidth, maxTicks);
        });
    const QRect& plotRect = layout.plotRect;
    const int capacity = chartBufferCapacity(m_config);

    paintCartesianAxes(painter, layout, chrome, style, colors,
                       chartTimeAxisTitle(m_config.xAxisMode));

    QVector<double> yMins(m_config.series.size());
    QVector<double> yMaxs(m_config.series.size());
    for (int i = 0; i < m_config.series.size(); ++i) {
        const ValueScale& scale = layout.yAxes[axes.axisOfSeries[i]].scale;
        yMins[i] = scale.lo;
        yMaxs[i] = scale.hi;
    }

    // Clipped to the plot: a Fixed range narrower than the data, or a line
    // wider than 1px at the edge, must not spill over the axis labels.
    painter.save();
    painter.setClipRect(plotRect.adjusted(-1, -1, 1, 1));
    for (int i = 0; i < m_config.series.size() && i < values.size(); ++i) {
        // A series hidden via its legend entry is dropped from the plot
        // entirely, not just faded -- only the legend itself keeps showing
        // it, grayed, so the same click can restore it.
        if (i < m_seriesHidden.size() && m_seriesHidden[i]) {
            continue;
        }
        paintLineSeries(painter, plotRect, capacity, m_config.series[i], values[i], yMins[i],
                        yMaxs[i], m_view.interpolation, effectiveLineWidth(m_view, style));
    }
    painter.restore();

    // Gated on showGrid too -- the markers are dots at the gridline
    // crossings, so they lose their reference entirely once the gridlines
    // themselves are hidden.
    if (m_view.showGridPointMarkers && m_config.showGrid) {
        paintGridPointMarkers(painter, plotRect, capacity, chartXGridPixels(layout),
                              m_config.series, values, yMins, yMaxs, m_view.interpolation,
                              m_config.decimals, m_seriesHidden, palette);
    }
    // Before the crosshair: an in-plot legend sits over the lines, and the
    // hover balloon over everything.
    paintLegends(painter, layout, values, m_view.showLastValueRow, style, colors, palette);

    if (m_view.showHoverCrosshair && m_hasHoverPos) {
        paintHoverCrosshair(painter, plotRect, capacity, m_hoverPos, m_config.series, values,
                            yMins, yMaxs, m_view.interpolation, m_config.decimals,
                            m_seriesHidden, palette);
    }
}

// --- DummyBarChartWidget -------------------------------------------------

DummyBarChartWidget::DummyBarChartWidget(QWidget* parent) : ChartWidgetBase(parent) {}

ChartViewFeatures DummyBarChartWidget::viewFeatures() const {
    return ChartViewFeature::YAxisTitle | ChartViewFeature::YTickLabels |
           ChartViewFeature::YTickCount | ChartViewFeature::Legend |
           ChartViewFeature::LastValue;
}

void DummyBarChartWidget::paintEvent(QPaintEvent*) {
    notePaintFrame();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const ChartStyle& style = chartStyle(effectiveStyle());
    const ChartColors colors = chartColors(style, palette);

    paintChartBackground(painter, *this, palette);

    const QVector<QVector<double>> values = seriesValues();
    const SeriesAxes axes = seriesAxes(values);
    // No time/sample X axis -- a fixed bar per series, each labeled with its
    // own current value directly below it rather than scrolling through
    // history.
    CartesianChrome chrome = cartesianChrome(m_view, m_config, kBarYGridDivisions);
    // The bottom row carries each bar's value label, not a legend row.
    chrome.bottomLegendRow = m_view.showLastValueRow;
    const ChartCartesianLayout layout =
        layoutCartesianChart(painter, rect(), axes.requests, chrome, style);

    const BarSlots bars = barSlots(layout.plotRect, m_config.series.size(), m_seriesHidden);
    QVector<qreal> barCenters;
    for (int i = 0; i < bars.visible.size(); ++i) {
        barCenters.append(layout.plotRect.left() + bars.slotWidth * (i + 0.5));
    }
    paintCartesianAxes(painter, layout, chrome, style, colors, QString(), barCenters);

    QVector<double> yMins(m_config.series.size());
    QVector<double> yMaxs(m_config.series.size());
    for (int i = 0; i < m_config.series.size(); ++i) {
        const ValueScale& scale = layout.yAxes[axes.axisOfSeries[i]].scale;
        yMins[i] = scale.lo;
        yMaxs[i] = scale.hi;
    }
    QVector<double> latest;
    double span = 0.0;
    for (int i = 0; i < m_config.series.size(); ++i) {
        latest.append(i < values.size() && !values[i].isEmpty() ? values[i].last() : qQNaN());
        span = qMax(span, yMaxs[i] - yMins[i]);
    }
    const QVector<double> shown = easedValues(latest, span);
    paintBarSnapshot(painter, layout, bars, m_config.series, values, shown, yMins, yMaxs,
                     m_config.decimals, m_view.showLastValueRow, style, colors);

    // Names only -- each bar already carries its own current value (see
    // above).
    paintLegends(painter, layout, values, /*withValues=*/false, style, colors, palette);
}

// --- DummyGaugeWidget ----------------------------------------------------

DummyGaugeWidget::DummyGaugeWidget(QWidget* parent) : StyledChartWidget(parent) {}

ChartViewFeatures DummyGaugeWidget::viewFeatures() const {
    return ChartViewFeature::GaugeShape | ChartViewFeature::ScaleLabels;
}

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
    m_ownColors.clear();
    for (const GaugeSeriesConfig& series : m_config.series) {
        m_ownColors.append(series.color);
    }
    refreshDataColors();
    applyViewFromConfig(config);
    update();
}

void DummyGaugeWidget::refreshDataColors() {
    for (int i = 0; i < m_config.series.size() && i < m_ownColors.size(); ++i) {
        m_config.series[i].color = ThemeManager::instance().seriesColor(i, m_ownColors[i]);
    }
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

void DummyGaugeWidget::onFieldSample(const traceview::TelemetryFieldBinding& binding,
                                     quint64 timestampUs, double value) {
    if (binding.sourceId != m_config.sourceId || binding.topicId != m_config.topicId) {
        return;
    }
    appendFieldSample(binding.fieldId, timestampUs, value);
}

void DummyGaugeWidget::seedFromHistory(const FieldHistoryLookup& lookup) {
    for (int i = 0; i < m_config.series.size() && i < m_values.size(); ++i) {
        const TelemetrySeriesBuffer* history = lookup(m_config.series[i].fieldId);
        if (history && !history->samples().isEmpty()) {
            m_values[i] = history->samples().last().value;
        }
    }
    update();
}

namespace {

// Everything a gauge shape needs to draw one frame.
struct GaugePaint {
    const GaugeConfig& config;
    const QVector<double>& values;  // latest readings, for text
    const QVector<double>& shown;   // eased readings, for fills/needles
    const ChartViewOptions& view;
    const ChartStyle& style;
    const ChartColors& colors;
    const ThemePalette& palette;
};

// The single most important number on a one-series gauge -- larger, bold,
// the one deliberate typography accent in the app (see "Typography" in
// docs/VISUAL_IDENTITY.md), in tabular figures under the Nice styles.
QFont gaugeValueFont(const QFont& base, const ChartStyle& style, qreal scale) {
    QFont font = chartTickFont(scaledFont(base, scale), style);
    font.setBold(true);
    return font;
}

// Ring (270 degrees) and HalfCircle (180): one arc per series, nested.
void paintGaugeDial(QPainter& painter, const QRect& area, const GaugePaint& g,
                    const GaugeSweep& sweep) {
    const int padding = chartOuterPadding();
    const int seriesCount = g.config.series.size();
    // A single ring keeps the large centered value label -- once there's
    // more than one ring, that space goes to a name/value legend above the
    // arc instead, since one big number can no longer speak for the whole
    // widget.
    const int legendHeight =
        seriesCount > 1 ? chartLegendRowHeight(QFontMetrics(painter.font())) + padding : 0;

    QRect plotRect = area.adjusted(padding, padding, -padding, -padding);
    QRect legendRect;
    if (legendHeight > 0 && plotRect.height() - legendHeight > 0) {
        legendRect = QRect(plotRect.left(), plotRect.top(), plotRect.width(), legendHeight);
        plotRect.setTop(legendRect.bottom() + 1);
    }

    const GaugeScale scale = gaugeScale(g.config, g.style, g.view.showScaleLabels);
    const QFont baseFont = painter.font();
    const QFont tickFont = chartTickFont(baseFont, g.style);
    const double labelExtent = gaugeLabelExtent(QFontMetrics(tickFont), scale);

    const bool half = qFuzzyCompare(sweep.spanDeg, kHalfSweep.spanDeg);
    QPointF center;
    double outerRadius = 0.0;
    if (half) {
        // Flat side down: the arc needs its full width but only its radius
        // in height, so it can grow wider than a full ring would.
        const double byWidth = plotRect.width() / 2.0 - labelExtent - 4.0;
        const double byHeight = plotRect.height() - labelExtent - 8.0;
        outerRadius = qMax(kGaugeMinInnerRadius, qMin(byWidth, byHeight));
        center = QPointF(plotRect.center().x() + 0.5,
                         plotRect.top() + labelExtent + 4.0 + outerRadius);
    } else {
        const int side = qMin(plotRect.width(), plotRect.height());
        if (side <= 0) {
            return;
        }
        center = QPointF(plotRect.center().x() + 0.5, plotRect.top() + side / 2.0);
        outerRadius = qMax(kGaugeMinInnerRadius, side / 2.0 - 4.0 - labelExtent);
    }
    const double pitch = gaugeRingPitch(outerRadius, qMax(1, seriesCount));

    // Innermost-first in z-order (i == 0 painted first) so an outer ring's
    // pointer/ticks never get buried under an inner one drawn on top of it.
    for (int i = 0; i < seriesCount; ++i) {
        const double ringRadius = outerRadius - i * pitch;
        if (ringRadius < kGaugeMinInnerRadius / 2.0) {
            break;  // out of room -- further rings would invert/overlap
        }
        const double penWidth = qBound(3.0, pitch - kGaugeRingGap, kGaugeMaxRingPenWidth);
        const QRectF ringRect(center.x() - ringRadius, center.y() - ringRadius, ringRadius * 2.0,
                              ringRadius * 2.0);
        const double value = i < g.shown.size() ? g.shown[i] : qQNaN();
        const bool hasValue = !qIsNaN(value);
        const double fraction = gaugeFraction(g.config, value);

        // Flat caps, not round -- a round cap left a little rounded blob
        // sticking out past the arc's true end.
        painter.setPen(QPen(g.palette.surfaceAlt, penWidth, Qt::SolidLine, Qt::FlatCap));
        painter.drawArc(ringRect, qtAngle(sweep.startDeg), qtAngle(sweep.spanDeg));

        paintGaugeTicks(painter, center, ringRadius, penWidth, scale, /*withMinors=*/i == 0,
                        g.colors.frame, sweep);
        if (i == 0 && !scale.labels.isEmpty()) {
            painter.setFont(tickFont);
            paintGaugeScaleLabels(painter, center, ringRadius + penWidth / 2.0, scale,
                                  g.colors.tickLabel, sweep);
            painter.setFont(baseFont);
        }

        if (hasValue) {
            painter.setPen(
                QPen(g.config.series[i].color, penWidth, Qt::SolidLine, Qt::FlatCap));
            painter.drawArc(ringRect, qtAngle(sweep.startDeg), qtAngle(sweep.spanDeg * fraction));
            paintGaugePointer(painter, center, ringRadius, penWidth, fraction,
                              g.palette.textPrimary, sweep);
        }
    }

    if (seriesCount == 1) {
        painter.setFont(gaugeValueFont(baseFont, g.style, 1.6));
        painter.setPen(g.palette.textPrimary);
        const QString text = gaugeValueText(g.config, g.values.value(0, qQNaN()));
        // Inside the ring for the full dial; tucked inside the arch, just
        // above the flat side, for the half circle.
        const QRectF textRect =
            half ? QRectF(center.x() - outerRadius, center.y() - outerRadius * 0.6,
                          outerRadius * 2.0, outerRadius * 0.6)
                 : QRectF(center.x() - outerRadius, center.y() - outerRadius, outerRadius * 2.0,
                          outerRadius * 2.0);
        painter.drawText(textRect, half ? (Qt::AlignHCenter | Qt::AlignBottom) : Qt::AlignCenter,
                         text);
        painter.setFont(baseFont);
    }

    if (!legendRect.isNull()) {
        paintGaugeLegend(painter, legendRect, g.config, g.values, g.style, g.colors, g.palette);
    }
}

// One horizontal bar per series: name on the left, value on the right, a
// track with the value fill between them, and (with scale values on) a
// shared scale under the last bar.
void paintGaugeBars(QPainter& painter, const QRect& area, const GaugePaint& g) {
    const int padding = chartOuterPadding();
    const int count = g.config.series.size();
    if (count == 0) {
        return;
    }
    const QRect inner = area.adjusted(padding, padding, -padding, -padding);
    const QFont baseFont = painter.font();
    const QFont tickFont = chartTickFont(baseFont, g.style);
    const QFontMetrics fm(baseFont);
    const QFontMetrics tickFm(tickFont);
    const GaugeScale scale = gaugeScale(g.config, g.style, g.view.showScaleLabels);
    const int scaleHeight = scale.labels.isEmpty() ? 0 : kGaugeTickLength + tickFm.height() + 2;

    int nameWidth = 0;
    for (const GaugeSeriesConfig& series : g.config.series) {
        nameWidth = qMax(nameWidth,
                         fm.horizontalAdvance(seriesDisplayName(series.name, series.fieldId)));
    }
    nameWidth = qMin(nameWidth, inner.width() / 3);
    int valueWidth = tickFm.horizontalAdvance(gaugeValueText(g.config, g.config.max));
    valueWidth = qMax(valueWidth, tickFm.horizontalAdvance(gaugeValueText(g.config, g.config.min)));
    valueWidth = qMax(valueWidth, tickFm.horizontalAdvance(QStringLiteral("--")));
    const int gap = kChartAxisLabelGap * 2;
    const int trackLeft = inner.left() + nameWidth + gap;
    const int trackRight = inner.right() - valueWidth - gap;
    if (trackRight - trackLeft < 10) {
        return;
    }

    const double rowHeight =
        qMin<double>((inner.height() - scaleHeight) / double(count), fm.height() * 2.6);
    const double barThickness = qBound(6.0, rowHeight * 0.45, 18.0);
    const double blockTop = inner.top() + (inner.height() - scaleHeight - rowHeight * count) / 2.0;
    const QRectF track0(trackLeft, 0, trackRight - trackLeft, barThickness);
    // The frame's corner shape on the tracks too, but scaled to a thin bar:
    // a full 8px chamfer on an 18px bar would turn its ends into arrows.
    FrameStyle trackFrame = ThemeManager::instance().currentFrameStyle();
    trackFrame.cornerSize = qMin(trackFrame.cornerSize, barThickness / 3.0);

    for (int i = 0; i < count; ++i) {
        const double rowCenter = blockTop + rowHeight * (i + 0.5);
        const QRectF track = track0.translated(0, rowCenter - barThickness / 2.0);
        const QPainterPath trackPath = frameShapePath(track, trackFrame);
        painter.fillPath(trackPath, g.palette.surfaceAlt);
        const double value = i < g.shown.size() ? g.shown[i] : qQNaN();
        if (!qIsNaN(value)) {
            const QRectF fill(track.left(), track.top(),
                              track.width() * gaugeFraction(g.config, value), track.height());
            painter.save();
            painter.setClipPath(trackPath);
            painter.fillRect(fill, g.config.series[i].color);
            painter.restore();
        }

        painter.setPen(g.colors.legendText);
        const GaugeSeriesConfig& series = g.config.series[i];
        painter.drawText(QRectF(inner.left(), rowCenter - fm.height() / 2.0, nameWidth,
                                fm.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         fm.elidedText(seriesDisplayName(series.name, series.fieldId),
                                       Qt::ElideRight, nameWidth));
        painter.setFont(tickFont);
        painter.setPen(g.palette.textPrimary);
        painter.drawText(QRectF(inner.right() - valueWidth + 1, rowCenter - tickFm.height() / 2.0,
                                valueWidth, tickFm.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         gaugeValueText(g.config, g.values.value(i, qQNaN())));
        painter.setFont(baseFont);
    }

    if (!scale.labels.isEmpty()) {
        const double axisY = blockTop + rowHeight * count + 2.0;
        painter.setPen(QPen(g.colors.frame, 1));
        painter.setFont(tickFont);
        for (int t = 0; t < scale.majors.size(); ++t) {
            const double x = crispCoord(trackLeft + (trackRight - trackLeft) * scale.majors[t]);
            painter.drawLine(QPointF(x, axisY), QPointF(x, axisY + kGaugeTickLength));
            const int w = tickFm.horizontalAdvance(scale.labels[t]);
            painter.save();
            painter.setPen(g.colors.tickLabel);
            painter.drawText(QRectF(x - w / 2.0, axisY + kGaugeTickLength, w, tickFm.height()),
                             Qt::AlignCenter, scale.labels[t]);
            painter.restore();
        }
        painter.setFont(baseFont);
    }
}

// Just the value(s): one large number, or a row per series with its name.
void paintGaugeNumbers(QPainter& painter, const QRect& area, const GaugePaint& g) {
    const int padding = chartOuterPadding();
    const int count = g.config.series.size();
    if (count == 0) {
        return;
    }
    const QRect inner = area.adjusted(padding, padding, -padding, -padding);
    const QFont baseFont = painter.font();
    const QFontMetrics fm(baseFont);

    if (count == 1) {
        const QString text = gaugeValueText(g.config, g.values.value(0, qQNaN()));
        // As large as fits both ways, name underneath.
        QFont font = gaugeValueFont(baseFont, g.style, 1.0);
        const int nameHeight = fm.height() + 4;
        // Measure at a reference size, then scale to whichever of width and
        // height runs out first.
        constexpr double kReference = 20.0;
        font.setPointSizeF(kReference);
        const QFontMetricsF reference(font);
        const double fit =
            qMin((inner.width() * 0.9) / qMax(1.0, reference.horizontalAdvance(text)),
                 (inner.height() - nameHeight) / qMax(1.0, reference.height()));
        font.setPointSizeF(qBound(8.0, kReference * fit, 200.0));
        painter.setFont(font);
        painter.setPen(g.config.series[0].color);
        const QRect valueRect(inner.left(), inner.top(), inner.width(),
                              inner.height() - nameHeight);
        painter.drawText(valueRect, Qt::AlignCenter, text);
        painter.setFont(baseFont);
        painter.setPen(g.colors.legendText);
        const GaugeSeriesConfig& series = g.config.series[0];
        painter.drawText(QRect(inner.left(), valueRect.bottom(), inner.width(), nameHeight),
                         Qt::AlignHCenter | Qt::AlignTop,
                         seriesDisplayName(series.name, series.fieldId));
        return;
    }

    const double rowHeight = inner.height() / double(count);
    QFont valueFont = gaugeValueFont(baseFont, g.style, 1.0);
    valueFont.setPointSizeF(qBound(8.0, rowHeight * 0.45, 28.0));
    for (int i = 0; i < count; ++i) {
        const QRectF row(inner.left(), inner.top() + rowHeight * i, inner.width(), rowHeight);
        const GaugeSeriesConfig& series = g.config.series[i];
        paintChartLegendSwatch(painter,
                               QRect(qRound(row.left()), qRound(row.top()),
                                     chartLegendSwatchWidth(g.style), qRound(row.height())),
                               series.color, ChartSeriesStyle::Solid, g.style);
        painter.setFont(baseFont);
        painter.setPen(g.colors.legendText);
        const int textLeft = qRound(row.left()) + chartLegendSwatchWidth(g.style) +
                             kChartSwatchTextGap;
        painter.drawText(QRectF(textLeft, row.top(), row.width() / 2.0, row.height()),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         seriesDisplayName(series.name, series.fieldId));
        painter.setFont(valueFont);
        painter.setPen(g.palette.textPrimary);
        painter.drawText(row, Qt::AlignRight | Qt::AlignVCenter,
                         gaugeValueText(g.config, g.values.value(i, qQNaN())));
    }
    painter.setFont(baseFont);
}

}  // namespace

void DummyGaugeWidget::paintEvent(QPaintEvent*) {
    notePaintFrame();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const ChartStyle& style = chartStyle(effectiveStyle());
    const ChartColors colors = chartColors(style, palette);

    paintChartBackground(painter, *this, palette);

    const QVector<double> shown = easedValues(m_values, qAbs(m_config.max - m_config.min));
    const GaugePaint g{m_config, m_values, shown, m_view, style, colors, palette};
    switch (m_view.gaugeShape) {
        case ChartGaugeShape::HalfCircle:
            paintGaugeDial(painter, rect(), g, kHalfSweep);
            break;
        case ChartGaugeShape::Bar:
            paintGaugeBars(painter, rect(), g);
            break;
        case ChartGaugeShape::Number:
            paintGaugeNumbers(painter, rect(), g);
            break;
        case ChartGaugeShape::Arc:
            paintGaugeDial(painter, rect(), g, kRingSweep);
            break;
    }
}

}  // namespace traceview
