# Changelog

All notable changes to TraceView are documented here, in the style of
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions follow
[Semantic Versioning](https://semver.org/) — see `CONTRIBUTING.md` for the
release flow.

## [Unreleased]

## [2.2.0] - 2026-08-21

### Added

- **Logs tab** — opens a bally_OS `.blog` file (the robot's SD-card event
  log: a headerless, back-to-back sequence of BTP v1 `Log` frames) and
  lists every decoded entry in a table, one row per message, showing its
  raw `timestamp_us`, severity, source/boot id, sequence and text.
  `LogFileReader` (`lib/protocol/logfilereader.h`) decodes each frame
  against the EspNow transport profile and reassembles multi-fragment
  messages the same way the firmware wrote them (sequential, in order); a
  corrupted or truncated frame is skipped rather than failing the whole
  file. `LogViewer` (`lib/logs/logviewer.h`) is the read-only table itself,
  wired into a new Ribbon tab alongside Run/Layout/Devices — no undo stack,
  nothing persisted into `.tvproj`, since opening a file is the only state.
- **USB HID transport** — a device can now connect over USB HID instead of
  a serial port: a new "Transport" combo in Add/Edit Device (`Device
  Config Dialog`) toggles between the port/baud/line-terminator fields
  (Serial) and a USB device picker (UsbHid), speaking BTP v1.1.0's
  `usb_hid` profile (BTP ADR 0011) to a dongle that exposes a composite
  CDC+HID USB device. `UsbHidManager` (`lib/core/usbhidmanager.h`) is the
  `hidapi`-backed transport — reports are `[report_id][valid_length]
  [payload...padding]`, since a fixed-size HID report always sends its
  full byte count zero-padded, so the explicit length prefix is what lets
  the receiver tell real data from padding; reading runs on its own
  polling thread, `hidapi` having no `readyRead` equivalent. `BtpSession`
  now takes a `btp::TransportProfile` and branches its decode path on it —
  Serial mode keeps incremental COBS decoding, UsbHid mode decodes each
  already-bounded HID report directly, no COBS — both still share the same
  fragment reassembler. USB HID devices have no console/raw-byte channel
  at all, so raw-text control-widget commands go nowhere for them, same
  "went nowhere" contract a closed serial port already had; the serial
  monitor widget is unaffected since its inbound/outbound path already
  runs over a real BTP `TERMINAL` frame under both transports. See
  `docs/DEVICES.md` and `docs/PROTOCOL.md`.

### Changed

- Devices tab: `DeviceConfigDialog`'s "Reported by device" fields
  (`btpVersion`/`btpId`) are now read-only, populated from the actual BTP
  handshake (`HELLO_RESULT`'s `selected_version`/`source_id`,
  `BtpBackend`/`Backend::deviceIdentified`) instead of being freely-typed
  text — and, like `connected`, no longer persisted into `.tvproj` since
  they're live session state, not configuration. Dropped the `chipType`
  field: nothing in the BTP protocol as implemented reports a chip/model,
  so there was no real data to back it. See `docs/DEVICES.md`.

## [2.1.1] - 2026-08-18

### Changed

- `DashboardCell`'s idle `palette.border` outline is now skipped for
  headerless controls (push button/toggle switch/slider,
  `widgets/controlwidgets.cpp`) — they already read as bare controls rather
  than cards, and the outline fought that. Headered kinds (chart, gauge,
  serial monitor) keep the outline; both still pick up the accent selection
  outline. See `docs/VISUAL_IDENTITY.md`.

### Removed

- Hid the **Debug** menu (chart-performance debug window) from the menu
  bar — not meant for end users. The window and its code are untouched
  (`DebugChartsWindow`, `onDebug()`), just not reachable from the UI for now.

## [2.1.0] - 2026-08-18

### Added

- **Devices tab** (`DevicesGrid`, alongside **Run** and **Layout**): devices
  are now managed as their own first-class list instead of a single
  port/baud pair on the Run tab. Each `DeviceCard` shows a name, live
  connection dot, and a gear button opening `DeviceConfigDialog` to edit
  its connection (port + refresh, baud, line terminator), description, and
  BTP manifest fields. **Add Device**/**Remove Device** mirror the Layout
  tab's own Add/Remove pair; devices persist into `.tvproj` under a new
  `devices` section (see `docs/DEVICES.md`).
- **Multiple simultaneous device connections**: each device now owns an
  independent `DeviceConnection` (its own `SerialManager` + `Backend`), so
  several BTP sessions can run side by side instead of one shared
  connection for the whole project. Connecting is ambient — a configured
  device retries its port every few seconds in the background until it
  comes online, and silently recovers from an unplug/replug with no user
  action needed.
- **Per-widget device targeting**: every dashboard widget that talks to a
  device (chart, gauge, push button/toggle/slider, serial monitor) now has
  its own "Device" picker in its config editor — there's no single active
  device for the whole project anymore, and each cell's header status dot
  reflects its own device's connection state.
- Undo/redo now tracks the Devices tab too: adding, removing, and editing
  a device is its own undoable step, on a separate stack from the
  dashboard's so Ctrl+Z/Ctrl+Y always act on whichever tab is visible
  (`QUndoGroup`).
- **Ctrl+Tab** / **Ctrl+Shift+Tab** cycles forward/backward through a
  project's workspaces from anywhere in the window, no need to open the
  workspace switcher first.

### Changed

- The Run tab no longer hosts a port/baud/connect bar — that configuration
  moved to the Devices tab. Run now shows a read-only strip of every
  configured device's name and connection dot instead.
- Line chart rendering switched from `QPainterPath` to a plain
  `QPolygonF`/`drawPolyline()`, plus removed a few redundant per-frame
  recomputations — noticeably cheaper to repaint for series with 100+
  points (see `tools/chart_benchmark`).

## [2.0.0] - 2026-08-17

### Added

- Multiple workspaces per project (`WorkspaceManager`, `WorkspaceSwitcher`):
  a project now holds N independently named dashboard layouts, switchable
  from a button in the status bar. Each workspace wraps its own
  `DashboardGrid` JSON payload under the new `workspaces` section of the
  `.tvproj` format; opening a project saved before workspaces existed
  migrates its single layout into one `"Default"` workspace.
- Multi-select and grouping on the dashboard grid: Ctrl-click or a
  rubber-band drag over empty space selects several widgets at once, and
  **Group**/**Ungroup** (`DashboardGrid::groupSelected`/`ungroupSelected`)
  locks a selection's positions together so grouped widgets always select,
  move, and resize as one rigid unit — undoable like any other grid edit.
  The **Layers** panel reflects multi-selection and group membership.
- Dockable **Layers**/**Properties** panels (`PanelDockController`,
  `DockablePanel`, `DockDropIndicator`, `DockResizeGrip`): both panels can
  now be dragged to any edge of the canvas or pulled off into a floating
  window, and resized once there. Each panel's position/size persists via
  `QSettings` across restarts. This intentionally avoids
  `QMainWindow`/`QDockWidget` so the canvas never resizes to make room for a
  docked panel.
- 7 new color themes alongside Dark/Light: **Wood**, **Black**, **Matrix**,
  **Synthwave**, **Amber**, **Arctic**, **Sakura** (see
  `docs/THEMING.md`).
- Font selection (**View → Font**), independent of the color theme
  (`FontManager`): System Default, Consolas, Georgia, Verdana — applied
  directly to `QApplication` so any font pairs with any theme.
- Redesigned **Toggle Switch** control widget: an animated,
  iOS/Android-style slide switch (`ToggleSwitch`) replacing the previous
  plain push-button look.
- **Donate** dialog (menu bar): a Pix QR code (bundled `qrcodegen` vendor
  library) plus an international donation note.

### Changed

- Ribbon icons reworked for visual consistency across the new themes.
- Push Button, Toggle Switch, and Slider control widgets now dim/adjust
  their appearance in dashboard edit mode (`setEditModeHint`).

## [1.0.3] - 2026-08-17

### Added

- Language switching (**View → Language**): a `LanguageManager` registers
  selectable UI languages, persists the choice via `QSettings`, and installs
  the matching translator (plus Qt's own base translation, so native dialog
  chrome follows along) on restart.
- Translations covering the UI/dashboard/protocol layers for Portuguese
  (Brazil), Spanish, French, German, Italian, Russian, Chinese (Simplified),
  and Japanese (`translations/traceview_*.ts`).

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
