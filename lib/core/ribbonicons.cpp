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

QIcon makePencilIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));

    // Same margin convention as the other icons (2px in from each edge), so
    // the diagonal stroke has the same visual weight/centering as its peers.
    const int margin = 2;
    painter.drawLine(QPoint(margin, kRibbonIconSize - margin), QPoint(kRibbonIconSize - margin - 2, margin + 2));

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    QPolygon tip;
    tip << QPoint(kRibbonIconSize - margin - 3, margin) << QPoint(kRibbonIconSize - margin, margin)
        << QPoint(kRibbonIconSize - margin, margin + 3);
    painter.drawPolygon(tip);
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

} // namespace traceview
