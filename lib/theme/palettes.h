#pragma once

#include "traceview/theme.h"

namespace traceview {

// Built-in templates. Adding a new one is: write a `makeXPalette()` here
// (or in a new file) and register it in ThemeManager's constructor — see
// docs/THEMING.md.
ThemePalette makeDarkPalette();
ThemePalette makeLightPalette();
ThemePalette makeWoodPalette();
ThemePalette makeBlackPalette();
ThemePalette makeMatrixPalette();
ThemePalette makeSynthwavePalette();
ThemePalette makeAmberPalette();
ThemePalette makeArcticPalette();
ThemePalette makeSakuraPalette();

}  // namespace traceview
