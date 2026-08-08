#include "serialpanelwidget.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace traceview {

SerialPanelWidget::SerialPanelWidget(QWidget* parent) : DashboardWidget(parent) {
    m_portCombo = new QComboBox(this);
    m_portCombo->addItems({"COM1", "COM2", "COM3", "COM4"});

    m_baudCombo = new QComboBox(this);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_baudCombo->setCurrentText("9600");

    m_connectButton = new QPushButton("Connect", this);
    m_connectButton->setCheckable(true);

    m_statusLabel = new QLabel("Disconnected", this);

    auto* form = new QFormLayout;
    form->addRow("Port", m_portCombo);
    form->addRow("Baud", m_baudCombo);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_connectButton);
    layout->addWidget(m_statusLabel);
    layout->addStretch();

    connect(m_connectButton, &QPushButton::toggled, this, &SerialPanelWidget::onConnectToggled);
}

void SerialPanelWidget::onConnectToggled(bool checked) {
    m_connectButton->setText(checked ? "Disconnect" : "Connect");
    m_statusLabel->setText(checked ? "Connected (placeholder)" : "Disconnected");
}

} // namespace traceview
