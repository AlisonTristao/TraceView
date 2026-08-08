#include "serialmonitorwidget.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "serialterminalwidget.h"

namespace traceview {

SerialMonitorWidget::SerialMonitorWidget(QWidget* parent) : DashboardWidget(parent) {
    m_portCombo = new QComboBox(this);
    m_portCombo->addItems({"COM1", "COM2", "COM3", "COM4"});
    m_portCombo->setToolTip("Port");

    m_baudCombo = new QComboBox(this);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_baudCombo->setCurrentText("9600");
    m_baudCombo->setToolTip("Baud rate");

    m_connectButton = new QPushButton("Connect", this);
    m_connectButton->setCheckable(true);

    auto* connectionBar = new QHBoxLayout;
    connectionBar->addStretch(1);
    connectionBar->addWidget(m_portCombo);
    connectionBar->addWidget(m_baudCombo);
    connectionBar->addWidget(m_connectButton);

    m_terminal = new SerialTerminalWidget(this);
    m_terminal->setMinimumHeight(120);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(connectionBar);
    layout->addWidget(m_terminal, 1);

    // DashboardCell resizes this widget with a direct setGeometry() call
    // (not through a parent layout), so nothing stops it from being
    // squeezed below what the connection bar actually needs — that's what
    // was squashing/overlapping the port/baud/connect controls. QWidget
    // does enforce setMinimumSize() on setGeometry() though, so pin the
    // floor to exactly what this layout requires; below that the widget
    // now just gets cleanly clipped by the cell instead of mangled.
    setMinimumSize(layout->minimumSize());

    connect(m_connectButton, &QPushButton::toggled, this, &SerialMonitorWidget::onConnectToggled);
    connect(m_terminal, &SerialTerminalWidget::sendRequested, this,
            &SerialMonitorWidget::sendRequested);
}

void SerialMonitorWidget::onConnectToggled(bool checked) {
    m_connectButton->setText(checked ? "Disconnect" : "Connect");
}

} // namespace traceview
