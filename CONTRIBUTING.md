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
   update the changelog, fix only release-blocking bugs on this branch.
2. Merge `release/x.y.z` into `main`, tag `vx.y.z` on `main`.
3. Merge `release/x.y.z` back into `develop` so the version bump and any
   last-minute fixes aren't lost.
4. Delete `release/x.y.z`.

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
