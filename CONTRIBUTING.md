# Contributing

This document defines the branching model, commit conventions, and release
process for TraceView. It applies to all contributors, including the
maintainer, to keep `main` always in a releasable state.

## Branch model

TraceView follows a Git Flow variant with two permanent branches:

| Branch    | Purpose                                                                 | Protection |
|-----------|--------------------------------------------------------------------------|------------|
| `main`    | Latest **stable** release. Every commit on `main` is tagged and buildable. | Protected — no direct pushes, merge via PR only. |
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
3. Merge `release/x.y.z` into `main`, tag `vx.y.z` on `main`.
4. Merge `release/x.y.z` back into `develop` so the version bump and any
   last-minute fixes aren't lost.
5. Delete `release/x.y.z`.

Hotfixes follow the same pattern starting from `main` instead of `develop`.

## Code style

- C++17, formatted with the repository's `.clang-format` (Google-based,
  100-column limit) — run `clang-format -i` before committing.
- Qt naming conventions: `PascalCase` for classes, `camelCase` for methods and
  variables, `m_` prefix for private member variables, `k` prefix for
  constants (matches `include/traceview/version.h`).
- Keep protocol/transport code (anything talking to a serial port or parsing
  telemetry frames) isolated from UI code — see
  [docs/ECOSYSTEM.md](docs/ECOSYSTEM.md) for why this boundary matters across
  the Bally ecosystem.
- No new abstractions or configuration options without a concrete use case;
  avoid speculative generality.

## Project scripts

Two Python scripts under `scripts/` help keep `develop` releasable. Neither
runs automatically (no git hook, no CI) — run them by hand before cutting a
release, or any time you want a sanity check after a change. Each
bootstraps its own virtual environment and installs whatever it needs on
first run (see `scripts/_bootstrap.py`), so a plain `python` on `PATH` is
enough to get started:

- `python scripts/check_style.py` — checks the C++ source against the
  "Code style" rules above: runs `clang-format` in check mode if it's on
  `PATH`, plus heuristic checks for `PascalCase` classes, `k`-prefixed
  constants, and `m_`-prefixed member variables. Best-effort (regex-based,
  not a real C++ parser) — it doesn't check function/parameter naming.
- `python scripts/smoke_test.py` — builds the project and launches
  `TraceView.exe` to confirm it doesn't crash on startup. This is a build
  + launch smoke test, not a UI regression suite — it doesn't click
  anything inside the app. For now, verifying dashboard behavior (drag,
  resize, save/load) after a change is still the manual walkthrough in
  [docs/DASHBOARD.md](docs/DASHBOARD.md).

## Tests

Automated unit tests live under `tests/`, written with Qt Test (`Qt6::Test`)
— one `QObject`-derived test class per file, built as its own executable and
registered with CTest (see `tests/CMakeLists.txt`). They build as part of the
normal CMake configure/build (`TRACEVIEW_BUILD_TESTS`, default `ON`; pass
`-DTRACEVIEW_BUILD_TESTS=OFF` to skip if `Qt6::Test` isn't installed). Run
them all with `ctest --output-on-failure` from the build directory, or a
single binary directly (e.g. `./tests/test_projectstore`) for one test
suite's output. Like the app itself, the test binaries need Qt's DLLs on
`PATH` to run — the same `environment` block in `CMakeUserPresets.json` used
to launch the built app works here too.

Current coverage, one file per area:

- `test_projectstore` — `.tvproj` save/load round-trip, on-disk format,
  failure paths (no path set, invalid JSON).
- `test_dashboarditem` — `DashboardItem` JSON (de)serialization, including
  field clamping and missing/optional fields.
- `test_widgetregistry` — built-in widget types are registered and
  constructible; unknown-type and duplicate-registration handling.
- `test_dashboardgrid` — `DashboardGrid`/`DashboardCommands`: add/remove/
  rename/re-key/reconfigure/retype are all undoable, dashboard JSON
  round-trips, and mouse-driven drag/resize (including grid-snapping and
  minimum-size clamping) via `QTest::mousePress`/`mouseMove`/`mouseRelease`.

Not covered yet: nothing UI-chrome-level (`MainWindow`, `Ribbon`,
`PropertiesPanel`) — `smoke_test.py` above is still the only check that the
app boots, and manual verification per `docs/DASHBOARD.md` is still how
dashboard editing gets exercised end-to-end.

## Scope of "features"

Given TraceView's role in the Bally ecosystem (see
[docs/ECOSYSTEM.md](docs/ECOSYSTEM.md)), a "feature" is expected to state,
in the PR description, which layer it touches:

- **Transport** — how bytes arrive (Serial, future transports).
- **Protocol** — how bytes are decoded into telemetry records.
- **Presentation** — how decoded records are displayed (plots, log view,
  robot state view).

This isn't enforced by tooling, just a convention to keep changes reviewable
and to prevent transport/protocol assumptions from leaking into UI code.
