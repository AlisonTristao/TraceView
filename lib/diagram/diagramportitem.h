#pragma once

#include <QGraphicsEllipseItem>

#include "diagramport.h"

namespace traceview {

// One connection point on a DiagramBlockItem -- a small filled circle, color
// coded by DiagramPortKind (see diagramscene.cpp's colorFor()), positioned by
// its parent block (left edge for Input, right edge for Output; see
// DiagramBlockItem::layoutPorts()). DiagramScene does the actual drag-to-
// connect hit testing directly against these items (itemAt() in its mouse
// handlers) rather than this class listening for its own mouse events, so a
// drag started slightly off-circle (over the block) still finds the nearest
// port via DiagramBlockItem::portAt().
class DiagramPortItem : public QGraphicsEllipseItem {
public:
    DiagramPortItem(DiagramPortKind kind, DiagramPortDirection direction,
                     QGraphicsItem* parent);

    DiagramPortKind kind() const { return m_kind; }
    DiagramPortDirection direction() const { return m_direction; }

    // Scene-space position of this port's connection point (its center),
    // recomputed on demand rather than cached -- cheap for a handful of
    // ports and always correct after the parent block moves.
    QPointF connectionPoint() const;

private:
    DiagramPortKind m_kind;
    DiagramPortDirection m_direction;
};

}  // namespace traceview
