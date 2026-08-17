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
// Two overlapping squares (the on-canvas stack) plus a chevron indicating
// the reorder direction: a single chevron for "one step" (forward/backward),
// a doubled chevron for "all the way" (to front/to back) -- the front (or
// back) square is filled solid to mark which one the action targets.
QIcon makeBringToFrontIcon(const QColor& color);
QIcon makeBringForwardIcon(const QColor& color);
QIcon makeSendBackwardIcon(const QColor& color);
QIcon makeSendToBackIcon(const QColor& color);
// Thumbtack, tilted 45°: filled when active=true (pinned open), outlined
// otherwise -- used by the panel pin toggle (see PropertiesPanel/LayersPanel).
QIcon makePinIcon(const QColor& color, bool active);
// A 2x2 grid of small squares -- the workspace switcher's button icon,
// evoking a dashboard layout.
QIcon makeWorkspaceIcon(const QColor& color);
// A trash bin: lid plus a tapered body -- the workspace switcher's per-row
// delete affordance. `size` defaults to kRibbonIconSize but can be drawn
// larger (the workspace switcher's rows want it a bit bigger than the
// ribbon's own buttons) -- the geometry scales with it rather than just
// being stretched, so it stays crisp instead of blurring.
QIcon makeTrashIcon(const QColor& color, int size = kRibbonIconSize);

} // namespace traceview
