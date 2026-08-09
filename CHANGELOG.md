# Changelog

All notable changes to TraceView are documented here, in the style of
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions follow
[Semantic Versioning](https://semver.org/) — see `CONTRIBUTING.md` for the
release flow.

## [Unreleased]

### Added

- Theme system (`ThemePalette`, `ThemeManager`) with Dark/Light templates,
  selectable from **View → Theme**, persisted via `QSettings`.
- Configurable dashboard grid (`DashboardGrid`): add/remove widgets,
  drag/resize them into a 12-column grid, lock the layout, all gated
  behind a "Configure Layout" edit-mode toggle.
- Project save/load (`ProjectStore`) to `.tvproj` JSON files, structured
  as independent extensible sections (only `dashboard` exists so far).
- Ribbon-style **Configure Project** tab (icon buttons for edit-mode
  toggle and adding widgets) and a disabled **Run** tab placeholder for
  future serial-port configuration.
- **File**, **View**, and **About** menus; the About dialog shows the app
  version and the Qt version used to build/run it.
- 3 placeholder chart widgets (Line, Bar, Gauge) used to exercise the
  grid before real telemetry visualizations exist.
- Event/command settings for the control widgets (Push Button: on
  press/release commands, momentary vs. pulse mode, repeat-while-held,
  long-press action, debounce, confirm-before-sending; Toggle Switch:
  on/off commands, confirm-before-toggling; Slider: command template,
  continuous-vs-on-release send mode with throttle) and a config editor
  for the Gauge widget (value source, fixed min/max, unit, decimals).
