# Changelog

All notable changes to TraceView are documented here, in the style of
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions follow
[Semantic Versioning](https://semver.org/) — see `CONTRIBUTING.md` for the
release flow.

## [Unreleased]

### Added

- **Hub children recover on their own.** A device behind a hub never
  handshakes, so a robot rebooting used to leave its card reading
  "connected" while its charts silently went dead until the operator
  reconnected it by hand. Now, while at least one hub child is connected,
  TraceView subscribes to that hub's `hub.peers` topic and reconciles it
  against every child once a second:
  - a robot that stays silent for ~8 s (its `hub.peers` `online` flag off,
    debounced so a busy control loop or one missed `STATUS` doesn't flap
    the card) paints the card amber — "robot not responding" — without
    touching the link to the hub itself;
  - a **boot_id change** (the robot power-cycled, so its per-boot
    subscription state is gone) re-requests that robot's catalog, and once
    the fresh `MANIFEST_DATA` lands, every subscription for it is re-sent
    against the new boot. A robot that merely dropped out of range and came
    back on the *same* boot needs nothing — its catalog is still valid and
    its subscriptions self-heal on the next lease renewal, so this
    deliberately does **not** re-request on every online blip (doing so
    turned a flaky link into a `MANIFEST_REQUEST` storm that could stall
    the hub's serial link).

  `BtpBackend::onPeerPresence()` is the new hook MainWindow feeds;
  `SubscriptionManager::onPeerRebooted()` is the per-source re-subscribe.
  A `Device` grew live-mirrored `peerOnline`/`peerBootId` fields (not
  persisted, same as `connected`), and `DeviceLinkState` a `PeerStale`
  state. The child's catalog retry timer no longer stops once the catalog
  arrives — it slows to a 20 s backstop. See the new "Talking to TraceView"
  section in the README for the device-side contract this assumes.

- **A hub child gets its catalog without a robot reset.** Two gaps closed
  after bench testing: `onPeerPresence()` now also re-arms the fast catalog
  retry when the robot is reported online but *no* catalog has ever
  arrived (not only on a boot_id change — which can't be detected until a
  first catalog sets the baseline), and while no catalog has arrived the
  retry backs off to 8 s rather than the 20 s reboot-backstop. Separately,
  the console/hub backend re-runs the dongle's full enumeration on its
  keepalive tick while its own catalog is still empty — the single
  enumeration on session-established can be lost, and without the dongle's
  `hub.peers` schema a child's presence and "Source ID" list never resolve.

### Internal

- `test_manifestclient` covers the last piece of pure protocol logic that
  had none: when a MANIFEST_REQUEST is worth sending (the config-revision
  gate that makes a reconnect cheap, the wildcard guard that keeps "ask
  this robot" from becoming "enumerate everything", the per-source
  cooldown that stops an unknown-schema sample stream flooding the link),
  and a bounds-checked walk over the response — every truncation point,
  an inconsistent `record_size`, an unsupported `manifest_format_version`,
  and NOT_MODIFIED carrying topic records that must not be applied.

## [2.3.0] - 2026-08-25

### Added

- **Hub channels** — a third `TransportType` (`HubChannel`) for a device
  that has no wire of its own and instead multiplexes over *another*
  device's connection. This is what turns the dongle from a cable into a
  hub: the desktop opens one connection to it and talks to it as an
  ordinary BTP device, and every robot behind its radio becomes its own
  `Device`, with its own manifest, charts and terminal, riding that same
  single cable.

  `HubTransport` (`lib/core/hubtransport.h`) is a third `Transport`
  implementation, so nothing above it had to learn a new shape. Inbound,
  the parent's `BtpSession` offers every decoded frame's raw octets tagged
  with its header's `source_id` and each child claims the ones matching its
  own robot's — that single comparison is the whole demux, with no routing
  table and no per-message-type case. Outbound, the child encodes under the
  ESP-NOW profile and the parent adds only the cable's framing. Nothing in
  either direction re-encodes, re-fragments or recomputes a CRC, which is
  what lets an end-to-end seal verify at the far end: the parent holds no
  key for the traffic it carries. Configured by `parentDeviceId` plus
  `peerSourceId` — the robot's permanent BTP address, deliberately *not*
  the dongle's `hub.peers` channel index, which is assigned in the order
  peers were first heard and would silently re-point a saved project at a
  different robot after a dongle reboot. See `docs/DEVICES.md`.
- **Live robot picker** — the "Robot source_id" field in Device Settings
  lists the peers the hub has actually heard (channel, address,
  online/offline with an age, MAC in the item's tooltip), decoded from the
  dongle's own `hub.peers` telemetry topic and re-polled at 1 Hz for as
  long as the dialog stays open. The topic and its six fields are resolved
  out of the hub's catalog **by name**, never by a hardcoded id:
  telemetry.md section 1 makes both local to a source's namespace, so they
  are the dongle's to renumber and only the names are a contract between
  the repositories. Reassembly lives in `HubPeerAccumulator`
  (`lib/devices/hubpeeraccumulator.h`), below the UI layer and tested
  without a QWidget. The combo stays editable, so a robot the hub hasn't
  heard yet can still be addressed by hand.
- **OTA Update tab** — push a firmware `.bin` to a robot over Wi-Fi
  (File → Upload Firmware (OTA)…), talking to bally_OS's `lib/OTAUpdater`
  HTTP side channel: `GET /status` for live reachability and the version
  each robot reports it is running, `POST /update` with the raw body for
  the upload, with a progress bar per row. Passwords are typed per device
  and only persisted into `.tvproj` if "Remember" is ticked. `*.local`
  addresses are resolved by our own multicast query (`MdnsResolver`)
  before Qt would hand them to the OS resolver — the path that simply
  fails on Windows without Bonjour installed — falling back to the OS
  resolver if that gets no answer. Polls only while the tab is visible.
  See `docs/OTA.md`.
- **Sealed channels** — `include/bally_channels.h`, the table answering
  "whose message is this, and which key opens it", now exists here as the
  third of three byte-identical copies (bally_OS, bally_dongle,
  TraceView), each guarded by a SHA-256 committed beside it. BTP has no
  key-id field on the wire, so that agreement is product convention rather
  than protocol — and three unenforced copies would drift silently, the
  first device added after a divergence simply not working.
- Per-widget device pickers in the chart/gauge config editors now resolve
  `sourceId`/`topicId` against each device's announced catalog, showing
  readable topic and field names instead of bare hex.

### Changed

- **Device Settings** — Name/Description moved into a "General" group, so
  they are no longer the only fields in the dialog without one; both gained
  tooltips. The reported-catalog block now prints each field's numeric id
  alongside its name: the manifest's human-readable name is a convenience
  TELEMETRY.md asks for, not a guarantee, so the id a field is actually
  addressed by on the wire stays visible for cross-checking. The catalog
  also refreshes when `MANIFEST_DATA` actually arrives rather than only on
  next open — it lands after the handshake, so an open dialog used to show
  an empty list.
- `DeviceCard` dropped its comm-type label line. With only `CommType::Btp`
  existing, it and the reported line below it both printed the bare word
  "BTP"; the reported line now leads with "v"/"ID" instead of repeating it.
- The whole repository is now `clang-format` clean, in one mechanical
  commit listed in `.git-blame-ignore-revs`. `CONTRIBUTING.md` had asked
  for this since it was written, but nothing enforced it and two
  conventions had grown side by side. `scripts/check_style.py` also covers
  `tests/` and `tools/` now, and excludes `lib/vendor` and
  `include/bally_channels.h` — code whose bytes belong to someone else.

### Fixed

- **OTA status polling cancelled itself.** A repeat `checkStatus()` aborted
  the request already running, and the tab polls every second while a
  request is allowed four — so any device answering slower than the poll
  interval had every attempt cancelled by the next tick and never resolved
  once, leaving its row on "Checking…" indefinitely with no error tooltip
  to explain it. Repeat polls now coalesce into the request in flight. That
  failure hit hardest exactly where it mattered most: a host that is up but
  slow to answer.
- **The robot's reported firmware version was parsed and thrown away.** It
  now has its own column, cleared when the device is unreachable.
- **mDNS A-records were parsed through signed overflow.** The first octet
  was shifted left by 24 as an `int`, which is undefined for any value
  ≥ 128 — that is every address in 128.0.0.0/1, including the 192.168.x.x
  range a robot on a home network actually gets. Widened to `quint32`
  first, matching the idiom used everywhere else in this codebase.
- `checkStatus()` with an empty address reported unreachable with an empty
  tooltip, making an unconfigured device indistinguishable from an
  unreachable one — the exact thing that tooltip exists to prevent.

### Performance

- `TelemetrySeriesBuffer::values()` is cached instead of rebuilt. A chart
  calls it once per series on every paint frame, and it was allocating and
  copying the whole series each time: 2000 reads of a 5000-sample buffer
  measured 199ms, against 13ms for the 400k appends that filled it — the
  read path costing an order of magnitude more than the write path it
  exists to serve. Cached, the same 2000 reads are unmeasurable.
- Opening a `.blog` no longer relayouts per cell or measures the whole file
  to size its columns. A robot's SD-card log runs to tens of thousands of
  entries, and both costs scaled with it.

### Removed

- The **synthetic device** tool (`tools/synthetic_device`) and its
  `docs/SYNTHETIC_DEVICE.md`. Real hardware and the hub made it redundant.
- `commTypeLabel()`, `DeviceConnection::hubTransport()` and
  `DashboardCell::isResizable()` — no callers. The first also left a stale
  comment describing `backend()` as HubChannel-only, which it is not.

### Internal

- `MainWindow` had declared `hubPeersFor()`, `onHubPeerFieldSample()` and
  `deviceSelfSourceId()` in its header, with doc comments describing all
  three, and defined none of them — it compiled because they are private
  members nobody called, so moc never referenced them. All three now exist,
  and `refreshPropertiesPanelDevices()` calls `deviceSelfSourceId()`
  instead of repeating its branch inline, as its comment already claimed.
- New test suites: `test_hubpeeraccumulator`, `test_otaclient`,
  `test_clocksync`. `ClockSync` was one of two protocol modules with no
  coverage at all despite being pure logic; its reply correlation and its
  drift decision (in both directions) are now pinned. 32 suites total.

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
