#pragma once

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include "dashboard/widgets/chartdata.h"
#include "dashboard/widgets/chartstyle.h"

class QPainter;

namespace traceview {

class DashboardWidget;

// Paint building blocks shared by every chart kind (line, bar, gauge, and
// whatever comes next). Each one takes the ChartStyle/ChartColors it should
// follow instead of hard-coding a look. A chart's paintEvent() composes
// these and only adds what is truly its own, such as the series shapes.
// docs/CHART_STYLE.md explains the layout anatomy and the paint order.

// Space between a chart's outer chrome (legend, axis labels) and the
// widget's own edge -- follows the app density (ThemeManager::
// currentDensity().contentPadding, 12px at Normal).
int chartOuterPadding();
// Tighter gap between pieces of axis chrome that belong together (a rotated
// axis title and its tick labels).
constexpr int kChartAxisLabelGap = 4;
// Horizontal gap between adjacent legend columns.
constexpr int kChartLegendItemGap = 14;
// Gap between a legend swatch and its text.
constexpr int kChartSwatchTextGap = 4;

// Fills the widget with palette.surface on the cell's rounded outline and
// strokes its decorative inner border -- see "Corner radius" in
// docs/VISUAL_IDENTITY.md.
void paintChartBackground(QPainter& painter, const DashboardWidget& widget,
                          const ThemePalette& palette);

// `base` with equal-width digits when the style asks for them, so a column
// of tick labels (and a changing value) doesn't shimmer as digits change.
QFont chartTickFont(const QFont& base, const ChartStyle& style);
// `base` with equal-width digits whatever the style (Qt 6.7+; older Qt
// keeps the font's own digits).
QFont chartTabularFont(const QFont& base);

// A 1px line drawn at a whole-pixel coordinate lands across two pixel rows
// under antialiasing; nudging it to the pixel center keeps it crisp.
qreal crispCoord(qreal coord);

// A series' dash pattern as a Qt pen style (markers map to SolidLine).
Qt::PenStyle chartPenStyle(ChartSeriesStyle style);
// Cross/Asterisk series draw one glyph per sample instead of a line.
bool isChartMarkerStyle(ChartSeriesStyle style);
// One Cross/Asterisk glyph centered on `center`, `size` px from center to
// tip, with the painter's current pen.
void paintChartMarker(QPainter& painter, const QPointF& center, ChartSeriesStyle style,
                      qreal size);

// --- Legend -------------------------------------------------------------

// One legend row's height: the taller of the swatch and the font's line.
int chartLegendRowHeight(const QFontMetrics& fm);
int chartLegendSwatchWidth(const ChartStyle& style);

struct ChartLegendEntry {
    QColor color;
    ChartSeriesStyle lineStyle = ChartSeriesStyle::Solid;
    bool hidden = false;  // grayed out, still clickable to bring it back
};

struct ChartLegendColumn {
    int x = 0;
    int width = 0;
};

// Column x-positions shared by every row of one legend, so swatches line up
// vertically across rows. Every column gets the same width -- the widest
// text of any row -- and columns stop (no wrap, no elide) once the next one
// would cross `rightBound`. `rows` holds one text per entry per row.
QVector<ChartLegendColumn> chartLegendColumns(const QFontMetrics& fm, int left, int rightBound,
                                              int entryCount, const QVector<QStringList>& rows,
                                              const ChartStyle& style);

// A dot (Dashboard) or a short line sample in the series' own dash pattern
// (Engineering/Scientific), vertically centered in `slot`.
void paintChartLegendSwatch(QPainter& painter, const QRect& slot, const QColor& color,
                            ChartSeriesStyle lineStyle, const ChartStyle& style);

void paintChartLegendRow(QPainter& painter, int y, int rowHeight,
                         const QVector<ChartLegendColumn>& columns,
                         const QVector<ChartLegendEntry>& entries, const QStringList& texts,
                         const ChartStyle& style, const ChartColors& colors,
                         const ThemePalette& palette);

// A boxed legend inside the plot, in `placement`'s corner (one of the four
// corner placements): one row per entry, on palette.surface at
// `opacityPercent` so it stays readable over the series, outlined in the
// frame color at the same opacity. Rows that don't fit the plot height are
// left out. Returns one clickable rect per entry, empty for a row left out.
QVector<QRect> paintChartInsetLegend(QPainter& painter, const QRect& plotRect,
                                     ChartLegendPlacement placement,
                                     const QVector<ChartLegendEntry>& entries,
                                     const QStringList& texts, int opacityPercent,
                                     const ChartStyle& style, const ChartColors& colors,
                                     const ThemePalette& palette);

// --- Cartesian axes -----------------------------------------------------

// Which pieces of axis chrome a cartesian chart shows -- the gear toggles a
// chart kind supports, already filtered by what it can draw.
struct CartesianChrome {
    bool xTickLabels = false;
    bool xTitle = false;
    bool yTickLabels = true;
    bool yTitle = true;
    bool showGrid = true;
    int gridDivisions = 2;  // value-axis grid in Divisions mode
    int yTickCount = 0;     // gear "Y axis ticks", 0 = the style's rule
    // Space reserved above the plot for the series names, and below it for
    // the latest values (or a bar chart's value labels). A chart whose
    // legend sits inside the plot or is hidden reserves neither, so the
    // plot takes the room.
    bool topLegendRow = true;
    bool bottomLegendRow = true;
    // Rows above everything else for live readouts (rate, level, series
    // statistics...), see paintChartInfoRow(). 0 = none.
    int infoRows = 0;
};

// One requested value axis, before layout.
struct ChartAxisRequest {
    AxisRange range;
    QString title;       // usually the unit; empty draws no title strip
    QColor labelColor;   // invalid = the style's tick label color
    int configDecimals = 0;
};

// A value axis after layout.
struct ChartValueAxis {
    ValueScale scale;
    QString title;
    QColor labelColor;
    bool primary = false;  // innermost; the only one whose gridlines are drawn
    int labelWidth = 0;    // widest tick label, 0 when labels are hidden
    int spineX = 0;        // x of this axis' spine line
};

struct ChartCartesianLayout {
    QRect area;      // the whole widget
    QRect plotRect;  // where series are drawn
    QVector<ChartValueAxis> yAxes;  // [0] primary, then stacked outward
    bool hasXScale = false;
    ValueScale xScale;
    QFont tickFont;
    int legendRowHeight = 0;
    int infoRowTop = 0;       // y of the first info row, when chrome.infoRows
    int infoRowPitch = 0;     // from one info row to the next
    int topLegendTop = 0;     // y of the top legend row (series names)
    int bottomLegendTop = 0;  // y of the bottom legend row (last values)
};

// Builds a chart scale for the X axis given the plot width and a tick
// budget -- see timeAxisScale().
using XScaleFactory = std::function<ValueScale(int plotWidthPx, int maxTicks)>;

// Reserves every margin (legend rows, axis titles, tick labels) and resolves
// every axis scale, in the only order that has no circular dependency:
// vertical margins -> plot height -> Y ticks -> Y label widths -> plot
// width -> X ticks -> right margin. `xScaleFor` is empty for charts with no
// continuous X axis (the bar chart).
ChartCartesianLayout layoutCartesianChart(const QPainter& painter, const QRect& area,
                                          const QVector<ChartAxisRequest>& yAxes,
                                          const CartesianChrome& chrome, const ChartStyle& style,
                                          const XScaleFactory& xScaleFor = {});

qreal chartValueToY(const QRect& plotRect, const ValueScale& scale, double value);
// Same mapping the line series uses: `scale.hi` on plotRect.right(), one
// full plot width to the left for `scale.lo`.
qreal chartValueToX(const QRect& plotRect, const ValueScale& scale, double value);

// Pixel x of every X gridline, dropping any that would sit on the plot's
// left or right edge (the frame, or nothing, is already there).
QVector<qreal> chartXGridPixels(const ChartCartesianLayout& layout);

// Grid, frame, every Y axis, and the X axis ticks/labels/title, in that
// order. `xTitle` is ignored unless chrome.xTitle. `categoryXs` gives tick
// marks at fixed pixel positions (bar centers) for a chart without an X
// scale; it is ignored when layout.hasXScale. `occupied`, when given,
// receives the rects of every tick label drawn.
void paintCartesianAxes(QPainter& painter, const ChartCartesianLayout& layout,
                        const CartesianChrome& chrome, const ChartStyle& style,
                        const ChartColors& colors, const QString& xTitle,
                        const QVector<qreal>& categoryXs = {},
                        QVector<QRect>* occupied = nullptr);

// --- Hand-placed ticks ----------------------------------------------------
//
// An axis whose values don't map linearly onto pixels (a log frequency
// axis) places its own ticks. Lay the chart out as usual, clear that axis'
// ValueScale ticks before paintCartesianAxes() (it still draws the frame,
// the spine and the title), then draw the ticks with these -- same marks,
// label font, offsets and collision rule as a ValueScale's.

struct ChartPixelTick {
    qreal pos = 0.0;  // x for the X axis, y for a Y axis, in widget pixels
    QString label;
};

// Gridlines at every tick strictly inside the plot. Call before
// paintCartesianAxes() so they sit under the frame, like its own grid.
void paintChartPixelGrid(QPainter& painter, const QRect& plotRect,
                         const QVector<ChartPixelTick>& xTicks,
                         const QVector<ChartPixelTick>& yTicks, const ChartStyle& style,
                         const ChartColors& colors);

// X tick marks, and the labels when chrome.xTickLabels. A label that would
// overlap its left neighbor or one of `occupied` is skipped; every label
// drawn is appended to `occupied`.
void paintChartPixelXTicks(QPainter& painter, const ChartCartesianLayout& layout,
                           const CartesianChrome& chrome, const QVector<ChartPixelTick>& ticks,
                           const ChartStyle& style, const ChartColors& colors,
                           QVector<QRect>* occupied);

// The primary Y axis' tick marks, and its labels when chrome.yTickLabels,
// right-aligned in the label column the layout reserved. A label that would
// overlap the one drawn before it or one of `occupied` is skipped; every
// label drawn is appended to `occupied`.
void paintChartPixelYTicks(QPainter& painter, const ChartCartesianLayout& layout,
                           const CartesianChrome& chrome, const QVector<ChartPixelTick>& ticks,
                           const ChartStyle& style, const ChartColors& colors,
                           QVector<QRect>* occupied);

// --- Info row -------------------------------------------------------------

// One readout of the info row: `format` holds "%1" where `value` goes.
// The value is drawn right-aligned in a slot as wide as `widestValue` (or
// the value, when longer), so the row stays put while numbers change.
struct ChartInfoField {
    QString format;
    QString value;
    QString widestValue;
};

// What starts a series' info row: its color dot and name, in a column
// `width` wide so the fields after it line up across rows.
struct ChartInfoLead {
    QColor swatch;  // invalid = no dot
    QString text;
    int width = 0;
};

// Info row `row` (0 = top), in equal-width digits: `lead`, then `left`
// fields one after another, `right` against the right edge, elided to the
// room left.
void paintChartInfoRow(QPainter& painter, const ChartCartesianLayout& layout, int row,
                       const QVector<ChartInfoField>& left, const QString& right,
                       const ChartColors& colors, const ChartInfoLead& lead = {});
// Width of a lead column holding `text` (dot included).
int chartInfoLeadWidth(const QFontMetrics& fm, const QString& text);

// --- Range markers ---------------------------------------------------------

// The A/B range markers at pixel columns `xA` and `xB`: the band between
// them lightly shaded, a dashed line each and a lettered tab on top to grab.
void paintChartRangeMarkers(QPainter& painter, const QRect& plotRect, qreal xA, qreal xB,
                            const ThemePalette& palette);
// Which marker (0 = A, 1 = B) is under `pos` -- its line or its tab, a few
// pixels either side -- or -1.
int chartRangeMarkerAt(const QRect& plotRect, qreal xA, qreal xB, const QPoint& pos);

// The X axis title for a line chart: "Samples" or "Time (s)".
QString chartTimeAxisTitle(ChartXAxisMode mode);

}  // namespace traceview
