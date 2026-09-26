#include "traceview/appearance.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QtMath>
#include <cmath>
#include <utility>

namespace traceview {

namespace {

// Generic id <-> enum lookup over a {id, "string"} table.
template <typename Enum, size_t N>
QString idOf(const std::pair<Enum, const char*> (&table)[N], Enum value) {
    for (const auto& entry : table) {
        if (entry.first == value) {
            return QString::fromLatin1(entry.second);
        }
    }
    return QString::fromLatin1(table[0].second);
}

template <typename Enum, size_t N>
Enum fromIdIn(const std::pair<Enum, const char*> (&table)[N], const QString& id) {
    for (const auto& entry : table) {
        if (id == QLatin1String(entry.second)) {
            return entry.first;
        }
    }
    return table[0].first;  // the first entry is each option's default
}

template <typename Enum, size_t N>
QVector<Enum> allIn(const std::pair<Enum, const char*> (&table)[N]) {
    QVector<Enum> values;
    for (const auto& entry : table) {
        values.append(entry.first);
    }
    return values;
}

const std::pair<DataColorsId, const char*> kDataColorIds[] = {
    {DataColorsId::Custom, "custom"},     {DataColorsId::Palette, "palette"},
    {DataColorsId::Matlab, "matlab"},     {DataColorsId::Tableau, "tableau"},
    {DataColorsId::OkabeIto, "okabeIto"}, {DataColorsId::Monochrome, "monochrome"},
};
const std::pair<DensityId, const char*> kDensityIds[] = {
    {DensityId::Normal, "normal"},
    {DensityId::Compact, "compact"},
    {DensityId::Comfortable, "comfortable"},
};
const std::pair<CardHeaderId, const char*> kCardHeaderIds[] = {
    {CardHeaderId::Filled, "filled"},
    {CardHeaderId::Line, "line"},
    {CardHeaderId::Hover, "hover"},
};
const std::pair<CanvasId, const char*> kCanvasIds[] = {
    {CanvasId::Plain, "plain"},
    {CanvasId::Dots, "dots"},
    {CanvasId::Grid, "grid"},
    {CanvasId::Gradient, "gradient"},
};
const std::pair<MotionId, const char*> kMotionIds[] = {
    {MotionId::Animated, "animated"},
    {MotionId::Reduced, "reduced"},
};

QVector<QColor> hexColors(std::initializer_list<const char*> hex) {
    QVector<QColor> colors;
    for (const char* value : hex) {
        colors.append(QColor(QString::fromLatin1(value)));
    }
    return colors;
}

double relativeLuminance(const QColor& color) {
    auto channel = [](double c) {
        return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
           0.0722 * channel(color.blueF());
}

// Palette fields in JSON order, shared by paletteToJson()/paletteFromJson().
struct PaletteField {
    const char* key;
    QColor ThemePalette::*member;
};

constexpr PaletteField kPaletteFields[] = {
    {"background", &ThemePalette::background},
    {"surface", &ThemePalette::surface},
    {"surfaceAlt", &ThemePalette::surfaceAlt},
    {"border", &ThemePalette::border},
    {"borderStrong", &ThemePalette::borderStrong},
    {"textPrimary", &ThemePalette::textPrimary},
    {"textSecondary", &ThemePalette::textSecondary},
    {"textDisabled", &ThemePalette::textDisabled},
    {"accent", &ThemePalette::accent},
    {"accentHover", &ThemePalette::accentHover},
    {"accentPressed", &ThemePalette::accentPressed},
    {"success", &ThemePalette::success},
    {"warning", &ThemePalette::warning},
    {"danger", &ThemePalette::danger},
};

}  // namespace

// --- Data colors ----------------------------------------------------------

QString dataColorsIdString(DataColorsId id) {
    return idOf(kDataColorIds, id);
}

DataColorsId dataColorsFromId(const QString& id) {
    return fromIdIn(kDataColorIds, id);
}

QString dataColorsDisplayName(DataColorsId id) {
    switch (id) {
        case DataColorsId::Palette:
            return QCoreApplication::translate("Appearance", "Palette colors");
        case DataColorsId::Matlab:
            return QCoreApplication::translate("Appearance", "MATLAB");
        case DataColorsId::Tableau:
            return QCoreApplication::translate("Appearance", "Tableau");
        case DataColorsId::OkabeIto:
            return QCoreApplication::translate("Appearance", "Color-blind safe");
        case DataColorsId::Monochrome:
            return QCoreApplication::translate("Appearance", "Monochrome");
        case DataColorsId::Custom:
            break;
    }
    return QCoreApplication::translate("Appearance", "Per series (as configured)");
}

QVector<DataColorsId> allDataColors() {
    return allIn(kDataColorIds);
}

QColor dataSeriesColor(DataColorsId scheme, int index, const QColor& own,
                       const ThemePalette& palette) {
    static const QVector<QColor> matlab =
        hexColors({"#0072BD", "#D95319", "#EDB120", "#7E2F8E", "#77AC30", "#4DBEEE", "#A2142F"});
    static const QVector<QColor> tableau =
        hexColors({"#1F77B4", "#FF7F0E", "#2CA02C", "#D62728", "#9467BD", "#8C564B", "#E377C2",
                   "#7F7F7F", "#BCBD22", "#17BECF"});
    static const QVector<QColor> okabeIto =
        hexColors({"#0072B2", "#E69F00", "#009E73", "#CC79A7", "#56B4E9", "#D55E00", "#F0E442",
                   "#999999"});
    index = qMax(0, index);
    switch (scheme) {
        case DataColorsId::Palette:
            return palette.series.isEmpty() ? own : palette.series[index % palette.series.size()];
        case DataColorsId::Matlab:
            return matlab[index % matlab.size()];
        case DataColorsId::Tableau:
            return tableau[index % tableau.size()];
        case DataColorsId::OkabeIto:
            return okabeIto[index % okabeIto.size()];
        case DataColorsId::Monochrome: {
            // Alternating lighter/darker steps around the accent, so
            // neighbors stay apart: 0, +1, -1, +2, -2 ...
            constexpr int kSteps[] = {100, 140, 70, 175, 50, 210};
            const int factor = kSteps[index % 6];
            return factor >= 100 ? palette.accent.lighter(factor)
                                 : palette.accent.darker(10000 / factor);
        }
        case DataColorsId::Custom:
            break;
    }
    return own;
}

// --- Density --------------------------------------------------------------

QString densityIdString(DensityId id) {
    return idOf(kDensityIds, id);
}

DensityId densityFromId(const QString& id) {
    return fromIdIn(kDensityIds, id);
}

QString densityDisplayName(DensityId id) {
    switch (id) {
        case DensityId::Compact:
            return QCoreApplication::translate("Appearance", "Compact");
        case DensityId::Comfortable:
            return QCoreApplication::translate("Appearance", "Comfortable");
        case DensityId::Normal:
            break;
    }
    return QCoreApplication::translate("Appearance", "Normal");
}

QVector<DensityId> allDensities() {
    return {DensityId::Compact, DensityId::Normal, DensityId::Comfortable};
}

const Density& density(DensityId id) {
    static const Density compact{DensityId::Compact, 20, 8, 0.5};
    static const Density normal{};
    static const Density comfortable{DensityId::Comfortable, 30, 16, 1.6};
    switch (id) {
        case DensityId::Compact:
            return compact;
        case DensityId::Comfortable:
            return comfortable;
        case DensityId::Normal:
            break;
    }
    return normal;
}

// --- Card header ----------------------------------------------------------

QString cardHeaderIdString(CardHeaderId id) {
    return idOf(kCardHeaderIds, id);
}

CardHeaderId cardHeaderFromId(const QString& id) {
    return fromIdIn(kCardHeaderIds, id);
}

QString cardHeaderDisplayName(CardHeaderId id) {
    switch (id) {
        case CardHeaderId::Line:
            return QCoreApplication::translate("Appearance", "Line");
        case CardHeaderId::Hover:
            return QCoreApplication::translate("Appearance", "On hover");
        case CardHeaderId::Filled:
            break;
    }
    return QCoreApplication::translate("Appearance", "Filled");
}

QVector<CardHeaderId> allCardHeaders() {
    return allIn(kCardHeaderIds);
}

// --- Canvas ---------------------------------------------------------------

QString canvasIdString(CanvasId id) {
    return idOf(kCanvasIds, id);
}

CanvasId canvasFromId(const QString& id) {
    return fromIdIn(kCanvasIds, id);
}

QString canvasDisplayName(CanvasId id) {
    switch (id) {
        case CanvasId::Dots:
            return QCoreApplication::translate("Appearance", "Dots");
        case CanvasId::Grid:
            return QCoreApplication::translate("Appearance", "Grid");
        case CanvasId::Gradient:
            return QCoreApplication::translate("Appearance", "Gradient");
        case CanvasId::Plain:
            break;
    }
    return QCoreApplication::translate("Appearance", "Plain");
}

QVector<CanvasId> allCanvases() {
    return allIn(kCanvasIds);
}

// --- Motion ---------------------------------------------------------------

QString motionIdString(MotionId id) {
    return idOf(kMotionIds, id);
}

MotionId motionFromId(const QString& id) {
    return fromIdIn(kMotionIds, id);
}

QString motionDisplayName(MotionId id) {
    return id == MotionId::Reduced ? QCoreApplication::translate("Appearance", "Reduced") : QCoreApplication::translate("Appearance", "Animated");
}

QVector<MotionId> allMotions() {
    return allIn(kMotionIds);
}

// --- Custom palettes --------------------------------------------------------

QJsonObject paletteToJson(const ThemePalette& palette) {
    QJsonObject json;
    json["id"] = palette.id;
    json["name"] = palette.displayName;
    for (const PaletteField& field : kPaletteFields) {
        json[QLatin1String(field.key)] = (palette.*field.member).name(QColor::HexArgb);
    }
    QJsonArray series;
    for (const QColor& color : palette.series) {
        series.append(color.name(QColor::HexArgb));
    }
    json["series"] = series;
    return json;
}

ThemePalette paletteFromJson(const QJsonObject& json, const ThemePalette& fallback) {
    ThemePalette palette = fallback;
    palette.id = json.value("id").toString(fallback.id);
    palette.displayName = json.value("name").toString(fallback.displayName);
    for (const PaletteField& field : kPaletteFields) {
        const QColor color(json.value(QLatin1String(field.key)).toString());
        if (color.isValid()) {
            palette.*field.member = color;
        }
    }
    const QJsonArray series = json.value("series").toArray();
    if (!series.isEmpty()) {
        palette.series.clear();
        for (const QJsonValue& value : series) {
            const QColor color(value.toString());
            if (color.isValid()) {
                palette.series.append(color);
            }
        }
    }
    return palette;
}

bool isCustomPaletteId(const QString& id) {
    return id.startsWith(QLatin1String("custom:"));
}

double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
}

}  // namespace traceview
