# Changelog

All notable changes to TraceView are documented here, in the style of
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions follow
[Semantic Versioning](https://semver.org/) — see `CONTRIBUTING.md` for the
release flow.

## [Unreleased]

## [1.0.2] - 2026-08-16

### Added

- Theme system (`ThemePalette`, `ThemeManager`) with Dark/Light templates,
  selectable from **View → Theme**, persisted via `QSettings`.
- Configurable dashboard grid (`DashboardGrid`): add/remove widgets,
  drag/resize them into a grid, lock the layout, all gated behind a
  "Configure Layout" edit-mode toggle.
- Dashboard layering: widgets may now overlap (only out-of-bounds placement
  is rejected), stacking order is the item's position in the project's item
  list, and four new ribbon actions (**To Front** / **Forward** /
  **Backward** / **To Back**) reorder it, undoable like any other grid edit.
  A **Layers** panel (`LayersPanel`) lists every item on the grid, front-most
  first, and selecting a row selects/raises that item on the canvas.
- Pinnable **Properties** and **Layers** side panels: pinning a panel keeps
  it visible on the Layout tab even with nothing selected; unpinned, it only
  shows while something is selected.
- Project save/load (`ProjectStore`) to `.tvproj` JSON files, structured
  as independent extensible sections (only `dashboard` exists so far).
- Ribbon-style **Configure Project** tab (icon buttons for edit-mode
  toggle and adding widgets) and a disabled **Run** tab placeholder for
  future serial-port configuration.
- **File**, **View**, and **About** menus; the About dialog shows the app
  version and the Qt version used to build/run it.
- 3 chart widgets (Line, Bar, Gauge) used to exercise the grid before real
  telemetry visualizations exist; line chart markers and legend fixes.
- Event/command settings for the control widgets (Push Button: on
  press/release commands, momentary vs. pulse mode, repeat-while-held,
  long-press action, debounce, confirm-before-sending; Toggle Switch:
  on/off commands, confirm-before-toggling; Slider: command template,
  continuous-vs-on-release send mode with throttle) and a config editor
  for the Gauge widget (value source, fixed min/max, unit, decimals).
- Debug menu (**Show synthetic data**, **Show statistics**) and a debug
  charts window for exercising chart layouts without live telemetry.

### Changed

- Telemetry access now goes through an abstract `Backend` interface
  (`lib/backend/backend.h`) instead of BTP-specific classes directly —
  `MainWindow` only knows `Backend`, with `BtpBackend` as the sole
  implementation today. See the README's "Architecture" section.
