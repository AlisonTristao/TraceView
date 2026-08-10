#include "ribbonicons.h"

#include <QPainter>
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

} // namespace traceview
