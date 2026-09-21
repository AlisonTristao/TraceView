# Changelog

All notable changes to TraceView are documented here, in the style of
[Keep a Changelog](https://keepachangelog.com/en/1.0.0/). Versions follow
[Semantic Versioning](https://semver.org/) — see `CONTRIBUTING.md` for the
release flow.

## [Unreleased]

### Added

- Developer/User access mode: the app now always starts in a restricted User
  mode, unlockable via a new **Access** menu login (username/password
  against locally registered accounts, managed from the same menu). Every
  install/device ships with the same default account out of the box (no
  first-run setup step) — change its password or add other accounts via
  Manage Users. In User mode the Devices tab, the dashboard edit-mode lock,
  the Layers/Properties panel toggle, and workspace creation/deletion are
  hidden; switching between existing workspaces, themes and language stays
  available. Developer accounts persist across restarts; the logged-in
  session itself does not.
- Per-screen-size dashboard layouts: each dashboard item now stores an
  independent position/size for three breakpoints (Phone/Tablet/Notebook)
  instead of one shared layout, selected manually via a new toolbar button
  (Developer mode only, next to the edit-mode lock) that also resizes the
  TraceView window itself to approximate that device's shape (clamped to
  fit the current screen, restored on returning to Notebook or leaving
  Developer mode). In User mode the breakpoint instead follows the real
  screen automatically. A +/− pair next to that button (hidden on Notebook)
  lets a Phone/Tablet layout grow taller than the window one step at a time,
  scrolling instead of being squeezed to fit once it exceeds the window;
  off by default, and persisted per breakpoint alongside its layout.
  Projects saved before this feature keep the same layout on all three
  sizes until customized.

## [3.3.19] - 2026-09-20

### Fixed

- Hub devices could keep a red status dot while telemetry continued arriving
  after the hub connected or reconnected. Reattaching children during a parent
  connection notification now preserves their connection-state transitions.
- The device status bar now refreshes after connection and peer-presence state
  changes, so its indicators reflect the updated state.

## [3.3.18] - 2026-09-19

### Fixed

- Automatic update checks now run on every launch when enabled, five seconds
  after startup. Removed the 24-hour interval that previously prevented a new
  check after a recent successful one. Skipped versions remain hidden during
  automatic checks.

## [3.3.17] - 2026-09-19

### Fixed

- Ribbon, settings and dashboard icons looked blurry on any display scaled
  above 100% (the Windows default). `iconutils::tintedPixmap` rasterized each
  SVG at the exact device-independent icon size (16-18px), so Qt then
  upscaled that single low-res bitmap to fill the larger physical button area
  on HiDPI screens, softening every edge. It now renders at 4x that size and
  tags the pixmap with a matching device pixel ratio, giving Qt native
  resolution to draw from instead of stretching a small bitmap.
- Six ribbon icons (arrows, plus/minus, fullscreen) used a heavier
  `stroke-width="2"` than the rest of the set (1.4-1.6), making them read as
  disproportionately bold and harder to tell apart from one another at a
  glance. Normalized them to 1.5 to match the rest of the icon set.

## [3.3.16] - 2026-09-19

### Fixed

- `flatpak install` fetched the signed summary fine but then failed pulling
  the app commit itself: "GPG verification enabled, but no signatures
  found". `flatpak build-update-repo --gpg-sign` only signs the summary
  file; the commits it lists are signed separately when `flatpak-builder`
  exports them into the repo, which never received `--gpg-sign`. Pass the
  same GPG args to the `flatpak-builder --repo=flatpak-repo` build step.

## [3.3.15] - 2026-09-19

### Fixed

- With `secrets: inherit` finally in place, the publish job reached static
  delta generation for the first time since v3.3.1 and hit
  `error: Listing refs: opendir(refs/remotes): No such file or directory`.
  The gh-pages-restore step copies the previously published ostree repo with
  `cp -a`, but git never stored `refs/mirrors`/`refs/remotes` because they
  were empty directories -- `flatpak build-update-repo --generate-static-deltas`
  expects them to exist alongside `refs/heads`. Recreate both after the
  restore copy.

## [3.3.14] - 2026-09-19

### Fixed

- The `linux-flatpak` job in the release workflow calls `flatpak.yml` as a
  reusable workflow (`workflow_call`) but never declared `secrets: inherit`,
  so `TRACEVIEW_FLATPAK_GPG_PRIVATE_KEY` reached the job as an empty string
  regardless of how the secret was configured -- `gpg --import` failed with
  "no valid OpenPGP data found" on every attempt. The rotated key from 3.3.13
  was never actually broken. Added `secrets: inherit` to the caller job.

## [3.3.13] - 2026-09-19

### Fixed

- The Flatpak signing key added in 3.3.12 never reached the CI job -- the
  `TRACEVIEW_FLATPAK_GPG_PRIVATE_KEY` secret was empty or misnamed, so
  `gpg --import` failed with "no valid OpenPGP data found" and the publish
  job never signed the repository. Rotated to a fresh signing key (the
  previous private key was already discarded and could not be recovered)
  and re-embedded the new public key in `TraceView.flatpakref` and
  `traceview.flatpakrepo`.

## [3.3.12] - 2026-09-19

### Fixed

- The published Flatpak repository was never GPG-signed
  (`flatpak build-update-repo` ran without `--gpg-sign`), but
  `TraceView.flatpakref`/`traceview.flatpakrepo` had no `GPGKey=`, so
  `flatpak remote-add` defaulted to requiring a verified summary that could
  never exist -- `flatpak install` failed with "GPG verification enabled,
  but no summary found" even though the repository itself was reachable.
  The release workflow now signs the repository with a dedicated key
  (`TRACEVIEW_FLATPAK_GPG_PRIVATE_KEY` secret) when publishing, and the ref
  files embed the matching public key.

## [3.3.11] - 2026-09-19

### Fixed

- The release workflow's `publish` job never checked out the repository, so
  it failed copying `packaging/flatpak/TraceView.flatpakref` and the other
  Flatpak Pages files it needs -- a pre-existing gap that only surfaced now
  that the Flatpak job upstream of it finally succeeds again. Added the
  missing checkout step.

## [3.3.10] - 2026-09-19

### Fixed

- Reverted the Linux Flatpak release workflow's attempt to self-host the KDE
  runtime mirror on GitHub Pages: it never reliably cleared CI (ostree ref
  resolution kept failing, then a direct Flathub pull got blocked with an
  HTTP 403). The Flatpak build depends on Flathub for `org.kde.Platform`
  again, as before; the application itself remains fully self-hosted on
  TraceView's own GitHub Pages Flatpak repository.
- `CMakeLists.txt`'s project version had drifted out of sync with the last
  several release tags (stuck at `3.3.0` through tags `v3.3.1`–`v3.3.9`),
  which `UpdateChecker` relies on matching exactly; bumped it back in step
  with the tag this version is released under.

## [3.3.0] - 2026-09-17

### Added

- Linux Flatpak packaging with the KDE runtime, pinned offline build dependencies,
  application metadata, and CI bundle/smoke jobs alongside the existing AppImage.
- Flatpak-managed updates: the Flatpak build disables the GitHub self-updater
  and directs users to their software manager or `flatpak update`.
- The official Flatpak channel is now a project-owned repository published to
  GitHub Pages, with `.flatpakref` and `.flatpakrepo` installation links.

## [3.2.9] - 2026-09-17

### Fixed

- Release packaging on Ubuntu 22.04 now uses the correct runner-specific Qt
  and system dependency setup, avoiding mismatches between the 22.04 build
  host and the newer 24.04 assumptions in the appimage workflow.

## [3.2.8] - 2026-09-17

### Changed

- Added the Qt SVG development package to the release and build jobs so the
  app's vector icon resources are available during CI validation and final
  package creation.

## [3.2.7] - 2026-09-17

### Changed

- Switched the app's ribbon and settings glyphs to SVG assets, and defaulted
  the Linux AppImage runtime to the XCB platform plugin for more reliable
  desktop startup behavior.

## [3.2.6] - 2026-09-17

### Fixed

- Corrected the project version metadata for the 3.2.6 release so the
  packaged build and version checks line up with the tagged release.

## [3.2.5] - 2026-09-17

### Fixed

- Polished the chart and control configuration editors plus the device
  configuration dialog so dashboard editing and per-device settings remain
  consistent during release validation.

## [3.2.4] - 2026-09-17

### Added

- Added a Windows installer smoke test to the release workflow, and tightened
  the Ubuntu 22.04 packaging checks so package validation covers the supported
  Linux runner more reliably.

## [3.2.3] - 2026-09-17

### Changed

- Updated the release workflow to the newer Qt package version used by the
  packaging and build environment.

## [3.2.2] - 2026-09-17

### Fixed

- Release CI's Linux package job failed to configure: OpenGL dev headers
  were missing (`libgl1-mesa-dev`), and `find_package(Qt6 ... LinguistTools)`
  hard-failed because Debian/Ubuntu's `qt6-l10n-tools` package never ships
  the `lprodump` tool that `Qt6LinguistToolsTargets.cmake` unconditionally
  checks for. Added `libgl1-mesa-dev` and a CI-only symlink stub for
  `lprodump` (unused by this project, no `.pro` files).

## [3.2.1] - 2026-09-17

### Fixed

- Release CI's Linux package job installed `qt6-serialport-dev`, which
  doesn't exist on the `ubuntu-22.04` runner (only from `noble`/24.04
  onward); switched to `libqt6serialport6-dev`, the correct package on
  jammy.

## [3.2.0] - 2026-09-17

### Changed

- Linux packaging migrated from a manually-bundled `.tar.gz` (CPack TGZ +
  `scripts/collect_linux_deps.sh`) to an `.AppImage` built with
  `scripts/build_linux_appimage.sh` and linuxdeploy/linuxdeploy-plugin-qt.
- The self-updater's Linux install step now replaces the running AppImage
  in place (verify checksum, atomic swap, relaunch) instead of extracting
  a tarball over the install directory. Automatic installation requires
  running as an AppImage from a writable location; other cases are sent
  to the release page to download the AppImage manually. Older `.tar.gz`
  installations need a one-time manual migration to the AppImage.

## [3.1.3] - 2026-09-16

### Fixed

- A floating Layers/Properties panel didn't move along with the main
  window while it was being dragged (including onto a different
  monitor) -- it's a separate top-level window, and nothing was
  repositioning it. 3.1.2 only anchored a floating panel's saved
  position across restarts; it stays anchored to the window through a
  live drag now too.

## [3.1.2] - 2026-09-15

### Added

- "Reset Panel Positions" (View menu): puts the Layers and Properties
  panels back at their default dock edge and thickness, for one dragged
  somewhere unreachable (e.g. off every screen).

### Fixed

- A floating Layers/Properties panel's saved position was raw screen
  coordinates, so a monitor layout change (a second monitor added or
  removed, resolution change, etc.) could open it off-screen or on the
  wrong monitor. Position is now stored relative to the main window
  instead, so it stays anchored to TraceView regardless of monitor setup.

## [3.1.1] - 2026-09-15

### Fixed

- `formatHexId()` uppercased the `0x` prefix along with the hex digits
  (`"0X11223344"` instead of `"0x11223344"`), affecting the Robot Log
  widget's Source ID/Boot ID columns and the sourceId/topicId fields in
  the chart, gauge and text board config editors.

## [3.1.0] - 2026-09-15

### Added

- Robot Log widget (`robot_log`): a read-only "serial monitor" for one or
  more robots' LOG channels, with the same tab-per-device strip as Serial
  Monitor. Rows show Timestamp/Severity/Source ID/Boot ID/Sequence/Message,
  colored by severity, bounded to a fixed history so a long-running session
  doesn't grow without limit.

## [3.0.0] - 2026-09-14

### Added

- Unified the Layout and Run workspace tabs into the Dashboard tab.
- Added a Dashboard editing toggle to enable or disable chart layout and
  configuration changes.

c## [2.9.1] - 2026-09-13

### Fixed

- Pressing Tab in the serial terminal to trigger the dongle shell's
  autocomplete moved keyboard focus to the next widget instead of sending
  the keystroke -- `QPlainTextEdit::focusNextPrevChild()` only blocks Qt's
  default Tab-focus-change while the widget is editable, and this terminal
  is read-only by design. `SerialTerminalWidget` now overrides
  `focusNextPrevChild()` to keep Tab/Shift+Tab as terminal input.

## [2.9.0] - 2026-09-13

### Added

- Chart widgets can group their series into multiple Y axes automatically,
  by unit -- an "Automatic axis" toggle in the chart's config editor groups
  series by the unit their bound field reports, stacks one auto-ranged axis
  per distinct unit to the left of the plot (each with its own vertical
  ruler and min/mid/max ticks), and colors each axis's labels to match the
  series it scales. Off by default, so existing dashboards are unaffected.
- A chart series' Y-axis range now prefers the bound field's own declared
  range (from the device's manifest, when it reports one) over guessing
  from buffered samples -- in both the single shared axis and the new
  per-unit automatic axes above. Requires a device on BTP manifest format
  version 3 or newer; falls back to the existing auto-range behavior
  otherwise.

## [2.8.2] - 2026-09-13

### Fixed

- A subscription pinned above its topic's source-granted max (or below its
  min) re-posted the "limited to X Hz (requested Y Hz)" notification on
  every lease renewal -- every ~7.5 s by default -- for as long as it
  stayed open, instead of once when that fact first became true. Both the
  status bar toast and the Notification History entry now fire only when
  the granted rate actually changes.

## [2.8.1] - 2026-09-13

### Fixed

- A rate-limited or rejected SUBSCRIBE already logged to Notification
  History (View > Notification History) by raw "0x.../0x..." id pair.
  Resolved against the telemetry catalog instead, so the entry reads
  "robot.sensors limited to 50 Hz (requested 200 Hz)" once that topic's
  schema has arrived.

## [2.8.0] - 2026-09-13

### Added

- A "Telemetry subscribe rate" override in Settings > Dashboard: when
  enabled, one rate replaces every widget's own requested rate (chart/text
  board sample time, the gauge's fixed 5 Hz), so the whole dashboard's
  subscribe load can be tuned from a single control instead of each
  widget's config editor. Each topic's own max/min rate on the source
  still applies on top of it.

### Fixed

- The status bar's per-topic summary appended "(limited, asked X Hz)" next
  to every rate-limited topic, which added noise without being actionable
  from that view. Removed; the effective rate alone is shown there now.

## [2.7.1] - 2026-09-13

### Fixed

- Line and bar chart labels (Y-axis min/mid/max, the legend's last-value
  row, grid-point markers, the hover tooltip, and bar values) formatted
  numbers with a variable number of significant digits, so the axis gutter
  and tooltip balloon visibly resized frame to frame as a value's fractional
  digits changed. Added a Decimals setting to the chart properties panel's
  Y Axis section (same 0-6 range the gauge widget's own Decimals field
  already used) so every one of those labels renders at a fixed width
  instead.

## [2.7.0] - 2026-09-13

### Added

- A file-backed diagnostic log: one file per session under the user's
  profile directory (falling back to the system temp directory if that's
  unwritable), covering serial/USB-HID open, close and error events, every
  connection retry, and every status-bar message. Reachable via
  Settings > Diagnostics or the View menu's "Open Log Folder". Raw serial
  byte dumps are an opt-in toggle there, off by default so a high-rate
  device doesn't fill the file fast.

### Fixed

- A serial or USB-HID connection that failed to open (e.g. `Permission
  denied` on Linux when the user isn't in the `dialout`/`uucp` group) left
  the Connect action looking like a no-op, with no error shown anywhere.
  `Transport::errorOccurred` is now surfaced through to the status bar like
  every other connection event.
- A subscription whose SUBSCRIBE or SUBSCRIBE_RESULT was lost over a lossy
  link (e.g. ESP-NOW near a robot's motors) could stay wedged "pending"
  indefinitely with no self-heal. It now retries once the round-trip
  budget elapses.

## [2.6.2] - 2026-09-12

### Fixed

- The Linux `.tar.gz` package's `qxcb` platform plugin failed to load on a
  clean Fedora with "libxcb-icccm.so.4: cannot open shared object file"
  (and the same for `libxcb-image`, `libxcb-keysyms`, `libxcb-render-util`)
  -- reproduced by running the actual v2.6.1 release asset in a bare
  `fedora:44` container. `scripts/collect_linux_deps.sh`'s system-library
  regex treated every `libxcb-*` as part of the base X11/xcb stack the
  target machine is assumed to already have, when these four are actually
  the separate "xcb-util" convenience libraries Qt's own xcb plugin links
  against -- present on the Ubuntu build machine as a `qt6-base-dev`
  dependency, so the gap never showed up building or packaging there, but
  absent from a stock Fedora install. Narrowed the regex to the real X11/xcb
  protocol libraries so these four fall through and get bundled like any
  other Qt-side dependency, same as ICU/harfbuzz/etc. already are. Verified
  by rebuilding the package and running it in a clean `fedora:44` container
  with no Qt or `xcb-util-*` packages installed.

- The same package failed the same way on a clean Arch install, but with
  "libselinux.so.1: cannot open shared object file" instead -- Arch has no
  SELinux support and no `libselinux` package at all (nothing to tell an
  Arch user to install), while the Ubuntu build machine's `libmount`/`libgio`/
  etc. happen to be linked against it, so `collect_linux_deps.sh` had
  `selinux` lumped in with the actual glibc family (`libc`/`libm`/`libdl`/...)
  as always-present. Dropped it from that group so it gets bundled too.
  Verified the same way, in a clean `archlinux/base` container.

## [2.6.1] - 2026-09-11

### Fixed

- The Linux `.tar.gz` package's CI build never installed `qt6-wayland`, so the
  build machine had no Wayland QPA plugin for `collect_linux_deps.sh` to
  bundle -- every 2.6.0 Linux package shipped with `xcb` only, failing to
  start under a native Wayland session (e.g. Fedora/GNOME) with "Could not
  find the Qt platform plugin wayland".

## [2.6.0] - 2026-09-11

### Added

- The Linux `.tar.gz` package now bundles Qt itself, instead of assuming the
  target machine already has a matching system Qt 6 install. It ships next
  to a `lib/` and `plugins/` directory containing everything the app's Qt
  modules pull in (found the same way `windeployqt` does for the Windows
  installer -- by walking the actual shared-library dependency graph, not a
  fixed list), so it now runs on a bare Linux machine the same way the
  Windows installer already ran on a machine without Qt or MinGW installed.
  Still assumes glibc and the X11/Wayland/GL stack a Linux desktop already
  has, the same way the Windows build assumes `user32.dll` and friends.

### Fixed

- The Windows installer's `ldd` safety-net sweep (the one that catches a Qt
  module's own third-party DLL windeployqt doesn't know about -- see the
  2.5.1 entry below) silently did nothing whenever CPack was packaged from
  outside a Git Bash terminal, including `release.yml`'s own Windows runner,
  which defaults to PowerShell. A bare `bash.exe` subprocess never sources
  the profile that puts Git's own `usr/bin` -- where `ldd.exe` lives -- on
  `PATH`, so every release built so far had quietly skipped that check.

## [2.5.4] - 2026-09-11

### Fixed

- Several widgets (the serial terminal's frame among them) could look
  different depending on which Qt build compiled the app,
  because this app draws its whole theme on top of whatever native OS
  style is active (see `theme/stylesheet.cpp`), and any widget property
  that stylesheet doesn't cover fell back to that native style's own
  default -- which isn't guaranteed to render the same across Qt
  distributions (the official Qt installer kit most contributors build
  with locally vs. MSYS2's Qt6 packages, used by `release.yml`). `main.cpp`
  now forces the built-in, OS-independent **Fusion** style at startup, so
  every such gap renders identically everywhere instead of silently
  drifting with whichever Qt build produced the binary.

- On Windows, clicking "Update Now" launched the installer and quit
  TraceView, but nothing ever brought it back -- the installer's own
  Finish page was never configured to relaunch anything, so an update
  just... closed the app. `UpdateInstaller::install()` now writes a small
  PowerShell script that waits for this process to exit, runs the NSIS
  installer fully silently (`/S`, no wizard pages at all), and relaunches
  TraceView from the same path it was already running from. One prompt is
  still unavoidable: Windows' own UAC consent dialog, since the installer
  requires admin rights to write to Program Files -- `/S` only removes the
  installer's *own* wizard, not that OS-level gate.

## [2.5.3] - 2026-09-11

### Fixed

- The self-updater reported "TraceView-*-windows-x64.exe is not listed in
  SHA256SUMS.txt" for every release, because `release.yml`'s Windows
  checksum step wrote its line with `-NoNewline`. With no newline to
  separate it, the `publish` job's `cat sha256-windows.txt sha256-linux.txt
  > SHA256SUMS.txt` glued the Windows line directly onto the start of the
  Linux one -- `UpdateDownloader`'s parser (correctly) only matches a
  filename that is the *last* token on its own line, so the Windows asset's
  line was never found once the two were merged. Removed `-NoNewline`.

## [2.5.2] - 2026-09-11

### Fixed

- The serial monitor's terminal widget could show a visible frame/border
  around it or not, depending entirely on which Qt build compiled the app --
  this app's global stylesheet has no rule for `QPlainTextEdit` at all (see
  `lib/diagram/diagramblockconfigdialog.cpp`'s own long-standing note on the
  same gap), so it fell back to whatever the active native `QStyle` draws by
  default, and that can differ between Qt distributions (e.g. the official
  Qt installer kit most contributors build with locally vs. MSYS2's Qt6
  packages, used by `.github/workflows/release.yml`). `SerialTerminalWidget`
  now forces `QFrame::NoFrame` explicitly, so it looks the same everywhere.

## [2.5.1] - 2026-09-11

### Fixed

- The Windows installer published by `.github/workflows/release.yml` failed
  to start (`libbrotlidec.dll não foi encontrado`) because that build uses
  MSYS2's Qt, whose `Qt6Network.dll` links against a separate Brotli DLL
  that `windeployqt --compiler-runtime` doesn't know to bundle (the official
  Qt installer's kit most contributors build with locally doesn't hit this
  at all). A new `scripts/collect_windows_deps.sh`, run right after
  `windeployqt` during packaging, walks `ldd` over every deployed
  `.exe`/`.dll` and copies in whatever else it resolves outside Windows'
  own system directories -- catching this and any similar gap generically
  instead of hardcoding one more DLL name.

## [2.5.0] - 2026-09-11

### Added

- **Self-update.** TraceView checks GitHub Releases for a newer version on
  startup (at most once a day) and from the new **Updates** category in
  Settings, which also has a "Check now" button and an on/off toggle for the
  automatic check. Finding one only shows a confirm/skip prompt with the
  release notes -- nothing downloads or installs without clicking
  "Update Now". Windows downloads and runs the NSIS installer; Linux extracts
  the `.tar.gz` and swaps it in over the current install directory once
  TraceView quits (falls back to pointing at the release page if that
  directory isn't writable by the current user). Every downloaded package is
  checked against a `SHA256SUMS.txt` published alongside it before anything
  runs. `.github/workflows/release.yml` now builds and publishes both
  packages (as a GitHub Release) whenever a `vX.Y.Z` tag is pushed -- see
  CONTRIBUTING.md's release flow.

## [2.4.0] - 2026-09-02

### Added

- **Settings center** — a Settings tab opened from **File ▸ Settings…**
  (`Ctrl+,`), the same open-on-demand closable tab as the OTA and BTP Traffic
  monitors. It groups application preferences into General, Appearance,
  Dashboard, Terminal, Connections and Diagnostics, each row in the navigation
  list carrying its own glyph. Rendering has Low (15 FPS), Medium (30 FPS),
  High (60 FPS) and Custom profiles; terminal scrollback/wrapping/follow-output/
  cursor, recent project retention, startup auto-connect and reconnect cadence
  are all configurable. Theme and font apply immediately. Language and
  diagnostic history bounds clearly offer an in-app restart because their
  changes are applied when a new application session starts.

### Added

- The serial terminal now renders TinyShell's live command-token colours:
  modules use the theme warning/dark-yellow pen, functions use the accent/blue
  pen, and arguments return to the normal foreground. ANSI 30 and 34 join the
  existing supported SGR subset, including theme-switch retinting.

- Added the **Text Board** dashboard widget (`text_board`) for low-rate,
  formatted UTF-8 telemetry. Each sample replaces the complete board instead
  of appending, fixed-width spacing is preserved, and the font automatically
  shrinks or grows to keep the whole report inside the resized cell. Its
  properties bind a device/source/topic and default to a 3000 ms request
  period (approximately 0.33 Hz). `TelemetryFieldRouter`/`Backend` now expose
  validated whole-topic `UTF8` samples alongside numeric `PACKED_LE` fields.

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
