#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "traceview/theme.h"

namespace traceview {

// Appearance choices that sit next to the palette (colors) and the frame
// (shapes, traceview/framestyle.h), all owned by ThemeManager and applied
// app-wide:
//   Data colors  -- the colors chart series, bars and gauge rings are drawn in.
//   Density      -- header height, inner padding and the gap between cards.
//   Card header  -- how a dashboard card's title strip is drawn.
//   Canvas       -- what the dashboard background shows behind the cards.
//   Motion       -- whether values ease into place or jump.
// Each is a plain id string in QSettings ("appearance/<key>") and a token
// table here; nothing outside this file and ThemeManager branches on the
// ids. See "Appearance" in docs/VISUAL_IDENTITY.md.

// --- Data colors ----------------------------------------------------------

//   Custom      -- every series keeps the color picked in its own config.
//   Palette     -- the active palette's series colors, by series index.
//   Matlab      -- MATLAB's default line color order.
//   Tableau     -- Tableau 10 (matplotlib's default cycle).
//   OkabeIto    -- Okabe-Ito, safe for the common kinds of color blindness.
//   Monochrome  -- shades of the palette's accent color.
enum class DataColorsId { Custom, Palette, Matlab, Tableau, OkabeIto, Monochrome };

QString dataColorsIdString(DataColorsId id);
DataColorsId dataColorsFromId(const QString& id);
QString dataColorsDisplayName(DataColorsId id);
QVector<DataColorsId> allDataColors();

// The color series `index` is drawn in under `scheme`: `own` (the series'
// configured color) for Custom, otherwise the scheme's color for that index,
// cycling.
QColor dataSeriesColor(DataColorsId scheme, int index, const QColor& own,
                       const ThemePalette& palette);

// --- Density --------------------------------------------------------------

enum class DensityId { Compact, Normal, Comfortable };

struct Density {
    DensityId id = DensityId::Normal;
    int headerHeight = 24;      // dashboard card title strip
    int contentPadding = 12;    // space between a widget's edge and its chrome
    double gutterScale = 1.0;   // multiplies the gap between cards
};

QString densityIdString(DensityId id);
DensityId densityFromId(const QString& id);
QString densityDisplayName(DensityId id);
QVector<DensityId> allDensities();
const Density& density(DensityId id);

// --- Card header ----------------------------------------------------------

//   Filled  -- a filled title strip (the original look).
//   Line    -- the title on the card's own fill, over a thin separator.
//   Hover   -- no strip in Run mode; it slides in over the card on mouse
//              hover. Touch screens, which have no hover, get Line instead.
enum class CardHeaderId { Filled, Line, Hover };

QString cardHeaderIdString(CardHeaderId id);
CardHeaderId cardHeaderFromId(const QString& id);
QString cardHeaderDisplayName(CardHeaderId id);
QVector<CardHeaderId> allCardHeaders();

// --- Canvas ---------------------------------------------------------------

enum class CanvasId { Plain, Dots, Grid, Gradient };

QString canvasIdString(CanvasId id);
CanvasId canvasFromId(const QString& id);
QString canvasDisplayName(CanvasId id);
QVector<CanvasId> allCanvases();

// --- Motion ---------------------------------------------------------------

//   Animated  -- gauge needles and bars ease to a new value; selection fades.
//   Reduced   -- everything jumps straight to its new state.
enum class MotionId { Animated, Reduced };

QString motionIdString(MotionId id);
MotionId motionFromId(const QString& id);
QString motionDisplayName(MotionId id);
QVector<MotionId> allMotions();

// --- Custom palettes --------------------------------------------------------

// A user-made palette round-trips through this JSON shape (QSettings
// "appearance/customPalettes"). Ids of custom palettes start with "custom:".
QJsonObject paletteToJson(const ThemePalette& palette);
ThemePalette paletteFromJson(const QJsonObject& json, const ThemePalette& fallback);
bool isCustomPaletteId(const QString& id);

// WCAG contrast ratio between two opaque colors, 1..21. The palette editor
// warns below 4.5 (normal text).
double contrastRatio(const QColor& a, const QColor& b);

}  // namespace traceview
