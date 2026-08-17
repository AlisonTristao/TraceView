# Theming

TraceView's visual identity is a set of color tokens (`ThemePalette`,
[include/traceview/theme.h](../include/traceview/theme.h)) rather than colors
hardcoded into widgets. A "template" the user picks from the menu is just one
`ThemePalette` value; the QSS in
[lib/theme/stylesheet.cpp](../lib/theme/stylesheet.cpp) reads only palette
tokens, never literal colors.

## Adding a new template

1. Add a `makeXPalette()` function to
   [lib/theme/palettes.h](../lib/theme/palettes.h)/`.cpp` that fills every
   field of `ThemePalette` (use the existing `dark`/`light` palettes as a
   reference for role meaning).
2. Register it in `ThemeManager`'s constructor
   ([lib/theme/thememanager.cpp](../lib/theme/thememanager.cpp)):
   `registerTheme(makeXPalette());`

That's it — the theme picker in `MainWindow::buildMenus()` enumerates
`ThemeManager::availableThemes()`, so a new template shows up automatically
with no UI code changes. No QSS or widget code needs to change either,
unless the new template needs a rule the current stylesheet doesn't cover
yet (in which case, add that rule as a token-driven line in
`buildStyleSheet()`, not a hardcoded color).

## Token reference

| Token           | Role                                                   |
|-----------------|---------------------------------------------------------|
| `background`    | Window background                                       |
| `surface`       | Panels, menus, buttons                                  |
| `surfaceAlt`    | Hover/alternate state for surfaces                       |
| `border`        | Subtle dividers (semi-transparent)                       |
| `borderStrong`  | Emphasis borders (fully opaque, e.g. button outlines)     |
| `textPrimary` / `textSecondary` / `textDisabled` | Text hierarchy      |
| `accent` / `accentHover` / `accentPressed`       | Interactive elements |
| `success` / `warning` / `danger`                 | Status colors        |
| `series`        | Reserved for future telemetry plot lines (not wired to QSS) |

## Current templates

- **Dark** (default) — near-black navy background (`#0A0F1E`), white text and
  borders, blue accent (`#3D8BFF`).
- **Light** — white background, dark navy text/borders, matching blue accent.
- **Wood** — dark walnut background (`#2B1D14`), cream text, amber/copper
  accent (`#C97A3D`).
- **Black** — pure-black background (`#000000`), white text, neutral
  grayscale accent (no blue undertone, unlike Dark).
- **Matrix** — black background, phosphor-green text and accent (`#00FF41`);
  warning/danger break from green so alerts still stand out.
- **Synthwave** — deep purple background, neon pink accent (`#FF2E9A`) with
  cyan as the series companion color.
- **Amber** — black background, monochrome amber phosphor (`#FFB000`), the
  other classic CRT color alongside Matrix's green.
- **Arctic** — cool blue-gray background (Nord-inspired), frost-cyan accent
  (`#88C0D0`).
- **Sakura** — soft pink background, deep maroon text, pink accent
  (`#E85D8A`).

Switch templates from the running app via **View → Theme**; the choice is
persisted (`QSettings`, key `appearance/theme`) across restarts.

## Fonts

Font family is a separate, orthogonal choice from the color theme above --
`FontManager` ([include/traceview/fontmanager.h](../include/traceview/fontmanager.h),
[lib/theme/fontmanager.cpp](../lib/theme/fontmanager.cpp)) applies a `QFont`
to `QApplication` directly instead of going through `ThemePalette`/the QSS, so
any font pairs with any theme rather than being bundled per-template. Only
the family changes; size/weight stay whatever Qt picked as the platform
default (same "default typography everywhere" decision as the rest of the
app -- see `TODO_VISUAL_IDENTITY.txt` TAREFA 0).

Current options: **System Default** (leaves the platform's own font alone),
**Consolas**, **Georgia**, **Verdana**. Adding another one is
`registerFont(...)` in `FontManager`'s constructor -- no other code changes,
same shape as adding a theme above. If the named family isn't installed, Qt
silently substitutes its closest match rather than failing.

Switch fonts from the running app via **View → Font**; the choice is
persisted (`QSettings`, key `appearance/font`) across restarts.

## Icon

`scripts/gen_icon.py` draws the mark and is the source of truth; run
`python scripts/gen_icon.py` after editing its color constants or geometry to
regenerate the PNG set and `.ico` under `resources/icons/`, which is what's
actually embedded via `resources/icons/app.qrc` (runtime `QIcon`) and
`resources/app.rc` (Windows executable icon). `resources/icons/app.svg` is a
hand-kept scalable mirror of the same design, bundled in the `.qrc` for
reference but not loaded by `QIcon` itself — update it to match whenever the
script's design changes.
