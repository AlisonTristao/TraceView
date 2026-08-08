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

**Add Widget** drops in an instance of the first registered type — no
picker dialog — and selects it immediately (`DashboardGrid::addItem`), so
the properties panel comes up right away with fields ready to edit; the
type, along with everything else, is picked there instead of upfront.

Selecting a widget populates the properties panel
(`PropertiesPanel`, [lib/core/propertiespanel.h](../lib/core/propertiespanel.h)),
a fixed-width panel embedded next to the canvas (not a `QDockWidget` — that
would span the full window height, alongside the ribbon too, instead of
just the canvas) which edits three fields directly on the selected item —
MainWindow wires its `*ChangeRequested` signals to the matching
`DashboardGrid` calls and keeps it in sync via `selectionChanged` and the
undo stack's `indexChanged` (so undo/redo of a property edit updates the
panel too):

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

Dragging/resizing snaps to the grid on release. If the drop position would
overlap another widget or go out of grid bounds, it's rejected and the
widget snaps back to its last valid position — see
`DashboardGrid::isPlacementValid` in
[dashboardgrid.cpp](../lib/dashboard/dashboardgrid.cpp).

## Adding a new widget type

1. Subclass `DashboardWidget`
   ([lib/dashboard/dashboardwidget.h](../lib/dashboard/dashboardwidget.h))
   and implement `paintEvent` (or whatever rendering the chart needs) —
   see the 3 placeholder widgets in
   [lib/dashboard/dummywidgets.h](../lib/dashboard/dummywidgets.h)/`.cpp`
   for the pattern.
2. Register it in `WidgetRegistry`'s constructor
   ([lib/dashboard/widgetregistry.cpp](../lib/dashboard/widgetregistry.cpp)):
   `registerType({"my_type", "My Type", [](QWidget* parent) { return new MyWidget(parent); }});`

That's it — the **Type** dropdown in the properties panel enumerates
`WidgetRegistry::availableTypes()`, so a new type shows up automatically.
No grid or `MainWindow` code needs to change.

The 3 widgets currently registered (`dummy_line`, `dummy_bar`,
`dummy_gauge`) are throwaway placeholders meant to exercise the grid
mechanics before real telemetry charts exist — replace/remove them once
real chart types land.

## Project file

`ProjectStore` ([lib/project/projectstore.h](../lib/project/projectstore.h))
persists the app's project state as a `.tvproj` JSON file made of
independent top-level sections:

```json
{
  "traceview": { "formatVersion": 1 },
  "dashboard": {
    "items": [
      { "id": "...", "type": "dummy_line", "name": "", "key": "", "x": 0.0, "y": 0.0, "width": 0.3333, "height": 0.25 }
    ]
  }
}
```

Only the `dashboard` section exists today. To persist a new kind of
config later (e.g. a serial connection profile), call
`ProjectStore::instance().setSection("connection", {...})` with a new
top-level key when saving, and read it back via `section("connection")`
after a load — `ProjectStore` itself doesn't need to change for this.

**Save Project** / **Open Project** in the toolbar drive this: Save reuses
the last path once one is chosen (prompting via a file dialog only the
first time), Open always prompts.
