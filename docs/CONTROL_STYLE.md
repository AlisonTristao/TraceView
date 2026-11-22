# Control style guide

The control widgets (push button, toggle switch, slider and LED) draw
themselves from one shared **style sheet**,
[lib/dashboard/widgets/controlstyle.h](../lib/dashboard/widgets/controlstyle.h).
They draw it through one shared set of **paint building blocks**,
[lib/dashboard/widgets/controlpainting.h](../lib/dashboard/widgets/controlpainting.h).
This is the controls' counterpart of the chart style sheet
([CHART_STYLE.md](CHART_STYLE.md)): two controls in the same style look like
one family, and every style works with every palette. The chart styles have
the same five names, so a dashboard in Panel or Retro matches throughout.

## The five styles

Picked app-wide from **View → Control Style** (also Settings → Appearance),
stored in `AppSettings::controlStyleId()` (`QSettings` key
`appearance/controlStyle`, default **Dashboard**). A control follows it while
its own **Style** (properties panel, config key `"style"`) is left at
**App default** (`"app"`). That is the default for new controls and for
dashboards saved before styles existed. Picking a style there pins that one
control to it.

| | Dashboard | Engineering | Scientific | Panel | Retro |
|---|---|---|---|---|---|
| Idea | The original TraceView look | Bench instrument | Publication | Physical panel | Phosphor terminal |
| Button (`button`) | `Filled`: rounded block in the tone color | `Striped`: raised key, tone stripe on the left | `Outline`: hairline, filled only while pressed | `Arcade`: round cap in a bezel, label beside it | `Bracket`: chamfered outline, `[ label ]`, inverted while pressed |
| Switch (`switchLook`) | `Pill`: sliding thumb | `Rocker`: `0` / `I` halves | `Checkbox`: square with a check mark | `Lever`: metal lever on a nut, pilot LED | `Cells`: `off` / `on` text cells |
| Slider (`slider`) | `Track`: filled track, round thumb | `Ruler`: ticks, min/mid/max labels, square cap | `Line`: hairline, end ticks, pointer | `Fader`: dark slot, scale, grooved metal cap | `Blocks`: a row of cells |
| LED (`led`) | `Dot`: flat dot, soft halo | `Bezel`: lamp in a metal ring | `Ring`: outline, filled when lit | `Lens`: halo, bezel, body, core, highlight | `Pixel`: square, square glow |
| Value box (`readout`) | `Plain`, medium weight | `Boxed`: recessed box | `Plain` | `Display`: black, unlit `8`s behind | `Plain`, in accent |
| Corners | 8 px round | 2 px round | square | 3 px round | 6 px chamfer |
| Text | Proportional | Fixed pitch | Serif, italic titles | Spaced titles | Fixed pitch, lower case, `> ` titles, accent |
| Screws (`screws`) | - | - | - | In the four corners (`paintPanelScrews()`) | - |

The table is `controlStyleFor()` in `controlstyle.cpp`. Change a value
there and every control follows.

The Panel look is hardware, so its bezels, nuts, fader cap and display are
fixed metal and black-plastic colors (`kBezelOuter`... in
`controlpainting.cpp`). Every other color comes from the palette through
`controlColors()`.

## The controls

| Kind | Type id | Paint function | Notes |
|---|---|---|---|
| Push button | `push_button` | `paintControlButton()` | `variant` picks the tone: default (accent), success, warning, danger. |
| Toggle switch | `toggle_switch` | `paintControlSwitch()` | Natural size per look (`controlSwitchSize()`), centered. `onLabel`/`offLabel` are shown beside it, or inside it with `Cells`. |
| Slider | `slider` | `paintControlSlider()`, `paintControlReadout()` | `controlSliderTrack()` is the geometry both the painter and the mouse use. |
| LED | `led` | `paintControlLed()` | Bound to one telemetry field, see below. |

Titles and state texts are `ControlText` widgets, painted rather than
`QLabel`s so the app stylesheet can't override the style's font and color.

## The LED

A `TelemetryConsumer` (`dashboard/telemetryconsumer.h`) bound to one field,
by name like every telemetry widget (`topicName`, `fieldName`, see
[DASHBOARD.md](DASHBOARD.md) "Telemetry bindings"). Config (`LedConfig` in
`controldata.h`):

| Key | Meaning |
|---|---|
| `colorMode` | `fixed`: lit in `color` while `condition` holds. `rgb`: the field is a packed `0xRRGGBB` color, `0` is off. |
| `condition`, `threshold` | `nonzero` (default), `zero`, `above`, `below` (the last two compare with `threshold`). |
| `color` | `"#rrggbb"`; missing means the palette's success color. |
| `blink`, `blinkMs` | Blink while lit. |
| `onLabel`, `offLabel` | State text under the lamp. |
| `sampleTimeMs` | Requested update period (default 100 ms). |

The lamp grows with the cell up to a 28 px radius (`kMaxLedRadius`). Until
the first sample arrives it is off.

## Rules

1. **No look in literals.** A control's paint code reads `ControlStyle` and
   `ControlColors`. The Panel hardware colors are the only exception, kept
   as named constants in `controlpainting.cpp`.
2. **Switch on the look token, never on the style id.** A future style can
   then mix looks, such as a Ruler slider with Pill switches.
3. **The geometry the painter uses is the geometry the mouse uses**
   (`controlSliderTrack()`), so a click lands where the thumb is drawn.
4. **Disabled controls** are drawn at 45% opacity by every paint function.

## Adding a style

1. Add the id to `ControlStyleId`, `controlStyleIdString()`,
   `controlStyleDisplayName()` and `allControlStyles()`.
2. Give it its tokens in `controlStyleFor()`. If it needs a new look, add
   the enum value and its case in the matching paint function.
3. `tests/test_controlstyle.cpp` checks that every style draws each control
   differently, and paints every control in every style.
4. Translate the display name in all eight catalogs (CONTRIBUTING.md
   "Translations").
