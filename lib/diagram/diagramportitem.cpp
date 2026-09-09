#include "diagramportitem.h"

#include <QBrush>
#include <QPen>

namespace traceview {

namespace {
constexpr qreal kPortRadius = 5.0;
}  // namespace

DiagramPortItem::DiagramPortItem(DiagramPortKind kind, DiagramPortDirection direction,
                                 QGraphicsItem* parent)
    : QGraphicsEllipseItem(-kPortRadius, -kPortRadius, 2 * kPortRadius, 2 * kPortRadius, parent),
      m_kind(kind),
      m_direction(direction) {
    setBrush(QBrush(diagramPortKindColor(kind)));
    setPen(QPen(QColor(0, 0, 0, 90), 1));
    setAcceptHoverEvents(true);
    setZValue(1);
}

QPointF DiagramPortItem::connectionPoint() const {
    return mapToScene(rect().center());
}

}  // namespace traceview
