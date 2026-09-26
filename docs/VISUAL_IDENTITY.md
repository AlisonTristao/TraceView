# Visual identity

Decisions made in TAREFA 0 of `TODO_VISUAL_IDENTITY.txt` (2026-08-09), before
any widget redesign started. These are the defaults every later TAREFA
should follow instead of inventing its own convention; if a later TAREFA
needs to deviate, update this file in the same change.

## Appearance

Every app-wide look choice sits under **View** (and Settings → Appearance),
and each one is a token table rather than literals in paint code:

| Choice | Owner | Tokens | Options |
|---|---|---|---|
| Palette | `ThemeManager` | `ThemePalette` ([THEMING.md](THEMING.md)) | built-in templates + custom palettes |
| Frame | `ThemeManager` | `traceview/framestyle.h` (below) | Rounded, Square, Borderless, Chamfered |
| Chart style | `AppSettings` | `widgets/chartstyle.h` ([CHART_STYLE.md](CHART_STYLE.md)) | Dashboard, Engineering, Scientific |
| Data colors | `ThemeManager` | `traceview/appearance.h` | Per series, Palette, MATLAB, Tableau, Color-blind safe (Okabe-Ito), Monochrome |
| Density | `ThemeManager` | `traceview/appearance.h` | Compact, Normal, Comfortable |
| Card header | `ThemeManager` | `traceview/appearance.h` | Filled, Line, On hover |
| Canvas | `ThemeManager` | `traceview/appearance.h` | Plain, Dots, Grid, Gradient |
| Motion | `ThemeManager` | `traceview/appearance.h` | Animated, Reduced |

- **One list drives every picker.** `core/appearancecatalog.h` describes each
  choice (menu title, row label, choices, how to read and apply it). The
  View menu, Settings → Appearance, the presets and the per-workspace pin
  are all built from it. A new choice is one entry there plus its tokens.
- **Every change arrives as `themeChanged()`.** Setters re-apply the
  stylesheet and emit it, so anything that already follows the palette also
  picks up a density, frame or data color change. `DashboardCell` and
  `DashboardGrid` lay out again on it, since density changes the header
  height and the gap between cards.
- **Data colors** override each series' configured color by index while a
  scheme other than "Per series" is picked. The configured colors are kept
  (`ChartWidgetBase::m_ownColors`) and come back on "Per series".
- **Density** sets the card header height (20/24/30px), the chart inner
  padding (`chartOuterPadding()`, 8/12/16px) and scales the gap between
  cards (x0.5/1/1.6).
- **Card header "On hover"** takes no space in Run mode: the strip floats in
  over the top of the card while the mouse is on it (`HoverHeaderStrip` in
  `dashboardcell.cpp`). Touch-only platforms have no hover, so they get
  "Line" instead (`ThemeManager::cardHeader()`).
- **Motion "Reduced"** makes gauge needles and bars jump to a new value
  instead of easing (`StyledChartWidget::easedValues()`) and makes the
  selection outline snap. It is not part of presets or workspace pins,
  because it is an accessibility preference that belongs to the person
  rather than to a look.
- **Presets** apply palette, frame, chart style, data colors, density, card
  header and canvas together. The built-in ones are TraceView, Lab, Paper,
  HUD, Synthwave and Accessible. **Save Current Appearance as Preset...**
  adds user presets (QSettings `appearance/userPresets`).
- **Per-workspace appearance**: **View → Workspace Appearance → Pin...**
  stores the current appearance in the workspace (project file, workspace
  `"appearance"`). A pinned workspace applies it whenever it becomes active
  and records every appearance change made while it is. Unpinned workspaces
  share the app-wide appearance (QSettings `appearance/global`).
- `tools/chart_preview --snapshot <dir>` writes `preset-<id>.png`: a real
  dashboard with every widget kind, once per built-in preset.

## Frame styles

The shape and outline of every large custom-painted container (dashboard
cells, the widgets inside them, device cards) and the corner radius of the
QSS-driven controls come from the app-wide **frame style**, picked under
**View → Frame** or Settings → Appearance and owned by `ThemeManager` next to
the palette. The palette decides colors and the frame decides shapes. The
tokens live in
[include/traceview/framestyle.h](../include/traceview/framestyle.h):

| | Rounded (default) | Square | Borderless | Chamfered |
|---|---|---|---|---|
| Corner | Round, 6px | Square | Round, 6px | Cut at 45°, 8px |
| Idle outline | 2px `palette.border` | 1px | none | 2px |
| QSS controls | 4px radius (as written) | square | 4px radius | square |
| Selection outline | 2px accent | 2px accent | 2px accent | 2px accent |

- Paint every container through `currentFramePath()`/`frameShapePath()`
  (or `DashboardWidget::roundedPath()`/`contentFillPath()`, which use it),
  never a hard-coded radius, so a new frame style reaches it for free.
- Check `FrameStyle::idleOutline` before stroking a decorative border.
  Borderless means that no container draws one. The selection outline is
  always drawn (`kFrameSelectionWidth`).
- QSS radii reach the controls through `scaleStyleSheetRadii()`, which
  multiplies every `border-radius` in the built stylesheet by the frame's
  `controlRadiusScale`. Keep writing radii in `stylesheet.cpp` as plain
  `border-radius: Npx`.
- Changing the frame rebuilds the stylesheet and emits `themeChanged()`, so
  anything that already repaints on a palette change also reshapes.
  `DashboardCell` also rebuilds its content mask then.
- `tools/chart_preview --snapshot <dir>` writes `frames-<theme>.png` with
  every frame style on real cells (idle, selected) and controls.

## Corner radius

The values below are the **Rounded** frame style. The rules about lining up
fills, masks and outlines hold for every frame style.

- **4px** for QSS-driven controls (buttons, inputs, combo boxes, checkboxes)
  — already the de facto value across `stylesheet.cpp`, kept as-is.
- **6px** for large custom-painted containers: `DashboardCell`'s outer
  border and the flat background rect drawn by `paintBackground()` in
  `chartwidgets.cpp` (line chart/bar chart/gauge). Larger than the control
  radius because these are bigger areas. Originally set to 6px in TAREFA 0;
  doubled to 12px on 2026-08-09 after seeing TAREFA 1 live — 6px read as
  barely-rounded on cells this size. Halved back to 6px on 2026-09-24:
  12px read as too rounded.
- Both radii must be applied with a `QPainterPath`, not `drawRoundedRect`
  on a plain fill, and the corners must line up: `DashboardCell`'s outline
  and the child `DashboardWidget`'s opaque background fill (`WA_StyledBackground`,
  see the big comment in `controlwidgets.cpp` / "Controls" in
  `DASHBOARD.md`) have to clip to the *same* rounded path, in both edit and
  locked mode, or a straight-cornered fill will show through the rounded
  outline. Verify in both themes (View > Theme) before calling a widget done.
- `DashboardCell`s show a 2px `palette.border` idle outline (same convention
  as `DeviceCard`'s outline, `devicecard.cpp`) — changed 2026-08-18 from the
  earlier "idle cells have no outer stroke" rule, so a cell's extent reads
  clearly even when nothing is selected. Exception: headerless controls
  (push button/toggle switch/slider, `widgets/controlwidgets.cpp`, see
  `wantsCellHeader() == false`) skip the idle outline — added 2026-08-18 same
  day, reported live right after the general change looked cluttered on
  these — they already read as bare controls rather than cards (see
  "Control panel fill" below) and a stroke around them fought that. All
  kinds still layer a distinct `palette.accent` (or `palette.danger` while an
  invalid drag is in progress) outline on top when selected, instead of a
  stroke appearing/disappearing. Both passes are painted by a transparent
  `BorderOverlay` child above the content widget. A parent paints before its
  children in Qt; painting the outline directly in `DashboardCell::paintEvent()`
  would let the content cover its straight runs while a few high-contrast
  pixels survive around the rounded corners. Do not move the outline back
  under the content.
- `partiallyRoundedRect()` (now in `traceview/framestyle.h`) uses `Qt::WindingFill` when it welds square
  corner patches onto the rounded base. The default odd/even rule subtracts
  their overlap and leaves a visible radius-sized square hole under headers.

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
