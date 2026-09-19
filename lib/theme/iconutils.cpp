#include "iconutils.h"

#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSvgRenderer>

namespace traceview {

namespace {

// Icons are rasterized once, at a fixed logical `size`, and then reused as a
// plain QPixmap wherever Qt needs them -- including on HiDPI screens, where
// Qt upscales that single bitmap to fill the physical button area. Without a
// higher-resolution backing bitmap that upscale is what reads as "blurry
// icons". Rendering at this multiple of `size` and tagging the result with a
// matching devicePixelRatio gives Qt enough native resolution to downscale
// (or lightly upscale, up to 4x display scaling) without visible softening.
constexpr qreal kSupersample = 4.0;

// Renders `svgResourcePath` into a `size`x`size` (device-independent pixels)
// transparent pixmap at kSupersample native resolution, then replaces every
// opaque pixel with `color` via SourceIn -- the SVG's own colors never reach
// the screen, only its alpha shape (anti-aliased edges included) does.
QPixmap tintedPixmap(const QString& svgResourcePath, const QColor& color, int size) {
    const int physicalSize = qRound(size * kSupersample);
    QPixmap pixmap(physicalSize, physicalSize);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(kSupersample);

    QSvgRenderer renderer(svgResourcePath);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(QRect(0, 0, size, size), color);
    painter.end();

    return pixmap;
}

}  // namespace

QIcon loadTintedIcon(const QString& svgResourcePath, const QColor& color, int size) {
    return QIcon(tintedPixmap(svgResourcePath, color, size));
}

void drawTintedIcon(QPainter& painter, const QRect& rect, const QString& svgResourcePath,
                     const QColor& color) {
    painter.drawPixmap(rect, tintedPixmap(svgResourcePath, color, rect.width()));
}

}  // namespace traceview
