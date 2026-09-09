#include "diagramconnectionitem.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>
#include <QtMath>

#include "diagramportitem.h"

namespace traceview {

namespace {
constexpr qreal kArrowSize = 8.0;
}  // namespace

DiagramConnectionItem::DiagramConnectionItem(DiagramPortItem* from, DiagramPortItem* to)
    : m_from(from), m_to(to) {
    setPen(QPen(diagramPortKindColor(from->kind()), 2));
    setZValue(0);
    updatePath();
}

void DiagramConnectionItem::updatePath() {
    const QPointF start = m_from->connectionPoint();
    const QPointF end = m_to->connectionPoint();

    // Horizontal S-curve: control points pulled out from each endpoint by a
    // third of the horizontal span (clamped so a short/backwards run doesn't
    // produce a loop), same shape convention as a typical node-editor wire.
    const qreal dx = qMax(qAbs(end.x() - start.x()) * 0.5, 40.0);
    const QPointF c1(start.x() + dx, start.y());
    const QPointF c2(end.x() - dx, end.y());

    QPainterPath path(start);
    path.cubicTo(c1, c2, end);
    setPath(path);
}

void DiagramConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(pen());
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    // Arrowhead at the destination end, oriented along the curve's tangent
    // there -- taken from two nearby sample points rather than
    // angleAtPercent() (whose degrees-from-x-axis convention is easy to get
    // backwards against Qt's downward y-axis).
    const QPointF tip = path().pointAtPercent(1.0);
    const QPointF tail = path().pointAtPercent(0.95);
    QPointF dir = tip - tail;
    const qreal len = qSqrt(dir.x() * dir.x() + dir.y() * dir.y());
    if (len > 0.001) {
        dir /= len;
    }
    const QPointF normal(-dir.y(), dir.x());
    const QPointF back = tip - dir * kArrowSize;
    const QPolygonF head({tip, back + normal * (kArrowSize * 0.5),
                          back - normal * (kArrowSize * 0.5)});
    painter->setBrush(pen().color());
    painter->setPen(Qt::NoPen);
    painter->drawPolygon(head);
}

}  // namespace traceview
