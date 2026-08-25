#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <functional>

namespace traceview {

class DashboardGrid;
class DashboardWidget;
class DeviceConnection;
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
// fully-formed outbound command) goes through that device's
// SerialManager::writeCommand() directly (appends the configured line
// terminator, docs/PROTOCOL.md "Outbound: control commands") -- this is raw
// text with no protocol envelope, so it bypasses Backend entirely. A widget
// with no device configured (or whose configured device doesn't exist,
// e.g. it was since removed) just goes nowhere -- same "went nowhere, not
// an error" contract SerialManager::write() already has for a closed port.
//
// SerialMonitorWidget is different in both directions: its sendRequested()
// (raw keystrokes/escape sequences) is handed to that device's
// Backend::sendTerminalIn() instead of SerialManager::write() directly, and
// -- unlike the outbound-only widgets above -- it also needs a standing
// inbound connection (Backend::terminalDataReceived -> appendData()) that
// must be re-pointed if the widget's target device changes later, since
// that can't be resolved fresh per call the way an outbound send can.
// refreshTerminalWiring() is what re-derives those connections; call it
// whenever a config edit could have changed a terminal widget's deviceId
// (MainWindow: the same indexChanged hook that drives
// refreshWidgetSubscriptions()).
class SerialWidgetBridge : public QObject {
    Q_OBJECT

public:
    SerialWidgetBridge(DashboardGrid* grid,
                       std::function<DeviceConnection*(const QString&)> deviceConnectionFor,
                       QObject* parent = nullptr);

    // Re-derives every wired terminal widget's inbound connection from its
    // current config. Outbound wiring (control widgets, and the terminal's
    // own sendRequested()) needs no equivalent call -- it's resolved fresh
    // on every send, see the class comment.
    void refreshTerminalWiring();

private:
    void wireWidget(DashboardWidget* widget);
    DeviceConnection* deviceConnectionForWidget(DashboardWidget* widget) const;
    // (Re)connects `monitor`'s inbound Backend::terminalDataReceived to
    // whichever device its config currently names, disconnecting the
    // previous one first if it changed. No-op if the device is unchanged.
    void rewireTerminalInbound(SerialMonitorWidget* monitor);

    DashboardGrid* m_grid;
    std::function<DeviceConnection*(const QString&)> m_deviceConnectionFor;
    // Every terminal widget wired so far, and which device's Backend its
    // inbound connection currently points at (empty = none) -- lets
    // rewireTerminalInbound() know what to disconnect before connecting the
    // new one, and lets refreshTerminalWiring() enumerate them all.
    QHash<SerialMonitorWidget*, QString> m_terminalDeviceIds;
};

}  // namespace traceview
