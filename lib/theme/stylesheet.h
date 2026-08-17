#pragma once

#include <QString>

#include "traceview/theme.h"

namespace traceview {

// Turns a ThemePalette into a Qt stylesheet applied app-wide. This is the
// only place that needs to change if a new widget class needs theme-aware
// rules — palettes themselves stay plain data.
QString buildStyleSheet(const ThemePalette& palette);

} // namespace traceview
