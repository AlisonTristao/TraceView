#include "serialwidgetbridge.h"

#include <QSet>

#include "backend/backend.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/controlwidgets.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "deviceconnection.h"
#include "serialmanager.h"

namespace traceview {

SerialWidgetBridge::SerialWidgetBridge(
    DashboardGrid* grid, std::function<DeviceConnection*(const QString&)> deviceConnectionFor,
    QObject* parent)
    : QObject(parent), m_grid(grid), m_deviceConnectionFor(std::move(deviceConnectionFor)) {
    connect(grid, &DashboardGrid::widgetCreated, this, &SerialWidgetBridge::wireWidget);
}

DeviceConnection* SerialWidgetBridge::deviceConnectionForWidget(DashboardWidget* widget) const {
    const QString deviceId = m_grid->configForWidget(widget).value("deviceId").toString();
    if (deviceId.isEmpty() || !m_deviceConnectionFor) {
        return nullptr;
    }
    return m_deviceConnectionFor(deviceId);
}

void SerialWidgetBridge::sendControlCommand(DeviceConnection* connection,
                                            const QByteArray& command) {
    if (SerialManager* serial = connection->serialManager()) {
        serial->writeCommand(command);
        return;
    }
    if (Backend* backend = connection->backend()) {
        backend->sendCommand(command);
    }
}

void SerialWidgetBridge::wireWidget(DashboardWidget* widget) {
    if (auto* button = qobject_cast<PushButtonWidget*>(widget)) {
        connect(button, &PushButtonWidget::sendRequested, this,
                [this, button](const QByteArray& command) {
                    if (DeviceConnection* connection = deviceConnectionForWidget(button)) {
                        sendControlCommand(connection, command);
                    }
                });
    } else if (auto* toggle = qobject_cast<ToggleSwitchWidget*>(widget)) {
        connect(toggle, &ToggleSwitchWidget::sendRequested, this,
                [this, toggle](const QByteArray& command) {
                    if (DeviceConnection* connection = deviceConnectionForWidget(toggle)) {
                        sendControlCommand(connection, command);
                    }
                });
    } else if (auto* slider = qobject_cast<SliderWidget*>(widget)) {
        connect(slider, &SliderWidget::sendRequested, this,
                [this, slider](const QByteArray& command) {
                    if (DeviceConnection* connection = deviceConnectionForWidget(slider)) {
                        sendControlCommand(connection, command);
                    }
                });
    } else if (auto* monitor = qobject_cast<SerialMonitorWidget*>(widget)) {
        connect(monitor, &SerialMonitorWidget::terminalInput, this,
                [this](const QString& deviceId, const QByteArray& bytes) {
                    if (deviceId.isEmpty() || !m_deviceConnectionFor) {
                        return;
                    }
                    if (DeviceConnection* connection = m_deviceConnectionFor(deviceId)) {
                        if (Backend* backend = connection->backend()) {
                            backend->sendTerminalIn(bytes);
                        }
                    }
                });
        connect(monitor, &SerialMonitorWidget::tabsChanged, this,
                [this, monitor]() { rewireMonitorInbound(monitor); });
        connect(monitor, &QObject::destroyed, this,
                [this, monitor]() { m_monitorInbound.remove(monitor); });

        m_monitorInbound.insert(monitor, {});
        rewireMonitorInbound(monitor);
        monitor->setDeviceNames(m_deviceNames);
    }
}

void SerialWidgetBridge::rewireMonitorInbound(SerialMonitorWidget* monitor) {
    QList<QMetaObject::Connection>& connections = m_monitorInbound[monitor];
    for (const QMetaObject::Connection& connection : connections) {
        QObject::disconnect(connection);
    }
    connections.clear();

    if (!m_deviceConnectionFor) {
        return;
    }

    QSet<QString> wired;
    const QStringList deviceIds = monitor->tabDeviceIds();
    for (const QString& deviceId : deviceIds) {
        if (deviceId.isEmpty() || wired.contains(deviceId)) {
            continue;
        }
        wired.insert(deviceId);

        DeviceConnection* connection = m_deviceConnectionFor(deviceId);
        if (!connection || !connection->backend()) {
            continue;
        }
        connections.append(connect(connection->backend(), &Backend::terminalDataReceived, monitor,
                                   [monitor, deviceId](const QByteArray& data) {
                                       monitor->feedDevice(deviceId, data);
                                   }));
    }
}

void SerialWidgetBridge::refreshTerminalWiring() {
    const QList<SerialMonitorWidget*> monitors = m_monitorInbound.keys();
    for (SerialMonitorWidget* monitor : monitors) {
        rewireMonitorInbound(monitor);
    }
}

void SerialWidgetBridge::setDeviceNames(const QHash<QString, QString>& namesById) {
    m_deviceNames = namesById;
    const QList<SerialMonitorWidget*> monitors = m_monitorInbound.keys();
    for (SerialMonitorWidget* monitor : monitors) {
        monitor->setDeviceNames(m_deviceNames);
    }
}

}  // namespace traceview
