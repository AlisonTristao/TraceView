#include "robotlogconfigeditor.h"

#include <QComboBox>
#include <QFormLayout>

namespace traceview {

RobotLogConfigEditor::RobotLogConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(tr("Which device's LOG channel this widget shows."));

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(tr("Device"), m_deviceCombo);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
}

void RobotLogConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    m_updating = false;
}

QJsonObject RobotLogConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    return cfg;
}

void RobotLogConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    populateDeviceCombo(m_deviceCombo, devices);
    m_updating = wasUpdating;
}

void RobotLogConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

}  // namespace traceview
