#include "chartstyle.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QtMath>
#include <cmath>

namespace traceview {

namespace {

ChartStyle makeDashboardStyle() {
    // Every field at its struct default -- ChartStyle's defaults *are* the
    // original TraceView chart look, so saved dashboards keep it unchanged.
    ChartStyle style;
    style.id = ChartStyleId::Dashboard;
    return style;
}

ChartStyle makeEngineeringStyle() {
    ChartStyle style;
    style.id = ChartStyleId::Engineering;
    style.frame = ChartFrame::Box;
    style.ticks = ChartTickPlacement::Nice;
    style.tickDirection = ChartTickDirection::Inward;
    style.tickLength = 5;
    style.mirrorTicks = true;
    style.tickLabelOffset = 5;
    style.rangeMargin = 0.0;
    style.snapRangeToTicks = true;
    style.frameStrength = 0.55;
    style.gridPenStyle = Qt::SolidLine;
    style.gridStrength = 0.12;
    style.lineWidth = 1.25;
    style.barWidthFraction = 0.8;
    style.barOutline = true;
    style.legendSwatch = ChartLegendSwatch::LineSample;
    style.tabularFigures = true;
    style.defaultXAxisTitle = true;
    style.defaultXTickLabels = true;
    style.defaultScaleLabels = true;
    return style;
}

ChartStyle makeScientificStyle() {
    ChartStyle style;
    style.id = ChartStyleId::Scientific;
    style.frame = ChartFrame::Spines;
    style.ticks = ChartTickPlacement::Nice;
    style.tickDirection = ChartTickDirection::Outward;
    style.tickLength = 4;
    style.tickLabelOffset = 8;  // clears the outward tick, then a 4px gap
    style.rangeMargin = 0.05;
    style.snapRangeToTicks = false;
    style.frameStrength = 0.8;
    style.gridPenStyle = Qt::DotLine;
    style.gridStrength = 0.3;
    style.strongLabels = true;
    style.lineWidth = 1.5;
    style.barWidthFraction = 0.8;
    style.legendSwatch = ChartLegendSwatch::LineSample;
    style.tabularFigures = true;
    style.defaultXAxisTitle = true;
    style.defaultXTickLabels = true;
    style.defaultScaleLabels = true;
    return style;
}

QColor mixColor(const QColor& over, const QColor& under, double strength) {
    const double t = qBound(0.0, strength, 1.0);
    return QColor::fromRgbF(float(over.redF() * t + under.redF() * (1.0 - t)),
                            float(over.greenF() * t + under.greenF() * (1.0 - t)),
                            float(over.blueF() * t + under.blueF() * (1.0 - t)));
}

QColor strengthColor(double strength, const ThemePalette& palette) {
    return strength < 0.0 ? palette.border
                          : mixColor(palette.textPrimary, palette.surface, strength);
}

// Gear-menu labels are translated in the "traceview::DashboardCell" context,
// where the menu's original tr() strings ("Show last value", "Interpolation:",
// ...) were written, so their existing translations keep working.
QString menuText(const char* text) {
    return QCoreApplication::translate("traceview::DashboardCell", text);
}

WidgetViewOption toggleOption(const char* id, const char* label, bool value,
                              bool startsSection = false) {
    WidgetViewOption option;
    option.id = QString::fromLatin1(id);
    option.label = menuText(label);
    option.kind = WidgetViewOption::Kind::Toggle;
    option.value = value;
    option.startsSection = startsSection;
    return option;
}

}  // namespace

QString chartStyleIdString(ChartStyleId id) {
    switch (id) {
        case ChartStyleId::Engineering:
            return QStringLiteral("engineering");
        case ChartStyleId::Scientific:
            return QStringLiteral("scientific");
        case ChartStyleId::Dashboard:
            break;
    }
    return QStringLiteral("dashboard");
}

ChartStyleId chartStyleFromId(const QString& id) {
    if (id == QLatin1String("engineering")) {
        return ChartStyleId::Engineering;
    }
    if (id == QLatin1String("scientific")) {
        return ChartStyleId::Scientific;
    }
    return ChartStyleId::Dashboard;
}

QString chartStyleDisplayName(ChartStyleId id) {
    switch (id) {
        case ChartStyleId::Engineering:
            return QCoreApplication::translate("ChartStyle", "Engineering");
        case ChartStyleId::Scientific:
            return QCoreApplication::translate("ChartStyle", "Scientific");
        case ChartStyleId::Dashboard:
            break;
    }
    return QCoreApplication::translate("ChartStyle", "Dashboard");
}

QVector<ChartStyleId> allChartStyles() {
    return {ChartStyleId::Dashboard, ChartStyleId::Engineering, ChartStyleId::Scientific};
}

namespace {

struct PlacementId {
    ChartLegendPlacement placement;
    const char* id;
};

constexpr PlacementId kPlacementIds[] = {
    {ChartLegendPlacement::Outside, "outside"},
    {ChartLegendPlacement::TopLeft, "topLeft"},
    {ChartLegendPlacement::TopRight, "topRight"},
    {ChartLegendPlacement::BottomLeft, "bottomLeft"},
    {ChartLegendPlacement::BottomRight, "bottomRight"},
    {ChartLegendPlacement::Hidden, "hidden"},
};

// The allowed value closest to `value`, so a hand-edited project file
// can't put the menu into a state it has no entry for.
double nearestChoice(double value, const QVector<double>& choices) {
    double best = choices.first();
    for (double choice : choices) {
        if (qAbs(choice - value) < qAbs(best - value)) {
            best = choice;
        }
    }
    return best;
}

// Menu ids for numeric choices: "0.5", "2", ... (no trailing zeros).
QString numberId(double value) {
    return QString::number(value, 'g', 6);
}

}  // namespace

QString chartGaugeShapeId(ChartGaugeShape shape) {
    switch (shape) {
        case ChartGaugeShape::HalfCircle:
            return QStringLiteral("halfCircle");
        case ChartGaugeShape::Bar:
            return QStringLiteral("bar");
        case ChartGaugeShape::Number:
            return QStringLiteral("number");
        case ChartGaugeShape::Arc:
            break;
    }
    return QStringLiteral("arc");
}

ChartGaugeShape chartGaugeShapeFromId(const QString& id) {
    for (ChartGaugeShape shape : {ChartGaugeShape::Arc, ChartGaugeShape::HalfCircle,
                                  ChartGaugeShape::Bar, ChartGaugeShape::Number}) {
        if (chartGaugeShapeId(shape) == id) {
            return shape;
        }
    }
    return ChartGaugeShape::Arc;
}

QString chartLegendPlacementId(ChartLegendPlacement placement) {
    for (const PlacementId& entry : kPlacementIds) {
        if (entry.placement == placement) {
            return QString::fromLatin1(entry.id);
        }
    }
    return QStringLiteral("outside");
}

ChartLegendPlacement chartLegendPlacementFromId(const QString& id) {
    for (const PlacementId& entry : kPlacementIds) {
        if (id == QLatin1String(entry.id)) {
            return entry.placement;
        }
    }
    return ChartLegendPlacement::Outside;
}

QVector<double> chartLegendOpacityChoices() {
    return {0, 25, 50, 75, 100};
}

QVector<double> chartYTickCountChoices() {
    // 3/5/6/11 ticks = 2/4/5/10 equal parts: round steps for the common
    // 0..100 and 0..1 ranges in the Dashboard style's Divisions mode.
    return {0, 3, 5, 6, 11};
}

QVector<double> chartLineWidthChoices() {
    return {0, 0.5, 1, 1.5, 2, 3, 4};
}

qreal effectiveLineWidth(const ChartViewOptions& options, const ChartStyle& style) {
    return options.lineWidth > 0.0 ? options.lineWidth : style.lineWidth;
}

const ChartStyle& chartStyle(ChartStyleId id) {
    static const ChartStyle dashboard = makeDashboardStyle();
    static const ChartStyle engineering = makeEngineeringStyle();
    static const ChartStyle scientific = makeScientificStyle();
    switch (id) {
        case ChartStyleId::Engineering:
            return engineering;
        case ChartStyleId::Scientific:
            return scientific;
        case ChartStyleId::Dashboard:
            break;
    }
    return dashboard;
}

ChartColors chartColors(const ChartStyle& style, const ThemePalette& palette) {
    ChartColors colors;
    colors.frame = strengthColor(style.frameStrength, palette);
    colors.grid = strengthColor(style.gridStrength, palette);
    const QColor text = style.strongLabels ? palette.textPrimary : palette.textSecondary;
    colors.tickLabel = text;
    colors.axisTitle = text;
    colors.legendText = text;
    return colors;
}

AxisRange seriesDataRange(const QVector<QVector<double>>& buffers,
                          const QVector<double>& declaredMins,
                          const QVector<double>& declaredMaxs) {
    AxisRange range;
    bool allDeclared = !buffers.isEmpty();
    for (int i = 0; allDeclared && i < buffers.size(); ++i) {
        const double dMin = i < declaredMins.size() ? declaredMins[i] : qQNaN();
        const double dMax = i < declaredMaxs.size() ? declaredMaxs[i] : qQNaN();
        if (qIsNaN(dMin) || qIsNaN(dMax)) {
            allDeclared = false;
            break;
        }
        range.lo = (i == 0) ? dMin : qMin(range.lo, dMin);
        range.hi = (i == 0) ? dMax : qMax(range.hi, dMax);
    }
    if (allDeclared) {
        range.source = AxisRange::Source::Declared;
        return range;
    }

    bool any = false;
    for (const QVector<double>& buffer : buffers) {
        for (double value : buffer) {
            if (!any) {
                range.lo = range.hi = value;
                any = true;
            } else {
                range.lo = qMin(range.lo, value);
                range.hi = qMax(range.hi, value);
            }
        }
    }
    if (!any) {
        return AxisRange{};
    }
    range.source = AxisRange::Source::Data;
    return range;
}

ValueScale valueScale(const AxisRange& range, const ChartStyle& style, int maxTicks,
                      int gridDivisions, int configDecimals, int tickCount) {
    ValueScale scale;
    double lo = range.lo;
    double hi = range.hi;

    if (style.ticks == ChartTickPlacement::Divisions) {
        switch (range.source) {
            case AxisRange::Source::Empty:
                lo = 0.0;
                hi = 1.0;
                break;
            case AxisRange::Source::Data:
                if (qFuzzyCompare(lo, hi)) {
                    lo -= 1.0;
                    hi += 1.0;
                } else {
                    const double pad = (hi - lo) * 0.05;
                    lo -= pad;
                    hi += pad;
                }
                break;
            case AxisRange::Source::Declared:
            case AxisRange::Source::Fixed:
                hi = qMax(hi, lo + 1e-6);
                break;
        }
        scale.lo = lo;
        scale.hi = hi;
        const int divisions = tickCount >= 2 ? tickCount - 1 : qMax(1, gridDivisions);
        scale.gridTicks.reserve(divisions + 1);
        for (int i = 0; i <= divisions; ++i) {
            scale.gridTicks.append(lo + (hi - lo) * i / divisions);
        }
        scale.ticks =
            tickCount >= 2 ? scale.gridTicks : QVector<double>{lo, (lo + hi) / 2.0, hi};
        scale.decimals = configDecimals;
        // More ticks than min/mid/max sit closer together: print at least
        // enough digits to tell neighbors apart (-1 / -0.5 / 0, not -1 / -1 /
        // 0), capped so an awkward step can't produce a long tail.
        const double step = (hi - lo) / divisions;
        if (tickCount >= 2 && step > 0.0 && step < 1.0) {
            const int needed = int(std::ceil(-std::log10(step) - 1e-9));
            scale.decimals = qMax(configDecimals, qMin(needed, 4));
        }
        return scale;
    }

    bool expand = false;
    switch (range.source) {
        case AxisRange::Source::Empty:
            lo = 0.0;
            hi = 1.0;
            expand = true;
            break;
        case AxisRange::Source::Data: {
            if (qFuzzyCompare(lo, hi)) {
                lo -= 1.0;
                hi += 1.0;
            }
            const double pad = (hi - lo) * style.rangeMargin;
            lo -= pad;
            hi += pad;
            expand = style.snapRangeToTicks;
            break;
        }
        case AxisRange::Source::Declared:
        case AxisRange::Source::Fixed:
            break;
    }
    const NiceScale nice = niceScale(lo, hi, tickCount >= 2 ? tickCount : maxTicks, expand);
    scale.lo = nice.lo;
    scale.hi = nice.hi;
    scale.ticks = nice.ticks;
    scale.gridTicks = nice.ticks;
    scale.decimals = nice.decimals;
    return scale;
}

// Pixel spacing and line-count bounds of the unlabeled Divisions X grid --
// the values the line chart always used, so the Dashboard style is
// unchanged.
constexpr int kXGridTargetSpacingPx = 60;
constexpr int kXGridMinLines = 4;
constexpr int kXGridMaxLines = 10;

ValueScale timeAxisScale(const ChartConfig& config, const ChartStyle& style, bool labeled,
                         int plotWidthPx, int maxTicks) {
    const int capacity = chartBufferCapacity(config);
    double span = double(qMax(1, capacity - 1));
    if (config.xAxisMode == ChartXAxisMode::Time) {
        span *= config.sampleTimeMs / 1000.0;
    }
    if (span <= 0.0) {
        span = 1.0;
    }

    ValueScale scale;
    scale.lo = -span;
    scale.hi = 0.0;
    if (style.ticks == ChartTickPlacement::Divisions && !labeled) {
        const int lineCount =
            qBound(kXGridMinLines, plotWidthPx / kXGridTargetSpacingPx, kXGridMaxLines);
        for (int i = 1; i < lineCount; ++i) {
            scale.gridTicks.append(scale.lo + span * i / lineCount);
        }
        return scale;
    }

    const NiceScale nice = niceScale(scale.lo, scale.hi, maxTicks, /*expand=*/false);
    scale.ticks = nice.ticks;
    scale.gridTicks = nice.ticks;
    scale.decimals = nice.decimals;
    return scale;
}

QStringList chartInfoItemIds() {
    return {QStringLiteral("rate"), QStringLiteral("samples"), QStringLiteral("span"),
            QStringLiteral("min"),  QStringLiteral("max"),     QStringLiteral("p2p"),
            QStringLiteral("peak"), QStringLiteral("mean"),    QStringLiteral("median"),
            QStringLiteral("rms")};
}

bool isChartInfoStatistic(const QString& id) {
    return chartInfoItemIds().indexOf(id) >= 3;
}

QString chartInfoItemLabel(const QString& id) {
    static const QHash<QString, const char*> labels = {
        {QStringLiteral("rate"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Sample rate")},
        {QStringLiteral("samples"),
         QT_TRANSLATE_NOOP("traceview::DashboardCell", "Samples in window")},
        {QStringLiteral("span"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Window span")},
        {QStringLiteral("min"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Minimum")},
        {QStringLiteral("max"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Maximum")},
        {QStringLiteral("p2p"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Peak to peak")},
        {QStringLiteral("peak"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Peak (|x| max)")},
        {QStringLiteral("mean"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Mean")},
        {QStringLiteral("median"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "Median")},
        {QStringLiteral("rms"), QT_TRANSLATE_NOOP("traceview::DashboardCell", "RMS")},
    };
    const char* text = labels.value(id);
    return text != nullptr ? menuText(text) : id;
}

QStringList defaultChartInfoItems() {
    return {QStringLiteral("rate"), QStringLiteral("samples"), QStringLiteral("span")};
}

// config["view"]["style"] value meaning "follow the app-wide chart style".
constexpr char kAppStyleId[] = "app";

ChartViewOptions parseChartViewOptions(const QJsonObject& view) {
    ChartViewOptions options;
    const QString styleId = view.value(ChartViewKey::Style).toString();
    options.followAppStyle = styleId.isEmpty() || styleId == QLatin1String(kAppStyleId);
    options.style = chartStyleFromId(styleId);
    const ChartStyle& style = chartStyle(options.style);
    options.showXAxisTitle = view.value(ChartViewKey::XAxisTitle).toBool(style.defaultXAxisTitle);
    options.showYAxisTitle = view.value(ChartViewKey::YAxisTitle).toBool(style.defaultYAxisTitle);
    options.showXTickLabels =
        view.value(ChartViewKey::XTickLabels).toBool(style.defaultXTickLabels);
    options.showYTickLabels =
        view.value(ChartViewKey::YTickLabels).toBool(style.defaultYTickLabels);
    options.showScaleLabels =
        view.value(ChartViewKey::ScaleLabels).toBool(style.defaultScaleLabels);
    options.showLastValueRow = view.value(ChartViewKey::LastValue).toBool(true);
    options.showGridPointMarkers = view.value(ChartViewKey::GridPoints).toBool(false);
    options.showHoverCrosshair = view.value(ChartViewKey::HoverCrosshair).toBool(false);
    options.showInfoRow = view.value(ChartViewKey::InfoRow).toBool(false);
    if (view.contains(ChartViewKey::InfoItems)) {
        options.infoItems.clear();
        const QJsonArray items = view.value(ChartViewKey::InfoItems).toArray();
        // Kept in menu order, unknown ids dropped.
        for (const QString& id : chartInfoItemIds()) {
            if (items.contains(id)) {
                options.infoItems << id;
            }
        }
    }
    options.rangeMarkers = view.value(ChartViewKey::RangeMarkers).toBool(false);
    options.fillArea = view.value(ChartViewKey::FillArea).toBool(false);
    options.interpolation =
        chartLineInterpolationFromId(view.value(ChartViewKey::Interpolation).toString());
    options.legendPlacement =
        chartLegendPlacementFromId(view.value(ChartViewKey::Legend).toString());
    options.legendOpacity = int(nearestChoice(view.value(ChartViewKey::LegendOpacity).toDouble(75),
                                              chartLegendOpacityChoices()));
    options.yTickCount = int(
        nearestChoice(view.value(ChartViewKey::YTicks).toDouble(0), chartYTickCountChoices()));
    options.lineWidth =
        nearestChoice(view.value(ChartViewKey::LineWidth).toDouble(0), chartLineWidthChoices());
    options.gaugeShape =
        chartGaugeShapeFromId(view.value(ChartViewKey::GaugeShape).toString());
    return options;
}

QJsonObject chartViewOptionsToJson(const ChartViewOptions& options) {
    QJsonObject view;
    view[ChartViewKey::Style] = options.followAppStyle ? QString::fromLatin1(kAppStyleId)
                                                       : chartStyleIdString(options.style);
    view[ChartViewKey::XAxisTitle] = options.showXAxisTitle;
    view[ChartViewKey::YAxisTitle] = options.showYAxisTitle;
    view[ChartViewKey::XTickLabels] = options.showXTickLabels;
    view[ChartViewKey::YTickLabels] = options.showYTickLabels;
    view[ChartViewKey::ScaleLabels] = options.showScaleLabels;
    view[ChartViewKey::LastValue] = options.showLastValueRow;
    view[ChartViewKey::GridPoints] = options.showGridPointMarkers;
    view[ChartViewKey::HoverCrosshair] = options.showHoverCrosshair;
    view[ChartViewKey::InfoRow] = options.showInfoRow;
    view[ChartViewKey::InfoItems] = QJsonArray::fromStringList(options.infoItems);
    view[ChartViewKey::RangeMarkers] = options.rangeMarkers;
    view[ChartViewKey::FillArea] = options.fillArea;
    view[ChartViewKey::Interpolation] = chartLineInterpolationId(options.interpolation);
    view[ChartViewKey::Legend] = chartLegendPlacementId(options.legendPlacement);
    view[ChartViewKey::LegendOpacity] = options.legendOpacity;
    view[ChartViewKey::YTicks] = options.yTickCount;
    view[ChartViewKey::LineWidth] = options.lineWidth;
    view[ChartViewKey::GaugeShape] = chartGaugeShapeId(options.gaugeShape);
    return view;
}

ChartViewOptions withChartViewOption(ChartViewOptions options, const QString& id,
                                     const QVariant& value) {
    if (id == QLatin1String(ChartViewKey::Style)) {
        options.followAppStyle = value.toString() == QLatin1String(kAppStyleId);
        options.style = chartStyleFromId(value.toString());
    } else if (id == QLatin1String(ChartViewKey::XAxisTitle)) {
        options.showXAxisTitle = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::YAxisTitle)) {
        options.showYAxisTitle = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::XTickLabels)) {
        options.showXTickLabels = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::YTickLabels)) {
        options.showYTickLabels = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::ScaleLabels)) {
        options.showScaleLabels = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::LastValue)) {
        options.showLastValueRow = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::GridPoints)) {
        options.showGridPointMarkers = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::HoverCrosshair)) {
        options.showHoverCrosshair = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::InfoRow)) {
        options.showInfoRow = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::InfoItems)) {
        const QStringList picked = value.toStringList();
        options.infoItems.clear();
        for (const QString& item : chartInfoItemIds()) {
            if (picked.contains(item)) {
                options.infoItems << item;
            }
        }
    } else if (id == QLatin1String(ChartViewKey::RangeMarkers)) {
        options.rangeMarkers = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::FillArea)) {
        options.fillArea = value.toBool();
    } else if (id == QLatin1String(ChartViewKey::Interpolation)) {
        options.interpolation = chartLineInterpolationFromId(value.toString());
    } else if (id == QLatin1String(ChartViewKey::Legend)) {
        options.legendPlacement = chartLegendPlacementFromId(value.toString());
    } else if (id == QLatin1String(ChartViewKey::LegendOpacity)) {
        options.legendOpacity = int(nearestChoice(value.toDouble(), chartLegendOpacityChoices()));
    } else if (id == QLatin1String(ChartViewKey::YTicks)) {
        options.yTickCount = int(nearestChoice(value.toDouble(), chartYTickCountChoices()));
    } else if (id == QLatin1String(ChartViewKey::LineWidth)) {
        options.lineWidth = nearestChoice(value.toDouble(), chartLineWidthChoices());
    } else if (id == QLatin1String(ChartViewKey::GaugeShape)) {
        options.gaugeShape = chartGaugeShapeFromId(value.toString());
    }
    return options;
}

QVector<WidgetViewOption> chartViewOptionList(const ChartViewOptions& options,
                                              ChartViewFeatures features) {
    // Marked for lupdate in the context menuText() looks them up in (spelled
    // out each time: lupdate does not expand a wrapper macro).
    static constexpr const char* kStyle =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Style:");
    static constexpr const char* kAppStyle =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "App default");
    static constexpr const char* kXAxisTitle =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show X axis title");
    static constexpr const char* kXTickLabels =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show X axis values");
    static constexpr const char* kYAxisTitle =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show Y axis title");
    static constexpr const char* kYTickLabels =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show Y axis values");
    static constexpr const char* kScaleLabels =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show scale values");
    static constexpr const char* kLastValue =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show last value");
    static constexpr const char* kGridPoints =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show grid point values");
    static constexpr const char* kInfoRow =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show info row");
    static constexpr const char* kInfoItems =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Info row values");
    static constexpr const char* kRangeMarkers =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Range markers (A/B)");
    static constexpr const char* kFillArea =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Fill area under the line");
    static constexpr const char* kHoverCrosshair =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Show hover crosshair");
    static constexpr const char* kInterpolation =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Interpolation:");
    static constexpr const char* kLinear =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Linear");
    static constexpr const char* kZoh =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "ZOH (step)");
    static constexpr const char* kStem =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Stem");
    static constexpr const char* kNone =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "None (points)");
    static constexpr const char* kYTicks =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Y axis ticks:");
    static constexpr const char* kAuto = QT_TRANSLATE_NOOP("traceview::DashboardCell", "Auto");
    static constexpr const char* kLegend =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Legend:");
    static constexpr const char* kOutside =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Outside the plot");
    static constexpr const char* kTopLeft =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Top left");
    static constexpr const char* kTopRight =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Top right");
    static constexpr const char* kBottomLeft =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Bottom left");
    static constexpr const char* kBottomRight =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Bottom right");
    static constexpr const char* kHidden = QT_TRANSLATE_NOOP("traceview::DashboardCell", "Hidden");
    static constexpr const char* kLegendOpacity =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Legend background:");
    static constexpr const char* kPercent = QT_TRANSLATE_NOOP("traceview::DashboardCell", "%1%");
    static constexpr const char* kLineWidth =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Line width:");
    static constexpr const char* kPixels = QT_TRANSLATE_NOOP("traceview::DashboardCell", "%1 px");
    static constexpr const char* kShape = QT_TRANSLATE_NOOP("traceview::DashboardCell", "Shape:");
    static constexpr const char* kShapeArc =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Ring (270°)");
    static constexpr const char* kShapeHalf =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Half circle");
    static constexpr const char* kShapeBar = QT_TRANSLATE_NOOP("traceview::DashboardCell", "Bar");
    static constexpr const char* kShapeNumber =
        QT_TRANSLATE_NOOP("traceview::DashboardCell", "Number");

    QVector<WidgetViewOption> list;

    WidgetViewOption style;
    style.id = QString::fromLatin1(ChartViewKey::Style);
    style.label = menuText(kStyle);
    style.kind = WidgetViewOption::Kind::Choice;
    style.value = options.followAppStyle ? QString::fromLatin1(kAppStyleId)
                                         : chartStyleIdString(options.style);
    style.choices.append({QString::fromLatin1(kAppStyleId), menuText(kAppStyle)});
    for (ChartStyleId id : allChartStyles()) {
        style.choices.append({chartStyleIdString(id), chartStyleDisplayName(id)});
    }
    list.append(style);

    if (features.testFlag(ChartViewFeature::GaugeShape)) {
        WidgetViewOption shape;
        shape.id = QString::fromLatin1(ChartViewKey::GaugeShape);
        shape.label = menuText(kShape);
        shape.kind = WidgetViewOption::Kind::Choice;
        shape.value = chartGaugeShapeId(options.gaugeShape);
        shape.choices = {
            {chartGaugeShapeId(ChartGaugeShape::Arc), menuText(kShapeArc)},
            {chartGaugeShapeId(ChartGaugeShape::HalfCircle), menuText(kShapeHalf)},
            {chartGaugeShapeId(ChartGaugeShape::Bar), menuText(kShapeBar)},
            {chartGaugeShapeId(ChartGaugeShape::Number), menuText(kShapeNumber)},
        };
        list.append(shape);
    }

    // Each group's first option present draws the separator above it.
    bool sectionOpen = true;
    auto add = [&](ChartViewFeature feature, WidgetViewOption option) {
        if (!features.testFlag(feature)) {
            return;
        }
        option.startsSection = sectionOpen;
        sectionOpen = false;
        list.append(option);
    };

    add(ChartViewFeature::XAxisTitle,
        toggleOption(ChartViewKey::XAxisTitle, kXAxisTitle, options.showXAxisTitle));
    add(ChartViewFeature::XTickLabels,
        toggleOption(ChartViewKey::XTickLabels, kXTickLabels, options.showXTickLabels));
    add(ChartViewFeature::YAxisTitle,
        toggleOption(ChartViewKey::YAxisTitle, kYAxisTitle, options.showYAxisTitle));
    add(ChartViewFeature::YTickLabels,
        toggleOption(ChartViewKey::YTickLabels, kYTickLabels, options.showYTickLabels));
    add(ChartViewFeature::ScaleLabels,
        toggleOption(ChartViewKey::ScaleLabels, kScaleLabels, options.showScaleLabels));

    WidgetViewOption yTicks;
    yTicks.id = QString::fromLatin1(ChartViewKey::YTicks);
    yTicks.label = menuText(kYTicks);
    yTicks.kind = WidgetViewOption::Kind::Choice;
    yTicks.value = numberId(options.yTickCount);
    for (double count : chartYTickCountChoices()) {
        yTicks.choices.append({numberId(count), count == 0 ? menuText(kAuto) : numberId(count)});
    }
    add(ChartViewFeature::YTickCount, yTicks);

    sectionOpen = true;
    WidgetViewOption legend;
    legend.id = QString::fromLatin1(ChartViewKey::Legend);
    legend.label = menuText(kLegend);
    legend.kind = WidgetViewOption::Kind::Choice;
    legend.value = chartLegendPlacementId(options.legendPlacement);
    legend.choices = {
        {chartLegendPlacementId(ChartLegendPlacement::Outside), menuText(kOutside)},
        {chartLegendPlacementId(ChartLegendPlacement::TopLeft), menuText(kTopLeft)},
        {chartLegendPlacementId(ChartLegendPlacement::TopRight), menuText(kTopRight)},
        {chartLegendPlacementId(ChartLegendPlacement::BottomLeft), menuText(kBottomLeft)},
        {chartLegendPlacementId(ChartLegendPlacement::BottomRight), menuText(kBottomRight)},
        {chartLegendPlacementId(ChartLegendPlacement::Hidden), menuText(kHidden)},
    };
    add(ChartViewFeature::Legend, legend);

    WidgetViewOption opacity;
    opacity.id = QString::fromLatin1(ChartViewKey::LegendOpacity);
    opacity.label = menuText(kLegendOpacity);
    opacity.kind = WidgetViewOption::Kind::Choice;
    opacity.value = numberId(options.legendOpacity);
    for (double percent : chartLegendOpacityChoices()) {
        opacity.choices.append({numberId(percent), menuText(kPercent).arg(percent)});
    }
    add(ChartViewFeature::Legend, opacity);

    sectionOpen = true;
    add(ChartViewFeature::LastValue,
        toggleOption(ChartViewKey::LastValue, kLastValue, options.showLastValueRow));
    add(ChartViewFeature::GridPoints,
        toggleOption(ChartViewKey::GridPoints, kGridPoints, options.showGridPointMarkers));
    add(ChartViewFeature::HoverCrosshair,
        toggleOption(ChartViewKey::HoverCrosshair, kHoverCrosshair, options.showHoverCrosshair));
    add(ChartViewFeature::InfoRow,
        toggleOption(ChartViewKey::InfoRow, kInfoRow, options.showInfoRow));
    WidgetViewOption infoItems;
    infoItems.id = QString::fromLatin1(ChartViewKey::InfoItems);
    infoItems.label = menuText(kInfoItems);
    infoItems.kind = WidgetViewOption::Kind::MultiChoice;
    infoItems.value = options.infoItems;
    for (const QString& item : chartInfoItemIds()) {
        infoItems.choices.append({item, chartInfoItemLabel(item)});
    }
    add(ChartViewFeature::InfoItems, infoItems);
    add(ChartViewFeature::RangeMarkers,
        toggleOption(ChartViewKey::RangeMarkers, kRangeMarkers, options.rangeMarkers));
    add(ChartViewFeature::FillArea,
        toggleOption(ChartViewKey::FillArea, kFillArea, options.fillArea));

    sectionOpen = true;
    WidgetViewOption lineWidth;
    lineWidth.id = QString::fromLatin1(ChartViewKey::LineWidth);
    lineWidth.label = menuText(kLineWidth);
    lineWidth.kind = WidgetViewOption::Kind::Choice;
    lineWidth.value = numberId(options.lineWidth);
    for (double width : chartLineWidthChoices()) {
        lineWidth.choices.append(
            {numberId(width), width == 0 ? menuText(kAuto) : menuText(kPixels).arg(width)});
    }
    add(ChartViewFeature::LineWidth, lineWidth);

    WidgetViewOption interpolation;
    interpolation.id = QString::fromLatin1(ChartViewKey::Interpolation);
    interpolation.label = menuText(kInterpolation);
    interpolation.kind = WidgetViewOption::Kind::Choice;
    interpolation.value = chartLineInterpolationId(options.interpolation);
    interpolation.choices = {
        {QStringLiteral("linear"), menuText(kLinear)},
        {QStringLiteral("zoh"), menuText(kZoh)},
        {QStringLiteral("stem"), menuText(kStem)},
        {QStringLiteral("none"), menuText(kNone)},
    };
    add(ChartViewFeature::Interpolation, interpolation);

    return list;
}

}  // namespace traceview
