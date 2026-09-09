#pragma once

#include <QGraphicsObject>
#include <QStyleOptionGraphicsItem>
#include <QString>

#include "devices/device.h"
#include "diagramport.h"

namespace traceview {

class DiagramPortItem;

// One device on the control diagram canvas -- a labeled box with 3 output
// ports (Command/Terminal/Telemetry, right edge) and 3 matching input ports
// (left edge), one pair per DiagramPortKind. Mirrors a Device from
// DevicesGrid (identified by deviceId, same string DeviceConnection/Backend
// key everything else on) but owns none of the actual connection -- this is
// purely the diagram's view of it. Movable/selectable so the user can
// rearrange the canvas; DiagramScene listens to moved() to keep any attached
// DiagramConnectionItem's path current.
class DiagramBlockItem : public QGraphicsObject {
    Q_OBJECT

public:
    DiagramBlockItem(QString deviceId, QString label, QGraphicsItem* parent = nullptr);

    QString deviceId() const { return m_deviceId; }
    QString label() const { return m_label; }
    void setLabel(const QString& label);
    // Live-mirrored link state, painted as the same red/amber/green dot the
    // Devices tab's card shows (see deviceLinkState() in devices/device.h) --
    // set by DiagramPage from DevicesGrid::devices() whenever it changes, not
    // read from the device registry directly (this class stays independent
    // of DevicesGrid, same reasoning DeviceCard is DevicesGrid-independent).
    void setLinkState(DeviceLinkState state);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

    DiagramPortItem* port(DiagramPortKind kind, DiagramPortDirection direction) const;

signals:
    // Double-click on the block body (not on a port) -- DiagramScene forwards
    // this to DiagramPage, which owns opening the config dialog.
    void activated(const QString& deviceId);
    // Position changed -- DiagramScene reacts by refreshing any
    // DiagramConnectionItem attached to one of this block's ports.
    void moved();

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void layoutPorts();

    QString m_deviceId;
    QString m_label;
    DeviceLinkState m_linkState = DeviceLinkState::Offline;
    DiagramPortItem* m_inputs[3] = {nullptr, nullptr, nullptr};
    DiagramPortItem* m_outputs[3] = {nullptr, nullptr, nullptr};
};

}  // namespace traceview
