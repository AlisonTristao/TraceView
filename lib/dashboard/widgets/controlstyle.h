#pragma once

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "traceview/theme.h"

namespace traceview {

// The control style sheet: every visual decision the control widgets (push
// button, toggle switch, slider, LED -- widgets/controlwidgets.h) make comes
// from one ControlStyle token set below, never from a literal in a widget's
// own paint code. It is the controls' counterpart of ChartStyle
// (widgets/chartstyle.h), and docs/CONTROL_STYLE.md is its manual.
//
// Five styles, picked app-wide from View > Control Style (also Settings >
// Appearance) or per control in its properties:
//   Dashboard   -- the original TraceView look: rounded, accent-filled.
//   Engineering -- a bench instrument: square edges, 0/I rocker, ruler
//                  with ticks, recessed readout in fixed-pitch digits.
//   Scientific  -- a publication look: thin outlines, a checkbox, a line
//                  slider with a pointer, italic serif titles.
//   Panel       -- a physical panel: arcade button, lever switch, fader,
//                  LEDs behind a lens, a 7-segment-like readout.
//   Retro       -- a phosphor terminal: chamfered corners, fixed pitch
//                  everywhere, "[ text ]" buttons, a slider of blocks.

enum class ControlStyleId { Dashboard, Engineering, Scientific, Panel, Retro };

// "dashboard"/"engineering"/"scientific"/"panel"/"retro" -- the config
// "style" value and the AppSettings value. FromId() falls back to Dashboard
// for anything unrecognized.
QString controlStyleIdString(ControlStyleId id);
ControlStyleId controlStyleFromId(const QString& id);
QString controlStyleDisplayName(ControlStyleId id);  // translated
QVector<ControlStyleId> allControlStyles();

// How each control is drawn. One look per style today, but they are
// separate tokens so a future style can mix them.
enum class ControlButtonLook {
    Filled,   // a rounded block in the tone color, text on it
    Striped,  // a raised key with a tone-colored stripe on its left edge
    Outline,  // a thin outline in the tone color, no fill until pressed
    Arcade,   // a round cap in a bezel, label beside it
    Bracket,  // a chamfered outline, "[ label ]", inverted while pressed
};

enum class ControlSwitchLook {
    Pill,      // sliding thumb in a rounded track
    Rocker,    // two halves, "0" and "I", the active one filled
    Checkbox,  // a square with a check mark
    Lever,     // a metal lever on a nut, up = on, with a pilot LED
    Cells,     // two text cells "off" / "on", the active one inverted
};

enum class ControlSliderLook {
    Track,   // rounded track, filled part in accent, round thumb
    Ruler,   // hairline with ticks and 0/50/100, rectangular cap
    Line,    // hairline with end ticks and a pointer triangle
    Fader,   // dark slot with a scale above and a grooved metal cap
    Blocks,  // a row of cells, the filled ones in accent
};

enum class ControlLedLook {
    Dot,    // a flat dot with a soft halo when on
    Bezel,  // a lamp in a metal ring, small highlight
    Ring,   // a thin outline, filled when on
    Lens,   // halo, bezel, body, core and specular highlight
    Pixel,  // a square pixel with a square glow when on
};

// The box a slider prints its value in.
enum class ControlReadoutLook {
    Plain,    // just the text
    Boxed,    // recessed box on the window background
    Display,  // black display, digits in accent over unlit "8"s
};

struct ControlStyle {
    ControlStyleId id = ControlStyleId::Dashboard;

    ControlButtonLook button = ControlButtonLook::Filled;
    ControlSwitchLook switchLook = ControlSwitchLook::Pill;
    ControlSliderLook slider = ControlSliderLook::Track;
    ControlLedLook led = ControlLedLook::Dot;
    ControlReadoutLook readout = ControlReadoutLook::Plain;

    // Corner radius of buttons, rockers and readout boxes; chamfer length
    // instead when `chamfer` is set.
    qreal cornerRadius = 8.0;
    bool chamfer = false;
    // Hairline width of outlines.
    qreal lineWidth = 1.0;

    // Text
    bool monoText = false;           // labels and readouts in fixed pitch
    bool serifText = false;          // labels in a serif face
    bool serifItalicTitle = false;   // titles in an italic serif
    bool lowercaseText = false;      // labels shown in lower case
    QString titlePrefix;             // drawn before every title ("> ")
    qreal titleLetterSpacing = 0.0;  // percent, QFont::PercentageSpacing - 100
    bool accentTitles = false;       // titles in accent instead of textSecondary
    bool accentReadout = false;      // slider value in accent
    bool brightTitles = false;       // titles in textPrimary
    bool screws = false;             // panel screws in the four corners
};

ControlStyle controlStyleFor(ControlStyleId id);

// The colors a control paints with, all derived from the palette so every
// style works in every theme (docs/THEMING.md).
struct ControlColors {
    QColor background;
    QColor surface;
    QColor surfaceAlt;
    QColor border;
    QColor frame;  // outlines a style draws in "ink", stronger than border
    QColor text;
    QColor textSecondary;
    QColor textDisabled;
    QColor accent;
    QColor success;
    QColor warning;
    QColor danger;
};

ControlColors controlColors(const ThemePalette& palette);

// `a` moved towards `b` by `t` (0..1), alpha included.
QColor mixControlColor(const QColor& a, const QColor& b, qreal t);
// Black or white, whichever reads better on `fill`.
QColor textOnColor(const QColor& fill);

// A push button's color ("variant" in its config).
enum class ControlTone { Accent, Success, Warning, Danger };
ControlTone controlToneFromId(const QString& id);  // "default" -> Accent
QColor controlToneColor(const ControlColors& colors, ControlTone tone);

// A control's own style choice, the config "style" key: "app" (or missing)
// follows the app-wide control style, anything else pins that style.
inline constexpr char kControlStyleKey[] = "style";
inline constexpr char kControlStyleApp[] = "app";

struct ControlStyleChoice {
    bool followApp = true;
    ControlStyleId style = ControlStyleId::Dashboard;
};

ControlStyleChoice parseControlStyleChoice(const QJsonObject& config);
// The style to draw with: the pinned one, or `appStyleId` (the
// AppSettings value) when following the app.
ControlStyleId effectiveControlStyle(const ControlStyleChoice& choice, const QString& appStyleId);

// `text` as the style shows it: lower case for Retro.
QString controlStyledText(const ControlStyle& style, const QString& text);
// A title as the style shows it: lower case and prefixed for Retro.
QString controlStyledTitle(const ControlStyle& style, const QString& title);

}  // namespace traceview
