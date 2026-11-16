#include "serialwidgetbridge.h"

#include <QSet>

#include "backend/backend.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/chatwidget.h"
#include "dashboard/widgets/controlwidgets.h"
#include "dashboard/widgets/robotlogwidget.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "deviceconnection.h"
#include "protocol/btpbackend.h"
#ifdef TRACEVIEW_ENABLE_SERIAL
#include "serialtransport.h"
#endif

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
    Backend* backend = connection->backend();
    if (!backend) {
        return;
    }
#ifdef TRACEVIEW_ENABLE_SERIAL
    // A serial port always carries a BTP session, so raw text written to the
    // port would land in the middle of the frame stream and be dropped. A
    // robot's own port is a direct session like TCP/BLE: the command goes as
    // a COMMAND_REQUEST, which the robot runs in its shell and answers with a
    // result (CommandClient logs both in the device's log panel). A dongle's
    // port is its own console: the line goes in through the terminal, as if
    // typed in the serial monitor, with the port's line terminator.
    if (SerialTransport* serial = connection->serialTransport()) {
        const auto* btpBackend = qobject_cast<BtpBackend*>(backend);
        if (!btpBackend || !btpBackend->isSerialRobotPeer()) {
            backend->sendTerminalIn(command + lineTerminatorBytes(serial->lineTerminator()));
            return;
        }
    }
#endif
    backend->sendCommand(command);
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
    } else if (auto* chat = qobject_cast<ChatWidget*>(widget)) {
        // Same path as a Push Button's command; unlike a button, a chat
        // message has a status to show, so a device that can't take it now
        // fails the message at once instead of dropping it silently.
        connect(chat, &ChatWidget::sendRequested, this,
                [this, chat](const QByteArray& command, quint32 token) {
                    DeviceConnection* connection = deviceConnectionForWidget(chat);
                    if (!connection || !connection->isConnected() || !connection->backend()) {
                        chat->markSendFailed(token, tr("Device not connected"));
                        return;
                    }
                    sendControlCommand(connection, command);
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
    } else if (auto* logWidget = qobject_cast<RobotLogWidget*>(widget)) {
        connect(logWidget, &RobotLogWidget::tabsChanged, this,
                [this, logWidget]() { rewireLogInbound(logWidget); });
        connect(logWidget, &QObject::destroyed, this,
                [this, logWidget]() { m_logInbound.remove(logWidget); });

        m_logInbound.insert(logWidget, {});
        rewireLogInbound(logWidget);
        logWidget->setDeviceNames(m_deviceNames);
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

void SerialWidgetBridge::rewireLogInbound(RobotLogWidget* widget) {
    QList<QMetaObject::Connection>& connections = m_logInbound[widget];
    for (const QMetaObject::Connection& connection : connections) {
        QObject::disconnect(connection);
    }
    connections.clear();

    if (!m_deviceConnectionFor) {
        return;
    }

    QSet<QString> wired;
    const QStringList deviceIds = widget->tabDeviceIds();
    for (const QString& deviceId : deviceIds) {
        if (deviceId.isEmpty() || wired.contains(deviceId)) {
            continue;
        }
        wired.insert(deviceId);

        DeviceConnection* connection = m_deviceConnectionFor(deviceId);
        if (!connection || !connection->backend()) {
            continue;
        }
        connections.append(connect(
            connection->backend(), &Backend::logReceived, widget,
            [widget, deviceId](quint64 timestampUs, quint32 sourceId, quint32 bootId,
                               quint32 sequence, quint8 severity, const QString& message) {
                widget->feedDevice(deviceId, timestampUs, sourceId, bootId, sequence, severity,
                                   message);
            }));
    }
}

void SerialWidgetBridge::refreshTerminalWiring() {
    const QList<SerialMonitorWidget*> monitors = m_monitorInbound.keys();
    for (SerialMonitorWidget* monitor : monitors) {
        rewireMonitorInbound(monitor);
    }
    const QList<RobotLogWidget*> logWidgets = m_logInbound.keys();
    for (RobotLogWidget* logWidget : logWidgets) {
        rewireLogInbound(logWidget);
    }
}

void SerialWidgetBridge::setDeviceNames(const QHash<QString, QString>& namesById) {
    m_deviceNames = namesById;
    const QList<SerialMonitorWidget*> monitors = m_monitorInbound.keys();
    for (SerialMonitorWidget* monitor : monitors) {
        monitor->setDeviceNames(m_deviceNames);
    }
    const QList<RobotLogWidget*> logWidgets = m_logInbound.keys();
    for (RobotLogWidget* logWidget : logWidgets) {
        logWidget->setDeviceNames(m_deviceNames);
    }
}

}  // namespace traceview
