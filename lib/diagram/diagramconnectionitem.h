#pragma once

#include <QGraphicsPathItem>
#include <QStyleOptionGraphicsItem>

namespace traceview {

class DiagramPortItem;

// One arrow between an Output port on one DiagramBlockItem and an Input port
// on another, both of the same DiagramPortKind (enforced by whoever creates
// this -- see DiagramScene's drag-release handler, the only call site).
// Purely visual for this first pass: it records which two ports it joins so
// DiagramScene can keep its path current and tear it down when either block
// is removed, but carries no data of its own.
class DiagramConnectionItem : public QGraphicsPathItem {
public:
    DiagramConnectionItem(DiagramPortItem* from, DiagramPortItem* to);

    DiagramPortItem* fromPort() const { return m_from; }
    DiagramPortItem* toPort() const { return m_to; }

    // Recomputes the path from the two ports' current scene positions --
    // called by DiagramScene whenever either endpoint's block moves.
    void updatePath();

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

private:
    DiagramPortItem* m_from;
    DiagramPortItem* m_to;
};

}  // namespace traceview
