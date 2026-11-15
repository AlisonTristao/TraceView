#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

namespace traceview {

class DashboardGrid;
class DashboardWidget;
class DeviceConnection;
class RobotLogWidget;
class SerialMonitorWidget;

// Wires each control/serial-monitor widget's send/receive to whichever
// device its own config currently targets (config()["deviceId"], see
// dashboard/widgetconfigeditor.h's DeviceOption) -- resolved fresh via
// `deviceConnectionFor` on every outbound send rather than a fixed
// SerialManager/Backend pair (the pre-multi-device-refactor shape, see git
// history), since a widget's target device can be changed at any time in
// the properties panel.
//
// PushButtonWidget/ToggleSwitchWidget/SliderWidget's sendRequested() (a
// fully-formed outbound command) goes through Backend::sendCommand() -- a
// real COMMAND_REQUEST (see protocol/commandclient.h) for a robot reached
// directly (its own serial port, TCP, BLE) or through a hub channel, a silent
// no-op for USB HID. The one exception is a dongle's serial port, which is the
// dongle's own console: there it goes into the terminal through
// Backend::sendTerminalIn() with the port's line terminator appended --
// exactly what typing it in the serial monitor does. (It used to be written
// raw to the port, which a BTP session just discards.) A widget with no
// device configured just goes nowhere.
//
// SerialMonitorWidget is different: it has one terminal *tab* per device, so
// its outbound signal is terminalInput(deviceId, bytes) -- the active tab's
// keystrokes, routed to that device's Backend::sendTerminalIn(). Inbound is
// one standing Backend::terminalDataReceived -> monitor->feedDevice(deviceId, ..)
// connection per distinct bound device; rewireMonitorInbound() rebuilds that
// set whenever the monitor's tab list changes (SerialMonitorWidget::tabsChanged,
// and the subscription-refresh hook MainWindow already drives). MainWindow also
// pushes device names through here for the tab labels.
//
// RobotLogWidget is inbound-only but otherwise the same tab-per-device shape
// as SerialMonitorWidget (its own "tabs" config, RobotLogConfigEditor): one
// standing Backend::logReceived -> widget->feedDevice(deviceId, ...)
// connection per distinct bound device, rebuilt by rewireLogInbound()
// whenever RobotLogWidget::tabsChanged() fires. refreshTerminalWiring()
// below re-derives this alongside the monitors on every config edit despite
// its name (same hook, no new one needed) -- an unrenamed name that still
// means "everything with inbound wiring" here.
class SerialWidgetBridge : public QObject {
    Q_OBJECT

public:
    SerialWidgetBridge(DashboardGrid* grid,
                       std::function<DeviceConnection*(const QString&)> deviceConnectionFor,
                       QObject* parent = nullptr);

    // Re-derives every wired serial monitor's inbound connections from its
    // current tab list. Outbound wiring needs no equivalent -- it's resolved
    // fresh on every send, see the class comment.
    void refreshTerminalWiring();

    // deviceId -> display name, for the monitors' tab labels. Pushed by
    // MainWindow on every device add/remove/rename.
    void setDeviceNames(const QHash<QString, QString>& namesById);

    // Backend::sendCommand(), or the terminal (Backend::sendTerminalIn() plus
    // the line terminator) on a dongle's serial port -- see the class comment
    // above. Public for the
    // AI assistant's send_command, which sends exactly what a button would.
    static void sendControlCommand(DeviceConnection* connection, const QByteArray& command);

private:
    void wireWidget(DashboardWidget* widget);
    DeviceConnection* deviceConnectionForWidget(DashboardWidget* widget) const;
    // Tears down `monitor`'s inbound connections and rebuilds one per distinct
    // non-empty device id in monitor->tabDeviceIds().
    void rewireMonitorInbound(SerialMonitorWidget* monitor);
    // Tears down `widget`'s inbound connections and rebuilds one per distinct
    // non-empty device id in widget->tabDeviceIds() -- same shape as
    // rewireMonitorInbound(), just feedDevice() taking the LOG fields instead
    // of raw bytes.
    void rewireLogInbound(RobotLogWidget* widget);

    DashboardGrid* m_grid;
    std::function<DeviceConnection*(const QString&)> m_deviceConnectionFor;
    // Every serial monitor wired so far -> its live inbound connections (one
    // per distinct bound device). The key set also enumerates the monitors
    // for refreshTerminalWiring()/the device-meta pushes.
    QHash<SerialMonitorWidget*, QList<QMetaObject::Connection>> m_monitorInbound;
    // Every robot log widget wired so far -> its live inbound connections
    // (one per distinct bound device), same shape as m_monitorInbound.
    QHash<RobotLogWidget*, QList<QMetaObject::Connection>> m_logInbound;

    QHash<QString, QString> m_deviceNames;
};

}  // namespace traceview
