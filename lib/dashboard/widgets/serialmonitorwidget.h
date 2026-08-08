#pragma once

#include "dashboard/dashboardwidget.h"

class QComboBox;
class QLabel;
class QPushButton;

namespace traceview {

class SerialTerminalWidget;

// Serial monitor: a connection bar (port/baud pickers, connect toggle)
// above a miniterm-style terminal (see SerialTerminalWidget). Purely
// visual for now — nothing here talks to QSerialPort; the Connect button
// doesn't open a port, the port list is a placeholder, and the terminal's
// sendRequested() has nowhere to write yet. Wiring it up to an actual
// connection is a later, separate step.
class SerialMonitorWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit SerialMonitorWidget(QWidget* parent = nullptr);

signals:
    // Forwarded from the terminal: one emission per keystroke, raw bytes,
    // ready for a future backend to write to the open QSerialPort.
    void sendRequested(const QByteArray& data);

private:
    void onConnectToggled(bool checked);

    QComboBox* m_portCombo = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QPushButton* m_connectButton = nullptr;
    SerialTerminalWidget* m_terminal = nullptr;
};

} // namespace traceview
