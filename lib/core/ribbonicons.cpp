#include "ribbonicons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygon>

namespace traceview {

QIcon makeSelectIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const int m = 2;
    const int len = 4;
    const int n = kRibbonIconSize;
    painter.drawLine(m, m + len, m, m);
    painter.drawLine(m, m, m + len, m);
    painter.drawLine(n - m - len, m, n - m, m);
    painter.drawLine(n - m, m, n - m, m + len);
    painter.drawLine(m, n - m - len, m, n - m);
    painter.drawLine(m, n - m, m + len, n - m);
    painter.drawLine(n - m - len, n - m, n - m, n - m);
    painter.drawLine(n - m, n - m, n - m, n - m - len);
    return QIcon(pixmap);
}

QIcon makePlusIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const int margin = kRibbonIconSize / 4;
    const int mid = kRibbonIconSize / 2;
    painter.drawLine(mid, margin, mid, kRibbonIconSize - margin);
    painter.drawLine(margin, mid, kRibbonIconSize - margin, mid);
    return QIcon(pixmap);
}

QIcon makeMinusIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const int margin = kRibbonIconSize / 4;
    const int mid = kRibbonIconSize / 2;
    painter.drawLine(margin, mid, kRibbonIconSize - margin, mid);
    return QIcon(pixmap);
}

QIcon makeArrowIcon(const QColor& color, bool pointingLeft) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const int margin = 3;
    QPolygon arrow;
    if (pointingLeft) {
        arrow << QPoint(kRibbonIconSize - margin, margin) << QPoint(margin, kRibbonIconSize / 2)
              << QPoint(kRibbonIconSize - margin, kRibbonIconSize - margin);
    } else {
        arrow << QPoint(margin, margin) << QPoint(kRibbonIconSize - margin, kRibbonIconSize / 2)
              << QPoint(margin, kRibbonIconSize - margin);
    }
    painter.drawPolyline(arrow);
    return QIcon(pixmap);
}

QIcon makeCopyIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Two overlapping outlined squares, offset diagonally — the standard
    // "copy" glyph, kept as plain strokes (no punched-out overlap) to match
    // this file's flat hand-drawn style.
    painter.drawRoundedRect(QRectF(2.5, 2.5, 8, 8), 1.5, 1.5);
    painter.drawRoundedRect(QRectF(5.5, 5.5, 8, 8), 1.5, 1.5);
    return QIcon(pixmap);
}

QIcon makePasteIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Clipboard glyph: body plus a small centered clip tab on top.
    painter.drawRoundedRect(QRectF(3, 3.5, 10, 11), 1.5, 1.5);
    painter.drawRoundedRect(QRectF(6, 2, 4, 3), 1, 1);
    return QIcon(pixmap);
}

QIcon makeFullscreenIcon(const QColor& color, bool active) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const int n = kRibbonIconSize;
    const int outer = 1;
    const int len = 4;
    const int inner = outer + len;
    // Inactive: elbow sits right at each corner, arms folding inward toward
    // the middle. Active: elbow pulled to `inner`, arms reaching back out
    // toward `outer` — the same four brackets, mirrored.
    const int elbow = active ? inner : outer;
    const int tip = active ? outer : inner;

    auto corner = [&](int cx, int cy, int hTipX, int vTipY) {
        painter.drawLine(cx, cy, cx, vTipY);
        painter.drawLine(cx, cy, hTipX, cy);
    };

    corner(elbow, elbow, tip, tip);
    corner(n - elbow, elbow, n - tip, tip);
    corner(elbow, n - elbow, tip, n - tip);
    corner(n - elbow, n - elbow, n - tip, n - tip);

    return QIcon(pixmap);
}

namespace {

// The on-canvas stack: back square top-left, front square bottom-right
// (same offset-diagonal layout as makeCopyIcon's two squares), with
// whichever one the action targets filled solid instead of just outlined.
void drawLayerSquares(QPainter& painter, const QColor& color, bool fillBack, bool fillFront) {
    QPen pen(color, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    painter.setBrush(fillBack ? QBrush(color) : Qt::NoBrush);
    painter.drawRoundedRect(QRectF(2, 6, 6, 6), 1.2, 1.2);
    painter.setBrush(fillFront ? QBrush(color) : Qt::NoBrush);
    painter.drawRoundedRect(QRectF(6.5, 9.5, 6, 6), 1.2, 1.2);
}

// One (or two, stacked) small chevrons centered above the squares, pointing
// up (bring forward/to front) or down (send backward/to back). `doubled`
// marks a "jump to the extreme" action; a single chevron marks a one-step
// reorder.
void drawReorderChevron(QPainter& painter, const QColor& color, bool pointingUp, bool doubled) {
    QPen pen(color, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const double cx = 8.0;
    const double armHeight = pointingUp ? -1.4 : 1.4;
    auto oneChevron = [&](double y) {
        QPolygonF chevron;
        chevron << QPointF(cx - 3.0, y - armHeight) << QPointF(cx, y + armHeight)
                << QPointF(cx + 3.0, y - armHeight);
        painter.drawPolyline(chevron);
    };

    oneChevron(pointingUp ? 3.6 : 2.0);
    if (doubled) {
        oneChevron(pointingUp ? 1.2 : 4.4);
    }
}

QIcon makeReorderIcon(const QColor& color, bool pointingUp, bool doubled) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    drawLayerSquares(painter, color, /*fillBack=*/!pointingUp, /*fillFront=*/pointingUp);
    drawReorderChevron(painter, color, pointingUp, doubled);
    return QIcon(pixmap);
}

} // namespace

QIcon makeBringToFrontIcon(const QColor& color) {
    return makeReorderIcon(color, /*pointingUp=*/true, /*doubled=*/true);
}

QIcon makeBringForwardIcon(const QColor& color) {
    return makeReorderIcon(color, /*pointingUp=*/true, /*doubled=*/false);
}

QIcon makeSendBackwardIcon(const QColor& color) {
    return makeReorderIcon(color, /*pointingUp=*/false, /*doubled=*/false);
}

QIcon makeSendToBackIcon(const QColor& color) {
    return makeReorderIcon(color, /*pointingUp=*/false, /*doubled=*/true);
}

QIcon makePinIcon(const QColor& color, bool active) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(active ? QBrush(color) : Qt::NoBrush);

    // A pushpin reduced to its two readable strokes: a round head (the
    // handle) and a straight needle below it -- filled head when pinned,
    // hollow when not.
    const double cx = kRibbonIconSize / 2.0;
    const double headCy = 4.6;
    const double r = 2.6;
    painter.drawEllipse(QPointF(cx, headCy), r, r);
    painter.drawLine(QPointF(cx, headCy + r), QPointF(cx, kRibbonIconSize - 2.0));

    return QIcon(pixmap);
}

QIcon makeWorkspaceIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.5);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Four small squares in a 2x2 grid, evoking a dashboard layout.
    const double cell = 6.0;
    const double gap = 2.0;
    const double origin = (kRibbonIconSize - (2 * cell + gap)) / 2.0;
    painter.drawRoundedRect(QRectF(origin, origin, cell, cell), 1.0, 1.0);
    painter.drawRoundedRect(QRectF(origin + cell + gap, origin, cell, cell), 1.0, 1.0);
    painter.drawRoundedRect(QRectF(origin, origin + cell + gap, cell, cell), 1.0, 1.0);
    painter.drawRoundedRect(QRectF(origin + cell + gap, origin + cell + gap, cell, cell), 1.0, 1.0);
    return QIcon(pixmap);
}

QIcon makeTrashIcon(const QColor& color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    // Every coordinate below is drawn in the original 16px design space;
    // scaling the painter (instead of the finished pixmap) keeps the strokes
    // crisp at a larger `size` rather than blurring an upscaled bitmap.
    const qreal factor = qreal(size) / kRibbonIconSize;
    painter.scale(factor, factor);
    QPen pen(color, 1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // Lid (a horizontal bar with a small handle) plus a tapered body below
    // it with two vertical ribs -- the standard "delete" glyph.
    painter.drawLine(QPointF(3.0, 4.5), QPointF(13.0, 4.5));
    painter.drawLine(QPointF(6.5, 4.5), QPointF(6.5, 2.8));
    painter.drawLine(QPointF(9.5, 4.5), QPointF(9.5, 2.8));
    painter.drawLine(QPointF(6.5, 2.8), QPointF(9.5, 2.8));

    QPainterPath body;
    body.moveTo(4.0, 4.5);
    body.lineTo(4.8, 13.0);
    body.lineTo(11.2, 13.0);
    body.lineTo(12.0, 4.5);
    painter.drawPath(body);

    painter.drawLine(QPointF(6.6, 6.5), QPointF(6.9, 11.5));
    painter.drawLine(QPointF(9.4, 6.5), QPointF(9.1, 11.5));

    return QIcon(pixmap);
}

} // namespace traceview
