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

That's it — the "Add Widget" picker in `MainWindow` enumerates
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
      { "id": "...", "type": "dummy_line", "x": 0.0, "y": 0.0, "width": 0.3333, "height": 0.25 }
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
