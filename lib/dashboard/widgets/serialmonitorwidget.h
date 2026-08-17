#pragma once

#include "dashboard/dashboardwidget.h"

namespace traceview {

class SerialTerminalWidget;

// Serial monitor: a thin wrapper around a miniterm-style terminal (see
// SerialTerminalWidget). The port/baud pickers and connect toggle that used
// to live in a header here now live once for the whole app, in the Run
// ribbon tab (MainWindow) -- there is one QSerialPort for the entire app
// (SerialManager), not one per widget, so every SerialMonitorWidget on the
// dashboard is just a view onto that shared connection, not an owner of it.
// SerialWidgetBridge (lib/core/serialwidgetbridge.h) wires sendRequested()
// to SerialManager::write() and SerialManager::dataReceived() to
// appendData() for every instance on the grid (BACKEND_TODO.txt Task 10) --
// this widget stays unaware of SerialManager itself, same as the control
// widgets in widgets/controlwidgets.h.
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

} // namespace traceview
