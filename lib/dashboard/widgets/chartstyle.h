#pragma once

#include <QColor>
#include <QFlags>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

#include "dashboard/widgets/chartdata.h"
#include "dashboard/widgets/chartscale.h"
#include "dashboard/widgetviewoption.h"
#include "traceview/theme.h"

namespace traceview {

// The chart style sheet: every visual decision a chart kind makes about its
// axes, grid, frame, series lines and legend comes from one ChartStyle
// token set below, never from a literal in the widget's own paint code.
// docs/CHART_STYLE.md is the manual. It has one table per style, the layout
// anatomy, and the checklist for adding a new chart kind.
//
// Three styles, picked per widget from the header gear menu:
//   Dashboard   -- the original TraceView look: no frame, a ruler per
//                  value axis, min/mid/max labels, soft solid grid.
//   Engineering -- a lab-instrument look: closed box around the plot,
//                  ticks pointing inward on all four edges, round-number
//                  ticks, the range snapped tight to the outer ticks.
//   Scientific  -- a publication look: open left/bottom spines, ticks
//                  pointing outward, round-number ticks with a small data
//                  margin, dotted grid.

enum class ChartStyleId { Dashboard, Engineering, Scientific };

// "dashboard"/"engineering"/"scientific" -- the config["view"]["style"]
// JSON value and the gear menu choice id. FromId() falls back to Dashboard
// for anything unrecognized, same as the other id helpers in chartdata.h.
QString chartStyleIdString(ChartStyleId id);
ChartStyleId chartStyleFromId(const QString& id);
QString chartStyleDisplayName(ChartStyleId id);  // translated
QVector<ChartStyleId> allChartStyles();

// What surrounds the plot area.
//   Ruler  -- no frame; each value axis gets its own left spine with ticks,
//             shown only while the grid is on (the original look).
//   Spines -- left and bottom edges only.
//   Box    -- all four edges.
enum class ChartFrame { Ruler, Spines, Box };

enum class ChartTickDirection { Inward, Outward };

// Where ticks sit on a value axis.
//   Divisions -- the axis is cut into equal parts (gridDivisions), labeled
//                only at min/mid/max with the chart's own decimals.
//   Nice      -- round-number ticks (chartscale.h), every one labeled.
// An X axis or a gauge scale in Divisions mode switches to Nice as soon as
// its labels are shown: a label at an arbitrary fraction would print an
// awkward number such as -37.6.
enum class ChartTickPlacement { Divisions, Nice };

enum class ChartLegendSwatch { Dot, LineSample };

// Where a chart's series legend goes (gear menu "Legend:").
//   Outside   -- the original rows: names above the plot, latest values
//                below it.
//   corners   -- one boxed list inside the plot, in that corner, on a
//                surface-colored background whose opacity the gear menu
//                sets (ChartViewOptions::legendOpacity), so it stays
//                readable over the lines.
//   Hidden    -- no legend at all; the plot takes the freed space.
enum class ChartLegendPlacement { Outside, TopLeft, TopRight, BottomLeft, BottomRight, Hidden };

// A gauge's shape (gear menu "Shape:").
//   Arc        -- the original 270 degree ring per series.
//   HalfCircle -- a 180 degree speedometer, flat side down.
//   Bar        -- one horizontal bar per series.
//   Number     -- just the value(s), large.
enum class ChartGaugeShape { Arc, HalfCircle, Bar, Number };

QString chartGaugeShapeId(ChartGaugeShape shape);
ChartGaugeShape chartGaugeShapeFromId(const QString& id);

// "outside"/"topLeft"/"topRight"/"bottomLeft"/"bottomRight"/"hidden".
QString chartLegendPlacementId(ChartLegendPlacement placement);
ChartLegendPlacement chartLegendPlacementFromId(const QString& id);

struct ChartStyle {
    ChartStyleId id = ChartStyleId::Dashboard;

    // Axes
    ChartFrame frame = ChartFrame::Ruler;
    ChartTickPlacement ticks = ChartTickPlacement::Divisions;
    ChartTickDirection tickDirection = ChartTickDirection::Outward;
    int tickLength = 4;
    // Box frame only: repeat the tick marks on the right and top edges.
    bool mirrorTicks = false;
    // Distance from the spine to the edge of a tick label.
    int tickLabelOffset = 4;
    qreal frameWidth = 1.0;

    // Auto range (Nice ticks only, Divisions keeps its 5% headroom).
    // Fraction of the data span added on each side before ticks are picked.
    double rangeMargin = 0.05;
    // Grow the range out to the nearest ticks (MATLAB "axis auto") instead
    // of labeling only the ticks that fall inside it.
    bool snapRangeToTicks = false;

    // Chrome colors, see chartColors(). A strength is how much of
    // palette.textPrimary is mixed into palette.surface; a negative strength
    // means "use palette.border" instead.
    double frameStrength = -1.0;
    Qt::PenStyle gridPenStyle = Qt::SolidLine;
    double gridStrength = -1.0;
    // Tick labels, axis titles and legend text in textPrimary instead of
    // the softer textSecondary.
    bool strongLabels = false;

    // Series
    qreal lineWidth = 2.0;
    double barWidthFraction = 0.7;  // of each bar's slot
    bool barOutline = false;

    // Legend and labels
    ChartLegendSwatch legendSwatch = ChartLegendSwatch::Dot;
    bool tabularFigures = false;  // equal-width digits in tick labels

    // What the gear menu's axis toggles start at for a saved view that
    // names this style but has no value for them yet. Picking a style in
    // the menu never changes the toggles -- see withChartViewOption().
    bool defaultXAxisTitle = false;
    bool defaultYAxisTitle = true;
    bool defaultXTickLabels = false;
    bool defaultYTickLabels = true;
    bool defaultScaleLabels = false;
};

const ChartStyle& chartStyle(ChartStyleId id);

// Colors one style resolves to under the current theme -- the only place
// chart chrome picks a color. Series colors are not here: they belong to
// each series' own config.
struct ChartColors {
    QColor frame;      // plot frame, spines, tick marks
    QColor tickLabel;  // tick numbers
    QColor axisTitle;  // X/Y axis titles
    QColor grid;
    QColor legendText;
};

ChartColors chartColors(const ChartStyle& style, const ThemePalette& palette);

// Where an axis range came from -- decides whether a style may widen it.
//   Data     -- scanned from buffered samples: margins and snapping apply.
//   Declared -- a device-declared field range: used as-is.
//   Fixed    -- the user's fixed range: used as-is.
//   Empty    -- nothing buffered yet: a placeholder 0..1.
struct AxisRange {
    enum class Source { Data, Declared, Fixed, Empty };
    double lo = 0.0;
    double hi = 1.0;
    Source source = Source::Empty;
};

// The auto range of a set of series: the union of their device-declared
// ranges when every one of them declares one (authoritative, so it wins
// over however little has been sampled), else the span of everything
// buffered, else Empty. `declaredMins`/`declaredMaxs` are
// ChartSeriesConfig::declaredMin/Max in the same order as `buffers`.
AxisRange seriesDataRange(const QVector<QVector<double>>& buffers,
                          const QVector<double>& declaredMins,
                          const QVector<double>& declaredMaxs);

// A value axis ready to draw: the range samples map into, which values get a
// tick label, and where the gridlines go.
struct ValueScale {
    double lo = 0.0;
    double hi = 1.0;
    QVector<double> ticks;      // labeled ticks (and tick marks)
    QVector<double> gridTicks;  // gridlines -- the ticks, or equal divisions
    int decimals = 0;
};

// Applies a style's range and tick rules to `range`.
//   Divisions: Data gets 5% headroom (a flat line gets +-1), ticks are
//              min/mid/max, gridlines split the range into `gridDivisions`,
//              labels use `configDecimals`.
//   Nice:      Data gets style.rangeMargin (and snapRangeToTicks), Declared
//              and Fixed keep their exact ends, ticks come from niceScale()
//              with at most `maxTicks`, labels use as many decimals as the
//              tick step needs.
// `tickCount` is the gear menu's "Y axis ticks" choice (0 = the style's
// own rule above). Divisions then splits the range into tickCount-1 equal
// parts and labels every one of them; Nice uses it as its tick budget
// instead of `maxTicks`.
ValueScale valueScale(const AxisRange& range, const ChartStyle& style, int maxTicks,
                      int gridDivisions, int configDecimals, int tickCount = 0);

// Scale for a line chart's X axis, which runs from the oldest sample slot
// to "now" at 0: [-(capacity-1), 0] samples, or the same span in seconds in
// Time mode. Divisions without labels reproduces the original unlabeled
// grid (one line every ~60px, 4 to 10 lines). Otherwise the ticks are nice
// ticks, at most `maxTicks` of them.
ValueScale timeAxisScale(const ChartConfig& config, const ChartStyle& style, bool labeled,
                         int plotWidthPx, int maxTicks);

// The info row's items (gear "Info row values"), in display order: the
// window readouts ("rate", "samples", "span") and the per-series statistics
// ("min", "max", "p2p", "peak", "mean", "median", "rms").
QStringList chartInfoItemIds();
bool isChartInfoStatistic(const QString& id);
QString chartInfoItemLabel(const QString& id);  // translated menu text
QStringList defaultChartInfoItems();            // the window readouts

// Per-widget view state: the style plus every gear-menu toggle, persisted
// under the widget's config["view"] object. Chart kinds share this one
// struct and simply don't offer the options they can't use (see
// ChartViewFeatures).
struct ChartViewOptions {
    // True ("App default" in the gear menu): draw in the app-wide chart
    // style from View > Chart Style, and `style` is unused. False: this
    // chart keeps `style` whatever the app-wide one is.
    bool followAppStyle = true;
    ChartStyleId style = ChartStyleId::Dashboard;
    bool showXAxisTitle = false;
    bool showYAxisTitle = true;
    bool showXTickLabels = false;
    bool showYTickLabels = true;
    bool showScaleLabels = false;  // gauge: numbers around the arc
    bool showLastValueRow = true;
    bool showGridPointMarkers = false;
    bool showHoverCrosshair = false;
    bool showInfoRow = false;  // live readouts above the plot
    // What the info row shows, ids from chartInfoItemIds(): window readouts
    // on one row, statistics on one row per series.
    QStringList infoItems = defaultChartInfoItems();
    bool rangeMarkers = false;  // A/B markers bounding the statistics
    bool fillArea = false;      // tint the area under each line
    ChartLineInterpolation interpolation = ChartLineInterpolation::Linear;
    ChartLegendPlacement legendPlacement = ChartLegendPlacement::Outside;
    int legendOpacity = 75;  // percent, background of an in-plot legend
    int yTickCount = 0;      // 0 = the style's own rule, see valueScale()
    double lineWidth = 0.0;  // series stroke in px, 0 = style.lineWidth
    ChartGaugeShape gaugeShape = ChartGaugeShape::Arc;
};

// The fixed choices the gear menu offers for legendOpacity/yTickCount/
// lineWidth -- parseChartViewOptions() snaps anything else to the nearest
// one. 0 means "Auto" (the style's own value) where it appears.
QVector<double> chartLegendOpacityChoices();
QVector<double> chartYTickCountChoices();
QVector<double> chartLineWidthChoices();

// The series stroke to draw with: the view's own width, or the style's.
qreal effectiveLineWidth(const ChartViewOptions& options, const ChartStyle& style);

// Gear-menu option ids, also the config["view"] JSON keys.
namespace ChartViewKey {
inline constexpr char Style[] = "style";
inline constexpr char XAxisTitle[] = "xAxisTitle";
inline constexpr char YAxisTitle[] = "yAxisTitle";
inline constexpr char XTickLabels[] = "xTickLabels";
inline constexpr char YTickLabels[] = "yTickLabels";
inline constexpr char ScaleLabels[] = "scaleLabels";
inline constexpr char LastValue[] = "lastValue";
inline constexpr char GridPoints[] = "gridPoints";
inline constexpr char HoverCrosshair[] = "hoverCrosshair";
inline constexpr char InfoRow[] = "info";
inline constexpr char InfoItems[] = "infoItems";
inline constexpr char RangeMarkers[] = "markers";
inline constexpr char FillArea[] = "fillArea";
inline constexpr char Interpolation[] = "interpolation";
inline constexpr char Legend[] = "legend";
inline constexpr char LegendOpacity[] = "legendOpacity";
inline constexpr char YTicks[] = "yTicks";
inline constexpr char LineWidth[] = "lineWidth";
inline constexpr char GaugeShape[] = "gaugeShape";
}  // namespace ChartViewKey

// A missing key takes the picked style's default (axis toggles) or the
// struct default (everything else), so a dashboard saved before this
// existed renders exactly as it always did. A missing or "app" style
// follows the app-wide chart style.
ChartViewOptions parseChartViewOptions(const QJsonObject& view);
QJsonObject chartViewOptionsToJson(const ChartViewOptions& options);

// `options` with one gear-menu option changed. Every option is independent:
// picking a style changes only how things are drawn, never which things are
// shown -- same as switching the app theme.
ChartViewOptions withChartViewOption(ChartViewOptions options, const QString& id,
                                     const QVariant& value);

// Which gear-menu options a chart kind offers. The style select is always
// offered.
enum class ChartViewFeature : quint32 {
    XAxisTitle = 1u << 0,
    YAxisTitle = 1u << 1,
    XTickLabels = 1u << 2,
    YTickLabels = 1u << 3,
    ScaleLabels = 1u << 4,
    LastValue = 1u << 5,
    GridPoints = 1u << 6,
    HoverCrosshair = 1u << 7,
    Interpolation = 1u << 8,
    Legend = 1u << 9,  // placement + background opacity
    YTickCount = 1u << 10,
    LineWidth = 1u << 11,
    GaugeShape = 1u << 12,
    InfoRow = 1u << 13,
    FillArea = 1u << 14,
    InfoItems = 1u << 15,     // pick the info row's values (with InfoRow)
    RangeMarkers = 1u << 16,  // A/B markers the statistics are taken between
};
Q_DECLARE_FLAGS(ChartViewFeatures, ChartViewFeature)

// The gear menu for a chart kind offering `features`, in a fixed order:
// style, then axis options, then legend, then data overlays, then
// interpolation.
QVector<WidgetViewOption> chartViewOptionList(const ChartViewOptions& options,
                                              ChartViewFeatures features);

}  // namespace traceview

// Outside the namespace, as Qt requires: inside it, the global operator|
// it declares hides Qt's own operators (Qt 6.2 resolves Qt::CTRL | Qt::Key_1
// to int instead of QKeyCombination in any file including this header).
Q_DECLARE_OPERATORS_FOR_FLAGS(traceview::ChartViewFeatures)
