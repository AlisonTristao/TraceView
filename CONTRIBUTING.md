# Contributing

This document defines the branching model, commit conventions, and release
process for TraceView. It applies to all contributors, including the
maintainer, to keep `main` always in a releasable state.

## Repository layout

| Path | Responsibility |
|---|---|
| `src/` | Application entry point. |
| `lib/core/` | Main window, device connections, transports and UI integration. |
| `lib/backend/`, `lib/telemetry/` | Backend contract and protocol-independent telemetry types. |
| `lib/protocol/` | BTP sessions, handshake, manifests, subscriptions, hub binding and channel sealing. |
| `lib/dashboard/`, `lib/devices/`, `lib/project/` | Widgets, device models, workspace persistence and undoable edits. |
| `lib/diagram/` | Per-device JavaScript runtime/editor and the shelved diagram canvas. |
| `lib/ota/`, `lib/updater/`, `lib/diagnostics/` | Firmware upload, application updates and diagnostics. |
| `tests/`, `tools/` | CTest suites and standalone visual/benchmark tools. |
| `resources/`, `translations/` | Assets and translation catalogs. |
| `docs/`, `scripts/`, `.github/workflows/` | Technical documentation, contributor scripts and CI. |

## Building from source

TraceView builds on Windows and Linux. The first CMake configure needs Git
and internet access: BTP, hidapi and mbedTLS are pinned and fetched
automatically, so they do not need separate checkouts.

Clone the repository and run all configure/build/test commands from its root:

```sh
git clone https://github.com/AlisonTristao/TraceView.git
cd TraceView
```

Common requirements:

- CMake 3.21 or newer and Ninja;
- a C and C++ compiler with C++17 support;
- Qt 6 with Widgets, Network, SerialPort, Qml, LinguistTools and, while tests are
  enabled, Test;
- Python 3.9 or newer only for the contributor scripts under `scripts/` (see "Project
  scripts" below).

Qt Qml provides `QJSEngine` for device scripts; the UI uses Qt Widgets.
The declarative development module is required even when you do not use
scripts. Dependency revisions are defined in the root `CMakeLists.txt`
and the fetched BTP project.

### Windows with MinGW

Install a Qt 6 Desktop MinGW 64-bit kit and the matching MinGW and Ninja tools.
In PowerShell, adjust the example paths to the version installed on the machine:

```powershell
$env:QT_ROOT_DIR = "C:/Qt/6.9.2/mingw_64"
$env:Path = "$env:QT_ROOT_DIR\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:Path"

cmake --preset windows-mingw
cmake --build --preset windows-mingw
ctest --preset windows-mingw
./build/windows-mingw/TraceView.exe
```

The compiler and Qt kit must use the same ABI: a MinGW Qt kit cannot be built
with MSVC, or the other way around. `ctest` needs Qt's DLLs on `PATH` (test
binaries aren't deployed the way `windeployqt` deploys the app below); when
found, `windeployqt` runs after the build and copies the required Qt runtime
beside `TraceView.exe` itself, so the last line above works even without Qt
on `PATH`.

### Windows with Visual Studio

Install Visual Studio 2022 with the **Desktop development with C++** workload
and a Qt 6 `msvc2022_64` kit:

```powershell
$env:QT_ROOT_DIR = "C:/Qt/6.9.2/msvc2022_64"
$env:Path = "$env:QT_ROOT_DIR\bin;$env:Path"

cmake --preset windows-msvc
cmake --build --preset windows-msvc
ctest --preset windows-msvc
./build/windows-msvc/Debug/TraceView.exe
```

### Linux (Debian/Ubuntu)

Install the compiler, Qt development modules and the native hidapi backends:

```sh
sudo apt update
sudo apt install git cmake ninja-build build-essential pkg-config python3 \
    qt6-base-dev qt6-declarative-dev qt6-svg-dev qt6-serialport-dev qt6-tools-dev qt6-l10n-tools qt6-wayland \
    libudev-dev libusb-1.0-0-dev

cmake --preset linux-ninja
cmake --build --preset linux-ninja
ctest --preset linux-ninja
./build/linux-ninja/TraceView
```

Distribution package names vary. When using a Qt installation outside the
system paths, set its root before configuring, for example:

```sh
export QT_ROOT_DIR="$HOME/Qt/6.9.2/gcc_64"
cmake --preset linux-ninja
```

Opening serial and USB HID devices on Linux also requires OS permissions. Add
the user to the distribution's serial-port group (commonly `dialout`) and
install a device-specific udev rule for HID access; log out and back in after
changing group membership.

Tests and developer-only visual tools are enabled by default. They can be
disabled for a smaller application-only build:

```sh
cmake --preset linux-ninja \
    -DTRACEVIEW_BUILD_TESTS=OFF \
    -DTRACEVIEW_BUILD_TOOLS=OFF
```

`CMakeUserPresets.json` is intentionally ignored by Git and can hold
machine-specific overrides without changing the shared presets.

### CI and troubleshooting

[build.yml](.github/workflows/build.yml) configures, builds and runs CTest
on Windows 2025 with MSYS2 UCRT64, Ubuntu 24.04, Fedora 44, Arch Linux and
Linux Mint 22.3. Linux jobs use `QT_QPA_PLATFORM=offscreen`. MSVC is a local
preset, outside this CI matrix. The workflow contains each distribution's
package list.

- If Qt is not found, check `QT_ROOT_DIR` and installed modules. Presets
  pass that root as `CMAKE_PREFIX_PATH`.
- When changing compiler, generator or Qt ABI, use a separate build
  directory through a user preset instead of reusing an incompatible cache.
- If Windows tests cannot load DLLs, put the matching Qt and compiler
  runtime directories on `PATH`; app deployment does not deploy tests.
- On headless Linux, export `QT_QPA_PLATFORM=offscreen` before testing.
  This does not validate native desktop integration.

### Packaging

Produces a Windows NSIS installer or a Linux AppImage from a Release build.
`.github/workflows/release.yml` runs the same steps and publishes the result
as a GitHub Release whenever a `vX.Y.Z` tag is pushed (see "Versioning and
releases" below) -- the commands below are for building a package locally
without cutting a release.

**Windows (NSIS installer)** — requires [NSIS](https://nsis.sourceforge.io/)
for `makensis.exe`, installed once:

```powershell
winget install --id NSIS.NSIS -e
```

Also make `bash` and `ldd` available through Git Bash/MSYS2. During
installation, `scripts/collect_windows_deps.sh` checks third-party DLLs
that `windeployqt` may not collect. Review deployment warnings before
sharing a package.

Then, with the same Qt/MinGW environment as the Windows-with-MinGW build above:

```powershell
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
cd build/windows-mingw-release
cpack -G NSIS
```

The installer bundles the Qt and MinGW runtime DLLs (`windeployqt
--compiler-runtime`), so it runs on a machine without Qt or this MinGW kit
installed.

**Linux (AppImage)**:

Install `curl`, `file`, `patchelf`, `desktop-file-utils`, Qt 6 development
packages, OpenSSL 3 runtime (`libssl3`) and `qt6-wayland`. Qt's `qmake6` must be on PATH (or set `QMAKE`).

```sh
cmake --preset linux-ninja-release
cmake --build --preset linux-ninja-release
bash scripts/build_linux_appimage.sh build/linux-ninja-release
```

The script downloads linuxdeploy and linuxdeploy-plugin-qt into the build
folder, installs into a fresh AppDir and produces
`build/linux-ninja-release/TraceView-<version>-linux-x64.AppImage`.
Set `LINUXDEPLOY` and `LINUXDEPLOY_PLUGIN_QT` to use existing tools.
The Qt plugin bundles libraries, plugins and translations; see its
[configuration](https://github.com/linuxdeploy/linuxdeploy-plugin-qt).
The legacy manual Qt bundling script has been replaced by linuxdeploy.
OpenSSL is included explicitly because Qt loads its TLS backend dynamically.

Release packaging uses Ubuntu 22.04 for its older glibc baseline. Targets
still need compatible glibc and the desktop graphics stack, but no system Qt.
Use `--appimage-extract-and-run` where FUSE is unavailable.

The release workflow gates publication on headless startup smoke tests in
Ubuntu, Fedora, Arch and Mint containers with no system Qt. Run the same check
locally with `bash scripts/smoke_linux_appimage.sh <artifact.AppImage>`.
Before releasing, also check X11/Wayland, translations, networking and device access
on desktop installations.
Test an update between two AppImages in a writable folder: verify the checksum,
replacement at the original path, executable permission and relaunch. Repeat
with spaces/apostrophes in the path and a read-only destination. Development
builds refuse installation. Older TGZ releases require a manual first migration.

## Branch model

TraceView follows a Git Flow variant with two permanent branches:

| Branch    | Purpose                                                                 | Protection |
|-----------|--------------------------------------------------------------------------|------------|
| `main`    | Release history; tag release commits and keep the branch buildable. | Policy: protected — no direct pushes, merge via PR only. |
| `develop` | Integration branch for the next release. All finished features land here first. | No direct pushes to shared history; prefer PRs. |

Supporting, short-lived branches are created off `develop` (or off `main` for
hotfixes) and deleted once merged:

| Branch pattern     | Base       | Merges into         | Use |
|---------------------|-----------|----------------------|-----|
| `feature/<slug>`    | `develop` | `develop`            | New functionality. |
| `fix/<slug>`        | `develop` | `develop`            | Bug fixes that aren't urgent. |
| `refactor/<slug>`   | `develop` | `develop`            | Internal restructuring, no behavior change. |
| `release/x.y.z`     | `develop` | `main` and `develop` | Stabilization/QA before a release (version bump, changelog, no new features). |
| `hotfix/x.y.z`      | `main`    | `main` and `develop` | Urgent fix against a released version. |

`<slug>` is short, kebab-case, and describes the change (e.g.
`feature/serial-telemetry-source`, `fix/plot-buffer-overflow`).

## Commit messages

Commits follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <short summary>

[optional body]

[optional footer]
```

Allowed types: `feat`, `fix`, `refactor`, `docs`, `test`, `build`, `ci`,
`chore`, `perf`. `<scope>` is the affected module (e.g. `serial`, `ui`,
`core`, `protocol`). Example:

```
feat(serial): add reconnect logic for QSerialPort source
```

## Pull requests

- PRs target `develop`, except `hotfix/*` and `release/*`, which target `main`.
- A PR must build (CMake configure + build) before merge; no broken commits
  on `develop` or `main`.
- Keep PRs scoped to one feature/fix. Split unrelated changes into separate PRs.
- Prefer squash-merge for `feature/*` and `fix/*` branches to keep history on
  `develop` linear and readable; use a regular merge commit for
  `release/*` → `main` so the release point is traceable.
- Delete the branch after merge.

## Versioning and releases

TraceView uses [Semantic Versioning](https://semver.org/) (`MAJOR.MINOR.PATCH`):

- `MAJOR` — breaking changes to the telemetry protocol contract or project file format.
- `MINOR` — backward-compatible features (new widgets, new telemetry sources).
- `PATCH` — bug fixes, no new functionality.

Release flow:

1. Cut `release/x.y.z` from `develop`. Bump `CMakeLists.txt` project version,
   turn `CHANGELOG.md`'s `[Unreleased]` section into `[x.y.z] - YYYY-MM-DD`
   and start a fresh empty `[Unreleased]` section, fix only
   release-blocking bugs on this branch.
2. Run `python scripts/check_style.py` and `python scripts/smoke_test.py`
   (see "Project scripts" below), plus `ctest` in the build directory (see
   "Tests" below), and fix whatever they flag.
3. Merge `release/x.y.z` into `main`, then tag the merged release commit:

   ```sh
   git switch main
   git pull --ff-only origin main
   git tag -a vx.y.z -m "TraceView x.y.z"
   git push origin vx.y.z
   ```

   Replace `x.y.z` with the version verified in `CMakeLists.txt`.
   [release.yml](.github/workflows/release.yml) publishes the Windows and
   Linux packages, `SHA256SUMS.txt` and generated release notes. It currently
   sets `--prerelease` unconditionally. The updater examines the first
   entry in the release list, including prereleases; this flag does not
   exclude a build from update notifications.

   After publication, the workflow deletes other GitHub releases and their
   assets in the same `MAJOR.MINOR` line, preserving Git tags. Account for
   this before publishing an older patch or rebuilding a release.
   The packaging workflow does not run CTest: complete the test gate before
   pushing the tag. Download and launch the packaged builds on clean target
   systems to check bundled runtime dependencies.
4. Merge `release/x.y.z` back into `develop` so the version bump and any
   last-minute fixes aren't lost.
5. Delete `release/x.y.z`.

Hotfixes follow the same pattern starting from `main` instead of `develop`.

Tag names must match `CMakeLists.txt`'s `project(... VERSION x.y.z)` exactly
(`vx.y.z`) -- `UpdateChecker` compares a release's tag against the running
build's own version, so a mismatched tag either hides a real release or
advertises one that isn't there. (`v2.16.0` in this repo's history is a
known-bad tag cut before this check existed and does not match any real
`CMakeLists.txt` version; leave it alone unless you're specifically cleaning
up tag history, since nothing depends on it today.)

## Code style

- C++17, formatted with the repository's `.clang-format` (Google-based,
  100-column limit) — run `clang-format -i` before committing.
- Qt naming conventions: `PascalCase` for classes, `camelCase` for methods and
  variables, `m_` prefix for private member variables, `k` prefix for
  constants (matches `include/traceview/version.h.in`).
- Keep protocol/transport code (anything talking to a serial port or parsing
  telemetry frames) isolated from UI code — see
  [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for why this boundary matters across
  the Bally ecosystem.
- No new abstractions or configuration options without a concrete use case;
  avoid speculative generality.

For repository-wide mechanical formatting, record the formatting commit in
`.git-blame-ignore-revs`. Keep semantic edits out of that commit.

## Project scripts

Two Python scripts under `scripts/` help keep `develop` releasable. Neither
runs as a Git hook — CMake builds and the Qt Test suite run in CI on Windows
and Linux instead (see `.github/workflows/build.yml`); run these by hand
before cutting a release, or any time you want a sanity check after a change.
Both scripts currently use only Python's standard library. Their shared
`_bootstrap.py` supports dependency installation, but the current empty
requirement lists do not create a virtual environment or install packages.

- `python scripts/check_style.py` — checks the C++ source against the
  "Code style" rules above: runs `clang-format` in check mode if it's on
  `PATH`, plus heuristic checks for `PascalCase` classes, `k`-prefixed
  constants, and `m_`-prefixed member variables. Best-effort (regex-based,
  not a real C++ parser) — it doesn't check function/parameter naming.
  Covers `src/`, `lib/`, `include/`, `tests/` and `tools/`, and excludes
  two things whose bytes belong to someone else: `lib/vendor`
  (reformatting vendored code turns the next upstream update into a merge
  conflict) and `include/bally_channels.h` (one of three byte-identical
  copies across the Bally repositories, hash-checked in each — see
  `tests/test_ballychannels.cpp`). Note that `clang-format` is not on
  `PATH` by default on a Qt install; it ships at
  `Tools/llvm-mingw*/bin/clang-format.exe`, and the check warns and skips
  the formatting half if it can't find it.
- `python scripts/smoke_test.py` — selects `windows-mingw` or `linux-ninja`
  for the host, builds the project and launches the resulting TraceView
  executable to confirm it doesn't crash on startup. Pass `--preset NAME` to
  use another shared or user preset. This is a build + launch smoke test, not
  a UI regression suite — it doesn't click anything inside the app. For now,
  verifying dashboard behavior (drag, resize, save/load) after a change is
  still the manual walkthrough in [docs/DASHBOARD.md](docs/DASHBOARD.md).

## Tests

Automated unit tests live under `tests/`, written with Qt Test (`Qt6::Test`)
— one `QObject`-derived test class per file, built as its own executable and
registered with CTest (see `tests/CMakeLists.txt`). They build as part of the
normal CMake configure/build (`TRACEVIEW_BUILD_TESTS`, default `ON`; pass
`-DTRACEVIEW_BUILD_TESTS=OFF` to skip if `Qt6::Test` isn't installed). Run all
of them with the matching preset (`ctest --preset windows-mingw`,
`ctest --preset windows-msvc` or `ctest --preset linux-ninja`), or run CTest
from a configured build directory. On a headless Linux host, set
`QT_QPA_PLATFORM=offscreen`. Like the app itself, Windows test binaries need
the matching Qt runtime on `PATH`; `CMakeUserPresets.json` can carry those
machine-specific environment overrides.

The authoritative suite list is [tests/CMakeLists.txt](tests/CMakeLists.txt).
Coverage includes BTP framing/session lifecycle, handshake, manifests,
subscriptions, hub binding/endpoints, device presence, transport error
paths, project persistence, dashboard widgets, telemetry buffers, OTA/mDNS
logic, updater logic and diagnostics. The shared `bally_channels.h`
contract has a dedicated hash check.

List or run a focused subset with the matching host preset:

```sh
ctest --preset windows-mingw -N
ctest --preset windows-mingw -R 'test_(hub|btp)' --output-on-failure
```

Tests use synthetic frames and controlled failure paths. They do not
establish real Serial/USB HID operation, ESP-NOW range, mDNS discovery or
firmware-flash compatibility. `BtpBackend` is exercised indirectly through
hub and connection tests; it has no dedicated suite.

### Validation before a pull request

1. Configure and build with the appropriate preset, then run its CTest suite.
2. For C++ edits, run `python scripts/check_style.py`. Ensure `clang-format`
   is available before reporting formatting as checked.
3. For startup, dependency or UI changes, run
   `python scripts/smoke_test.py --preset windows-mingw` (or the matching
   host preset). It configures/builds, launches the app, checks that it stays
   alive briefly and terminates it. It does not exercise UI flows.
4. For dashboard/project changes, open `example.tvproj`, edit properties,
   drag/resize widgets, undo/redo, save a separate copy and reopen it.
   Verify layout, bindings and device configuration persistence.
5. For connection changes, check the affected real transport. For hub
   changes, check child discovery, reconnect and independent robot restart.
   For OTA changes, follow [docs/OTA.md](docs/OTA.md). Device script changes
   need manual checks of callbacks, timers and outbound actions.
6. Describe the observable change, affected layers, checks run and remaining
   hardware/platform validation in the PR. Update `[Unreleased]` in
   `CHANGELOG.md` for user-visible behavior changes.

The full main window, OTA/update flows and native desktop integration still
need manual checks. For documentation-only changes, verify commands and
links against the repository; rebuilding unchanged C++ is unnecessary.

Visual tools are enabled by `TRACEVIEW_BUILD_TOOLS`. They are separate
executables, excluded from installed packages and CTest. For example:

```sh
cmake --build --preset windows-mingw --target chart_preview
```

On Windows, run
`build/windows-mingw/tools/chart_preview/chart_preview.exe`; on Linux,
`build/linux-ninja/tools/chart_preview`. See
[tools/CMakeLists.txt](tools/CMakeLists.txt) for available targets.

## Scope of "features"

Given TraceView's role in the Bally ecosystem (see
[docs/ECOSYSTEM.md](docs/ECOSYSTEM.md)), a "feature" is expected to state,
in the PR description, which layer it touches:

- **Transport** — how bytes arrive (Serial, USB HID and hub channels).
- **Protocol** — how bytes are decoded into telemetry records.
- **Presentation** — how decoded records are displayed (plots, log view,
  robot state view).

This isn't enforced by tooling, just a convention to keep changes reviewable
and to prevent transport/protocol assumptions from leaking into UI code.
