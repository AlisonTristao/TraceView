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

} // namespace traceview
