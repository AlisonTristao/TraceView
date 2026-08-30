# Dashboard grid

The dashboard is a Bootstrap-like grid (`DashboardGrid`,
[lib/dashboard/dashboardgrid.h](../lib/dashboard/dashboardgrid.h)). Each
widget (`DashboardItem`) stores its position/size as `x`/`y`/`width`/`height`
fractions (0.0-1.0) of the canvas area, not pixels or cell indices — so a
layout is resolution-independent and stays visually identical (relative to
the canvas) across window resizes, with no clamping or reflow needed.
`DashboardGrid` is the only place that converts those fractions to pixels
(`itemRect()`); a fixed logical division count (`kGridColumns`/`kGridRows`
in dashboardgrid.cpp) only drives grid-line painting and drag/resize snap
granularity, it is not part of the persisted model.

There is no `QLayout` involved — cells are positioned manually
(`setGeometry`) so they can be dragged and resized with the mouse.

## Editing a layout

The top toolbar's **Configure Layout** action toggles edit mode:

- Off: widgets are locked in place, no chrome is shown.
- On: each widget gets a header (drag handle + remove button) and a
  resize grip in its bottom-right corner; grid lines are drawn; **Add
  Widget** becomes available.
  - A kind can opt out of the header via `DashboardWidget::wantsCellHeader()`
    (see [dashboard/dashboardwidget.h](../lib/dashboard/dashboardwidget.h)) —
    the control types (push button/toggle/slider, see "Element kinds" below)
    do, since the header would eat a disproportionate share of an
    already-small cell. `DashboardCell` gives that space back to the content
    instead of leaving it blank, and lets a click anywhere in the selected
    body start a move-drag in place of the missing header.

**Add Widget** drops in an instance of the first registered type — no
picker dialog — and selects it immediately (`DashboardGrid::addItem`), so
the properties panel comes up right away with fields ready to edit; the
type, along with everything else, is picked there instead of upfront.

Selecting a widget populates the properties panel
(`PropertiesPanel`, [lib/core/propertiespanel.h](../lib/core/propertiespanel.h)),
a fixed-width panel embedded next to the canvas (not a `QDockWidget` — that
would span the full window height, alongside the ribbon too, instead of
just the canvas). MainWindow wires its `*ChangeRequested` signals to the
matching `DashboardGrid` calls and keeps it in sync via `selectionChanged`
and the undo stack's `indexChanged` (so undo/redo of a property edit
updates the panel too).

Above a divider, three fields are common to every kind:

- **Type** — swaps the widget for a new instance of the chosen type,
  keeping position/size (`DashboardGrid::changeSelectedType`).
- **Name** — the header's display name. Empty falls back to the type's
  registered `displayName` (see `DashboardGrid::displayNameFor` in
  [dashboardgrid.cpp](../lib/dashboard/dashboardgrid.cpp)). Doesn't affect
  `typeId`, `id`, or `key`.
- **Key** — a user-defined, optional string that must be unique across
  items (`DashboardGrid::isKeyAvailable`); rejected edits (duplicate key)
  revert the field and show a status bar message. This is the intended
  handle for wiring a widget up to a live data source later — telemetry
  updates would target a specific widget by `key`, chosen and remembered
  by the user, independent of its display name or type.

Every item also carries a separate, stable `id` (a `QUuid`, assigned once
in `DashboardGrid::addItem` and never shown in the UI) that the grid uses
internally to track cells/drag state/undo commands. `key` is the
user-facing identifier; `id` is not meant to be read or typed by a user.

Below the divider, the panel hosts whatever type-specific settings the
selected type registers — see "Per-type config editor" below. It's edited
and persisted (`DashboardGrid::changeSelectedConfig`/
`selectedItemConfig`, `SetItemConfigCommand`) exactly like Type/Name/Key,
undo/redo and all; types that don't register a config editor just leave
that area empty.

Dragging/resizing snaps to the grid on release. Overlapping another widget is
allowed — only going out of grid bounds is rejected, snapping the widget back
to its last valid position (`DashboardGrid::isPlacementValid` in
[dashboardgrid.cpp](../lib/dashboard/dashboardgrid.cpp)). A brand-new widget
(**Add Widget**) or a pasted one still prefers a genuinely empty spot when
one exists (`DashboardGrid::isPlacementFree`, used only for that automatic
placement), falling back to a spot that may overlap when the canvas is full.

## Layering

Since widgets can now overlap, stacking order matters: whichever one is
"in front" is the one on top and the one a click lands on. Order is simply
each item's position in the project's item list (`DashboardItem` order,
persisted as-is in the `.tvproj` file, see "Project file" below) — the last
item is frontmost, no separate z-index field.

Four ribbon actions (Layout tab, next to Add/Remove) reorder the
selected widget's stacking position, each undoable like every other grid
edit: **To Front** / **Forward** / **Backward** / **To Back**
(`DashboardGrid::bringSelectedToFront`/`bringSelectedForward`/
`sendSelectedBackward`/`sendSelectedToBack` in
[dashboardgrid.cpp](../lib/dashboard/dashboardgrid.cpp), via a new
`ChangeZOrderCommand`).

A **Layers** panel (`LayersPanel`,
[lib/core/layerspanel.h](../lib/core/layerspanel.h)) sits to the left of the
canvas — mirroring the Properties panel's width on the right — listing every
item currently on the grid, front-most first, so one hidden behind another
is never forgotten. Selecting a row selects that item on the canvas exactly
like clicking it directly would: `DashboardGrid::selectItem()` temporarily
raises the selected widget to the very front (purely visual — it does not
touch the persisted stacking order) so it's fully reachable to drag out from
under whatever was covering it; deselecting restores it to its own layer.

## Element kinds

Every item placed on the grid is a `DashboardItem` (id/type/name/key/
position — identical fields regardless of kind) paired with a
`DashboardWidget` subclass that gives it its actual behavior. Each kind
lives in its own module under
[lib/dashboard/widgets/](../lib/dashboard/widgets/):

- **Chart** — `widgets/chartwidgets.h`/`.cpp`. The 3 registered types
  (`dummy_line`, `dummy_bar`, `dummy_gauge`) are throwaway placeholders
  meant to exercise the grid mechanics before real telemetry charts
  exist — replace/remove them once real chart types land. `dummy_line`/
  `dummy_bar` do have a real config editor already —
  `widgets/chartconfigeditor.h`/`.cpp` — covering how a chart's incoming
  data frame and series are shaped, and how the axes are displayed (X as
  sample count or elapsed time = `Ts * N`; Y auto-scaled or fixed
  min/max, with an optional unit label) — settled UI even before the
  charts themselves are real; see "Per-type config editor" below.
  `dummy_gauge` has its own, simpler editor —
  `widgets/gaugeconfigeditor.h`/`.cpp` — since it only ever shows one
  live value: which frame slot it reads (format + index, byte type if
  the frame is raw bytes), a fixed min/max range, unit, and decimals.
  No history/axis settings and no threshold-triggered actions —
  deliberately left out, it only ever reflects the current value.
- **Serial Monitor** — `widgets/serialmonitorwidget.h`/`.cpp`
  (`serial_monitor`): a header row (a tab strip
  `widgets/terminaltabbar.h`/`.cpp` on the left, a right-aligned **Clear**
  button that wipes the visible terminal's scrollback) over one
  `widgets/serialterminalwidget.h`/`.cpp` per tab — a
  miniterm/PlatformIO-Serial-Monitor-style terminal with no input line, one
  tab per device. The tabs are plain butted rectangles
  (`|terminal 1|terminal 2|`), every tab the same width (sized to the widest
  label) and labelled by its device's name; the terminal
  surface itself captures the keyboard and emits `sendRequested(QByteArray)`
  per keystroke (immediately, never buffered until Enter), which the widget
  re-emits as `terminalInput(deviceId, bytes)` tagged with the active tab's
  device. `Ctrl+←` / `Ctrl+→` cycle tabs; `Ctrl+C` copies the selection when
  there is one and otherwise sends `0x03` (SIGINT); `Ctrl+Shift+C` always
  copies and `Ctrl+Shift+V` pastes the clipboard as typed. The tab list is
  the widget's config — `{ "tabs": [ { "deviceId": … }, … ] }`, edited in
  `widgets/serialmonitorconfigeditor.h`/`.cpp` (add/remove/reorder rows).
  A pre-tabs config (a bare `deviceId`, or none) reads as a single tab, and
  a lone tab hides the strip (leaving just the Clear button).
  `core/serialwidgetbridge.h` resolves each tab's device and wires the
  active terminal to its `Backend::sendTerminalIn()` and every bound
  device's `Backend::terminalDataReceived()` back to the matching tab via
  `feedDevice()`, re-deriving all of it on `tabsChanged()`.
- **Text Board** (`text_board`) — `widgets/textboardwidget.h`/`.cpp`. A
  fixed-pitch, read-only surface for one whole `UTF8` telemetry topic. Every
  sample replaces the previous document, so a formatted status table appears
  to update only its changing numbers rather than scrolling. The painter
  preserves spaces and line breaks, never wraps, and derives one font size
  from the longest line plus the total line count whenever the cell is
  resized. Its config editor selects Device/Source/Text topic, requests a
  3000 ms period by default (about 0.33 Hz), and optionally stores waiting
  text shown before the first sample. Only catalog topics whose encoding is
  `UTF8` appear in the picker, though a numeric topic id can still be entered
  before its manifest arrives. `MainWindow` manages its topic subscription
  exactly like a chart/gauge; `TelemetryFieldRouter::textSample` carries the
  validated complete document to `TextBoardWidget::onTextSample()`.
- **Controls** — `widgets/controlwidgets.h`/`.cpp`, one class per kind, each
  placed and adjusted individually (not a multi-button panel — drop as many
  as needed and size each on its own). No cell header (`wantsCellHeader()`
  returns false — see "Editing a layout" above), and each fills its cell
  edge-to-edge (zero layout margins, `QSizePolicy::Expanding` on the
  interactive control) with the base `DashboardWidget`'s normal opaque
  `palette.background` fill — the same color as the canvas behind the cell,
  since `DashboardGrid` never fills its own background — so it reads as "no
  panel" without ever leaving a gap for `DashboardCell`'s border or
  `DashboardGrid`'s edit-mode grid lines to show through (both painted in
  `palette.borderStrong`). An earlier attempt turned `WA_StyledBackground`
  back off instead for genuine transparency; dropped because it needed
  every pixel of the actual control to be opaque to avoid a leak, and
  `QPushButton`'s default vertical size policy is `Fixed`, so it wasn't.
  Also
  resizable much smaller than other kinds (`kMinHeaderlessItemWidth`/
  `kMinHeaderlessItemHeight` in
  [dashboardgrid.cpp](../lib/dashboard/dashboardgrid.cpp), picked via
  `DashboardCell::hasHeader()` — see "Editing a layout" above), since there's
  no header height to stay legible above:
  - **Push Button** (`push_button`) — a momentary action button.
    `pressedRequested()` fires per click.
  - **Toggle Switch** (`toggle_switch`) — an on/off switch that holds its
    state. `toggled(bool)` fires on each flip.
  - **Slider** (`slider`) — a bounded value control with a live value
    readout beside it. `valueChanged(int)` fires while dragging.

  All three have a config editor — `widgets/controlconfigeditor.h`/`.cpp`
  (`PushButtonConfigEditor`/`ToggleSwitchConfigEditor`/`SliderConfigEditor`)
  — covering label text, appearance, and the event/command shape a real
  data-source binding will eventually consume:
  - **Push Button**: color style; Mode (Momentary sends an On Press and an
    On Release command; Pulse sends only On Press, once per click); Repeat
    While Held (re-fires On Press on an interval); a Long Press command
    that only fires once held past a threshold; Debounce (minimum time
    between triggers); Confirm Before Sending.
  - **Toggle Switch**: on/off text, starting state, On Command / Off
    Command sent on each transition, Confirm Before Toggling.
  - **Slider**: min/max/step/default/unit/whether the value is shown, a
    Command template (`{value}` is substituted), and Send mode —
    Continuous (throttled to a max rate while dragging) or On Release.

  Front-end shell only, same as the chart types: none of the commands
  above are wired to a live output yet, and — also same as the chart
  types' series settings — the config isn't fed back into the on-canvas
  widget's own appearance (e.g. a configured label doesn't yet relabel the
  button you see on the grid); it only round-trips through the project
  file for whenever a real data binding lands.

`DashboardGrid`, `DashboardItem`, and `PropertiesPanel` treat every kind
identically; only the widget's own implementation differs.

## Adding a new widget type

1. Subclass `DashboardWidget`
   ([lib/dashboard/dashboardwidget.h](../lib/dashboard/dashboardwidget.h))
   in its own file(s) under
   [lib/dashboard/widgets/](../lib/dashboard/widgets/) — one module per
   element kind (chart, serial, button panel, ...). See the existing
   modules listed above for the pattern; whether it paints itself
   (`paintEvent`, like the chart placeholders) or is built from real
   child widgets (`QComboBox`/`QPushButton`/layouts, like the serial
   panel and button panel) is up to what that kind needs.
2. Add the new files to `traceview_dashboard` in
   [lib/CMakeLists.txt](../lib/CMakeLists.txt).
3. Register it in `WidgetRegistry`'s constructor
   ([lib/dashboard/widgetregistry.cpp](../lib/dashboard/widgetregistry.cpp)):
   `registerType({"my_type", "My Type", [](QWidget* parent) { return new MyWidget(parent); }});`

That's it — the **Type** dropdown in the properties panel enumerates
`WidgetRegistry::availableTypes()`, so a new type shows up automatically.
No grid or `MainWindow` code needs to change.

## Per-type config editor

A type can optionally show its own settings below the properties panel's
divider (see "Editing a layout" above) — the chart types' data-frame/series
settings are the first example. It's opt-in and independent of step 3
above:

1. Subclass `WidgetConfigEditor`
   ([lib/dashboard/widgetconfigeditor.h](../lib/dashboard/widgetconfigeditor.h)):
   implement `setConfig(QJsonObject)` (populate the UI, must not emit
   `configChanged`) and `config()` (serialize current UI state back to
   JSON), and call `emit configChanged()` whenever the user edits
   something. See `widgets/chartconfigeditor.h`/`.cpp` for the pattern,
   including a `QTableWidget` with per-row cell widgets (`setCellWidget`)
   for the series table.
2. Set `WidgetTypeInfo::configEditorFactory` when registering the type:
   `registerType({"my_type", "My Type", myWidgetFactory, myConfigEditorFactory});`

The panel swaps in/out the right editor as the selection or its type
changes, and the JSON it produces round-trips through
`DashboardItem::config` — persisted to the project file and covered by
undo/redo — with no further wiring needed. What that JSON means (parsing
it into an actual data binding) is up to whatever later consumes it; the
editor's only job is capturing the settings.

## Project file

`ProjectStore` ([lib/project/projectstore.h](../lib/project/projectstore.h))
persists the app's project state as a `.tvproj` JSON file made of
independent top-level sections:

```json
{
  "traceview": { "formatVersion": 1 },
  "workspaces": {
    "activeId": "5d8f...",
    "list": [
      {
        "id": "5d8f...", "name": "Default",
        "dashboard": {
          "items": [
            {
              "id": "...", "type": "dummy_line", "name": "", "key": "",
              "config": {
                "format": "csv", "count": 1,
                "xAxis": { "mode": "samples", "sampleTimeMs": 100.0 },
                "yAxis": { "mode": "auto", "min": 0.0, "max": 100.0, "unit": "", "grid": true },
                "series": []
              },
              "x": 0.0, "y": 0.0, "width": 0.3333, "height": 0.25
            }
          ]
        }
      }
    ]
  }
}
```

A project holds N named workspaces (`WorkspaceManager`,
[lib/project/workspacemanager.h](../lib/project/workspacemanager.h)), each
wrapping its own independent `DashboardGrid::toJson()`/`fromJson()` payload
under `dashboard` — switching the active one (the workspace switcher button
in the status bar, bottom-right, `WorkspaceSwitcher`,
[lib/core/workspaceswitcher.h](../lib/core/workspaceswitcher.h)) swaps every
widget on the canvas for that workspace's own set. `MainWindow` keeps the
active workspace's `dashboard` field in sync with the live grid before every
save or switch (`WorkspaceManager::setDashboardFor`). Opening a `.tvproj`
saved before workspaces existed (no `workspaces` section, only a bare
top-level `dashboard`) migrates that single layout into one `"Default"`
workspace.

To persist another new kind of config later (e.g. a serial connection
profile), call `ProjectStore::instance().setSection("connection", {...})`
with a new top-level key when saving, and read it back via
`section("connection")` after a load — `ProjectStore` itself doesn't need
to change for this.

**Save Project** / **Open Project** in the toolbar drive this: Save reuses
the last path once one is chosen (prompting via a file dialog only the
first time), Open always prompts.
