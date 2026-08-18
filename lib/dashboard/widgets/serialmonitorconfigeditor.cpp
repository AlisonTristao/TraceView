#include "serialmonitorconfigeditor.h"

#include <QComboBox>
#include <QFormLayout>

namespace traceview {

SerialMonitorConfigEditor::SerialMonitorConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(tr("Which device this terminal's TERMINAL_IN/TERMINAL_OUT traffic is routed to."));

    auto* formLayout = new QFormLayout(this);
    formLayout->setContentsMargins(0, 8, 0, 0);
    formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    formLayout->addRow(tr("Device"), m_deviceCombo);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_updating) {
            return;
        }
        emit configChanged();
    });
}

void SerialMonitorConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    m_updating = false;
}

QJsonObject SerialMonitorConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    return cfg;
}

void SerialMonitorConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    populateDeviceCombo(m_deviceCombo, devices);
    m_updating = wasUpdating;
}

} // namespace traceview
