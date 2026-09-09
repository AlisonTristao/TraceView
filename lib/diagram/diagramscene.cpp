#include "diagramscene.h"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPen>

#include <utility>

#include "diagramblockitem.h"
#include "diagramconnectionitem.h"
#include "diagramportitem.h"

namespace traceview {

DiagramScene::DiagramScene(QObject* parent) : QGraphicsScene(parent) {
    // Enough room for a couple dozen blocks in DiagramPage's own grid layout
    // (4 columns x ~220px) plus some margin to drag them around -- not the
    // vast, mostly-empty canvas a fixed 4000x4000 rect gave.
    setSceneRect(-100, -100, 1700, 1000);
}

DiagramBlockItem* DiagramScene::addBlock(const QString& deviceId, const QString& label,
                                         const QPointF& pos) {
    if (DiagramBlockItem* existing = m_blocks.value(deviceId)) {
        return existing;
    }
    auto* block = new DiagramBlockItem(deviceId, label);
    block->setPos(pos);
    addItem(block);
    m_blocks.insert(deviceId, block);
    connectBlockSignals(block);
    return block;
}

void DiagramScene::removeBlock(const QString& deviceId) {
    DiagramBlockItem* block = m_blocks.take(deviceId);
    if (block == nullptr) {
        return;
    }
    for (int i = m_connections.size() - 1; i >= 0; --i) {
        DiagramConnectionItem* connection = m_connections[i];
        if (connection->fromPort()->parentItem() == block ||
            connection->toPort()->parentItem() == block) {
            removeItem(connection);
            delete connection;
            m_connections.removeAt(i);
        }
    }
    removeItem(block);
    delete block;
}

DiagramBlockItem* DiagramScene::blockFor(const QString& deviceId) const {
    return m_blocks.value(deviceId);
}

QVector<DiagramBlockItem*> DiagramScene::blocks() const {
    return QVector<DiagramBlockItem*>(m_blocks.cbegin(), m_blocks.cend());
}

DiagramConnectionItem* DiagramScene::addConnection(const QString& fromDeviceId, DiagramPortKind kind,
                                                   const QString& toDeviceId) {
    DiagramBlockItem* fromBlock = m_blocks.value(fromDeviceId);
    DiagramBlockItem* toBlock = m_blocks.value(toDeviceId);
    if (fromBlock == nullptr || toBlock == nullptr || fromBlock == toBlock) {
        return nullptr;
    }
    return connectPorts(fromBlock->port(kind, DiagramPortDirection::Output),
                        toBlock->port(kind, DiagramPortDirection::Input));
}

DiagramConnectionItem* DiagramScene::connectPorts(DiagramPortItem* from, DiagramPortItem* to) {
    if (from == nullptr || to == nullptr) {
        return nullptr;
    }
    for (const DiagramConnectionItem* existing : std::as_const(m_connections)) {
        if (existing->fromPort() == from && existing->toPort() == to) {
            return nullptr;
        }
    }
    auto* connection = new DiagramConnectionItem(from, to);
    addItem(connection);
    m_connections.append(connection);
    return connection;
}

void DiagramScene::connectBlockSignals(DiagramBlockItem* block) {
    connect(block, &DiagramBlockItem::activated, this, &DiagramScene::blockActivated);
    connect(block, &DiagramBlockItem::moved, this, [this, block] { updateConnectionsFor(block); });
}

void DiagramScene::updateConnectionsFor(DiagramBlockItem* block) {
    for (DiagramConnectionItem* connection : std::as_const(m_connections)) {
        if (connection->fromPort()->parentItem() == block ||
            connection->toPort()->parentItem() == block) {
            connection->updatePath();
        }
    }
}

void DiagramScene::cancelPendingConnection() {
    if (m_dragPreview != nullptr) {
        removeItem(m_dragPreview);
        delete m_dragPreview;
        m_dragPreview = nullptr;
    }
    m_dragFromPort = nullptr;
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (auto* port = dynamic_cast<DiagramPortItem*>(itemAt(event->scenePos(), QTransform()))) {
            if (port->direction() == DiagramPortDirection::Output) {
                m_dragFromPort = port;
                m_dragPreview = new QGraphicsPathItem();
                m_dragPreview->setPen(QPen(diagramPortKindColor(port->kind()), 2, Qt::DashLine));
                m_dragPreview->setZValue(10);
                addItem(m_dragPreview);
                QPainterPath path(port->connectionPoint());
                path.lineTo(event->scenePos());
                m_dragPreview->setPath(path);
                event->accept();
                return;
            }
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragFromPort != nullptr) {
        QPainterPath path(m_dragFromPort->connectionPoint());
        path.lineTo(event->scenePos());
        m_dragPreview->setPath(path);
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (m_dragFromPort != nullptr) {
        DiagramPortItem* from = m_dragFromPort;
        if (auto* target = dynamic_cast<DiagramPortItem*>(itemAt(event->scenePos(), QTransform()))) {
            const bool validTarget = target->direction() == DiagramPortDirection::Input &&
                                     target->kind() == from->kind() &&
                                     target->parentItem() != from->parentItem();
            if (validTarget) {
                connectPorts(from, target);
            }
        }
        cancelPendingConnection();
        event->accept();
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

}  // namespace traceview
