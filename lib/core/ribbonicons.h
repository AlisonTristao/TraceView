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

} // namespace traceview
