#pragma once

#include "dashboard/dashboardwidget.h"

namespace traceview {

class SerialTerminalWidget;

// Serial monitor: a thin wrapper around a miniterm-style terminal (see
// SerialTerminalWidget). Port/baud/connect config lives per-device in the
// Devices tab (DeviceConfigDialog); which device *this* instance talks to is
// its own config (SerialMonitorConfigEditor, "deviceId") -- this widget
// stays unaware of SerialManager/DeviceConnection itself, same as the
// control widgets in widgets/controlwidgets.h. SerialWidgetBridge
// (lib/core/serialwidgetbridge.h) resolves that device and wires
// sendRequested() to its Backend::sendTerminalIn() and its
// Backend::terminalDataReceived() to appendData(), re-pointing both if the
// widget's configured device changes.
class SerialMonitorWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit SerialMonitorWidget(QWidget* parent = nullptr);

public slots:
    // Raw bytes off the wire, shown verbatim -- the terminal is a passive
    // debug tap on the whole stream, not filtered by key like chart/gauge
    // payloads are (docs/PROTOCOL.md "Malformed lines").
    void appendData(const QByteArray& data);

signals:
    // Forwarded from the terminal: one emission per keystroke, raw bytes,
    // ready for a future backend to write to the open QSerialPort.
    void sendRequested(const QByteArray& data);

private:
    SerialTerminalWidget* m_terminal = nullptr;
};

}  // namespace traceview
