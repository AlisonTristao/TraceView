#include "serialwidgetbridge.h"

#include "backend/backend.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/controlwidgets.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "deviceconnection.h"
#include "serialmanager.h"

namespace traceview {

SerialWidgetBridge::SerialWidgetBridge(DashboardGrid* grid,
                                       std::function<DeviceConnection*(const QString&)> deviceConnectionFor,
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

void SerialWidgetBridge::wireWidget(DashboardWidget* widget) {
    if (auto* button = qobject_cast<PushButtonWidget*>(widget)) {
        connect(button, &PushButtonWidget::sendRequested, this, [this, button](const QByteArray& command) {
            if (DeviceConnection* connection = deviceConnectionForWidget(button)) {
                // Raw-text control commands only exist over Serial's console
                // byte stream (docs/PROTOCOL.md "Outbound: control
                // commands") -- a device connected over USB HID has no
                // SerialManager at all (fragmentation-and-transports.md
                // section 3.3: no console, always BTP-protocolled), so this
                // goes nowhere, same "went nowhere, not an error" contract a
                // closed port already had.
                if (SerialManager* serial = connection->serialManager()) {
                    serial->writeCommand(command);
                }
            }
        });
    } else if (auto* toggle = qobject_cast<ToggleSwitchWidget*>(widget)) {
        connect(toggle, &ToggleSwitchWidget::sendRequested, this, [this, toggle](const QByteArray& command) {
            if (DeviceConnection* connection = deviceConnectionForWidget(toggle)) {
                if (SerialManager* serial = connection->serialManager()) {
                    serial->writeCommand(command);
                }
            }
        });
    } else if (auto* slider = qobject_cast<SliderWidget*>(widget)) {
        connect(slider, &SliderWidget::sendRequested, this, [this, slider](const QByteArray& command) {
            if (DeviceConnection* connection = deviceConnectionForWidget(slider)) {
                if (SerialManager* serial = connection->serialManager()) {
                    serial->writeCommand(command);
                }
            }
        });
    } else if (auto* monitor = qobject_cast<SerialMonitorWidget*>(widget)) {
        connect(monitor, &SerialMonitorWidget::sendRequested, this, [this, monitor](const QByteArray& bytes) {
            if (DeviceConnection* connection = deviceConnectionForWidget(monitor)) {
                connection->backend()->sendTerminalIn(bytes);
            }
        });
        m_terminalDeviceIds.insert(monitor, QString());
        rewireTerminalInbound(monitor);
        connect(monitor, &QObject::destroyed, this, [this, monitor]() { m_terminalDeviceIds.remove(monitor); });
    }
}

void SerialWidgetBridge::rewireTerminalInbound(SerialMonitorWidget* monitor) {
    const QString currentDeviceId = m_terminalDeviceIds.value(monitor);
    const QString newDeviceId = m_grid->configForWidget(monitor).value("deviceId").toString();
    if (currentDeviceId == newDeviceId) {
        return;
    }

    if (DeviceConnection* oldConnection = currentDeviceId.isEmpty() ? nullptr : m_deviceConnectionFor(currentDeviceId)) {
        disconnect(oldConnection->backend(), &Backend::terminalDataReceived, monitor, &SerialMonitorWidget::appendData);
    }
    if (DeviceConnection* newConnection = newDeviceId.isEmpty() ? nullptr : m_deviceConnectionFor(newDeviceId)) {
        connect(newConnection->backend(), &Backend::terminalDataReceived, monitor, &SerialMonitorWidget::appendData);
    }
    m_terminalDeviceIds[monitor] = newDeviceId;
}

void SerialWidgetBridge::refreshTerminalWiring() {
    const QList<SerialMonitorWidget*> monitors = m_terminalDeviceIds.keys();
    for (SerialMonitorWidget* monitor : monitors) {
        rewireTerminalInbound(monitor);
    }
}

} // namespace traceview
