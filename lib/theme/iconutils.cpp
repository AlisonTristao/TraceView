#include "iconutils.h"

#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSvgRenderer>

namespace traceview {

namespace {

// Renders `svgResourcePath` into a `size`x`size` transparent pixmap, then
// replaces every opaque pixel with `color` via SourceIn -- the SVG's own
// colors never reach the screen, only its alpha shape (anti-aliased edges
// included) does.
QPixmap tintedPixmap(const QString& svgResourcePath, const QColor& color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QSvgRenderer renderer(svgResourcePath);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, QRectF(0, 0, size, size));
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(pixmap.rect(), color);
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
