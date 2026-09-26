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
// scale; it is ignored when layout.hasXScale.
void paintCartesianAxes(QPainter& painter, const ChartCartesianLayout& layout,
                        const CartesianChrome& chrome, const ChartStyle& style,
                        const ChartColors& colors, const QString& xTitle,
                        const QVector<qreal>& categoryXs = {});

// The X axis title for a line chart: "Samples" or "Time (s)".
QString chartTimeAxisTitle(ChartXAxisMode mode);

}  // namespace traceview
