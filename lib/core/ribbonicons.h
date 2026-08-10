#pragma once

#include <QColor>
#include <QIcon>

namespace traceview {

// Pixel size these icons are drawn at; ribbon buttons should size themselves
// to match (see Ribbon::createButtonGroup).
inline constexpr int kRibbonIconSize = 16;

// Flat, hand-drawn (not font-glyph) icons so they render crisply and
// consistently at small toolbar sizes, colored from the active theme.
QIcon makeSelectIcon(const QColor& color);
QIcon makePlusIcon(const QColor& color);
QIcon makeMinusIcon(const QColor& color);
QIcon makeArrowIcon(const QColor& color, bool pointingLeft);
QIcon makeCopyIcon(const QColor& color);
QIcon makePasteIcon(const QColor& color);
// active=false draws the classic "enter fullscreen" glyph (corner elbows
// right at the icon's corners, arms folding inward); active=true draws its
// mirror, the "exit fullscreen" glyph (elbows pulled to the center, arms
// reaching back out toward the corners).
QIcon makeFullscreenIcon(const QColor& color, bool active);

} // namespace traceview
