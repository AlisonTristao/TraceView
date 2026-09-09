#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QString>
#include <QVector>

#include "diagramport.h"

class QGraphicsPathItem;
class QGraphicsSceneMouseEvent;

namespace traceview {

class DiagramBlockItem;
class DiagramConnectionItem;
class DiagramPortItem;

// Owns the block/connection items and the one piece of interaction that
// can't live on a single item: dragging a wire from an Output port to a
// matching Input port on another block. Everything else (moving a block,
// selecting it, double-clicking it) is handled by DiagramBlockItem itself
// and just flows through QGraphicsScene's default event dispatch.
class DiagramScene : public QGraphicsScene {
    Q_OBJECT

public:
    explicit DiagramScene(QObject* parent = nullptr);

    // Creates (or, if `deviceId` already has a block, returns the existing
    // one unmoved) a block at `pos`.
    DiagramBlockItem* addBlock(const QString& deviceId, const QString& label,
                              const QPointF& pos);
    // Removes the block for `deviceId`, if any, along with every connection
    // attached to one of its ports.
    void removeBlock(const QString& deviceId);
    DiagramBlockItem* blockFor(const QString& deviceId) const;
    QVector<DiagramBlockItem*> blocks() const;

    // Joins `fromDeviceId`'s Output port of `kind` to `toDeviceId`'s Input
    // port of the same kind -- the same connection a completed port-to-port
    // drag creates, exposed as an API so DiagramPage::fromJson() can restore
    // saved connections without simulating mouse events. Returns nullptr (no
    // connection made) if either device has no block, they're the same
    // block, or that exact connection already exists.
    DiagramConnectionItem* addConnection(const QString& fromDeviceId, DiagramPortKind kind,
                                         const QString& toDeviceId);
    QVector<DiagramConnectionItem*> connections() const {
        return m_connections;
    }

signals:
    // A block was double-clicked -- DiagramPage owns opening the config
    // dialog for it.
    void blockActivated(const QString& deviceId);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // Shared by addConnection() and the mouse-drag release handler: creates
    // the connection unless one between this exact pair already exists.
    // Returns nullptr in that case, or if either port is null.
    DiagramConnectionItem* connectPorts(DiagramPortItem* from, DiagramPortItem* to);
    void connectBlockSignals(DiagramBlockItem* block);
    // Refreshes every connection with an endpoint on `block` -- wired to
    // that block's own moved() signal.
    void updateConnectionsFor(DiagramBlockItem* block);
    void cancelPendingConnection();

    QHash<QString, DiagramBlockItem*> m_blocks;
    QVector<DiagramConnectionItem*> m_connections;

    // Non-null only between a press on an Output port and the matching
    // release -- the wire currently being dragged out.
    DiagramPortItem* m_dragFromPort = nullptr;
    QGraphicsPathItem* m_dragPreview = nullptr;
};

}  // namespace traceview
