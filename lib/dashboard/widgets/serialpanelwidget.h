#pragma once

#include "dashboard/dashboardwidget.h"

class QComboBox;
class QLabel;
class QPushButton;

namespace traceview {

// Front-end shell for a serial connection element: port/baud pickers and a
// connect toggle. Purely visual for now — nothing here talks to
// QSerialPort; the Connect button doesn't open a port, the port list is a
// placeholder, not an enumeration of real devices. Wiring it up to an
// actual connection is a later, separate step.
class SerialPanelWidget : public DashboardWidget {
public:
    explicit SerialPanelWidget(QWidget* parent = nullptr);

private:
    void onConnectToggled(bool checked);

    QComboBox* m_portCombo = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QPushButton* m_connectButton = nullptr;
    QLabel* m_statusLabel = nullptr;
};

} // namespace traceview
