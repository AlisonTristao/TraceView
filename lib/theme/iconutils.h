#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QPainter;
class QRect;

namespace traceview {

// Loads an SVG resource (see resources/icons/ribbon, .../settings,
// .../dashboard) and tints every opaque pixel `color`, the same "flat,
// theme-colored glyph" look the hand-drawn QPainter icons used to produce --
// only the SVG's alpha shape matters, its own fill/stroke colors are
// discarded. `size` is the square pixmap side in device-independent pixels.
QIcon loadTintedIcon(const QString& svgResourcePath, const QColor& color, int size);

// Same tinting as loadTintedIcon(), but painted directly into an existing
// QPainter at `rect` instead of returned as a QIcon -- for glyphs drawn
// live inside a paintEvent (e.g. DashboardCell's header controls) rather
// than handed to a QAbstractButton/QAction as an icon.
void drawTintedIcon(QPainter& painter, const QRect& rect, const QString& svgResourcePath,
                     const QColor& color);

}  // namespace traceview
