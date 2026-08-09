# Visual identity

Decisions made in TAREFA 0 of `TODO_VISUAL_IDENTITY.txt` (2026-08-09), before
any widget redesign started. These are the defaults every later TAREFA
should follow instead of inventing its own convention; if a later TAREFA
needs to deviate, update this file in the same change.

## Corner radius

- **4px** for QSS-driven controls (buttons, inputs, combo boxes, checkboxes)
  — already the de facto value across `stylesheet.cpp`, kept as-is.
- **12px** for large custom-painted containers: `DashboardCell`'s outer
  border and the flat background rect drawn by `paintBackground()` in
  `chartwidgets.cpp` (line chart/bar chart/gauge). Larger than the control
  radius because these are bigger areas. Originally set to 6px in TAREFA 0;
  doubled to 12px on 2026-08-09 after seeing TAREFA 1 live — 6px read as
  barely-rounded on cells this size.
- Both radii must be applied with a `QPainterPath`, not `drawRoundedRect`
  on a plain fill, and the corners must line up: `DashboardCell`'s outline
  and the child `DashboardWidget`'s opaque background fill (`WA_StyledBackground`,
  see the big comment in `controlwidgets.cpp` / "Controls" in
  `DASHBOARD.md`) have to clip to the *same* rounded path, in both edit and
  locked mode, or a straight-cornered fill will show through the rounded
  outline. Verify in both themes (View > Theme) before calling a widget done.

## Elevation / shadow

**Flat — no `QGraphicsDropShadowEffect`, anywhere.** `QGraphicsEffect`
forces software rendering for the whole widget subtree it's attached to;
with a grid that can hold many widgets at once this is a real repaint-cost
risk, and the codebase doesn't use custom paint effects like this anywhere
else today. Selection/hover state keeps expressing itself the way it
already does — a border color/width change (`palette.accent`, thicker
line) — not a glow or drop shadow. Revisit only if a specific widget has a
strong, tested case for it (profile with a full grid first).

## Motion

Short, discrete-state transitions only — not continuous/idle animation:

- Selection border color change, and (TAREFA 2) the toggle switch's thumb
  sliding between on/off, may use `QPropertyAnimation`/`QVariantAnimation`
  at ~150ms with an ease curve.
- Anything that would redraw on its own on a timer (glow, pulse, idle
  shimmer) is out — it would compete with the existing per-instance ~30Hz
  repaint throttle (`ChartWidgetBase::onSerialPayload`) instead of running
  alongside it, and there's no concrete case asking for it.
- Data updates (chart lines, gauge arc, incoming serial text) stay
  instantaneous, same as today — only the throttle limits their repaint
  rate, no eased interpolation between values.

## Typography

Leave the system default font/size everywhere **except** one deliberate
accent: the gauge's central value (`DummyGaugeWidget::paintEvent`,
`chartwidgets.cpp`) gets a larger, bold point size, since it's the single
most important number on that widget and today it's visually identical to
every other label. `DashboardCell` header titles, control labels
(TAREFA 2), and chart corner labels (TAREFA 3) stay at default weight/size
— they're identifying text, not the headline value.

## Border contrast (light theme)

`ThemePalette::border` (the subtle-divider token used all over
`stylesheet.cpp` — button/input/combo box/table borders) had alpha 32 in
`makeLightPalette()`, noticeably fainter against white than the dark
theme's alpha-40-on-near-black equivalent. Reported directly while
checking TAREFA 1 in the app (2026-08-09); bumped to alpha 56 in
`palettes.cpp`. Not a TAREFA 0 decision so much as a bug in one of them —
noted here so nobody "fixes" it back down without knowing why.

## Control panel fill

Headerless controls (push button/toggle switch/slider, `controlwidgets.cpp`)
no longer use `DashboardWidget`'s default opaque fill (`palette.background`,
same token as the canvas behind the whole grid). Reported live 2026-08-09:
with `DashboardCell`'s border now rounded, a fill indistinguishable from the
canvas left the rounded corner reading as a disconnected stray curve instead
of a panel, especially with another cell nearby. Fixed by opting these three
widgets into `palette.surface` (same tone charts already use) via a
`dashboardControlPanel="true"` dynamic property matched in `stylesheet.cpp`
— same property-selector idiom as `QPushButton[variant=...]`. Any future
headerless control kind should set this property too, for the same reason.

## Series palette

`ThemePalette::series` (`theme.h`) stops being a dead field. A newly added
series in `ChartConfigEditor` should default its color to
`palette.series[index % 6]` instead of the hardcoded `"#3B82F6"`
(`chartconfigeditor.cpp:343`, same literal in both themes today). The color
picker stays fully free-form after that — the palette only decides the
*default* a new series starts from, so per-theme series colors is a
real feature and not a comment that lies. Implementing this is TAREFA 3's
job (it's the TAREFA that touches `chartconfigeditor.cpp`); this file just
settles the default-source question so that TAREFA doesn't have to
re-litigate it.

Implemented in TAREFA 3 (2026-08-09): `ChartConfigEditor::addSeriesRow()`
only falls back to the palette when the row's JSON has no `"color"` key at
all (a fresh "+ Add series" row, or an old save predating the field) —
a row with an explicit color, however it got one, is left alone.
