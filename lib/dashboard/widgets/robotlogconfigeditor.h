#pragma once

#include "dashboard/widgetconfigeditor.h"

class QComboBox;

namespace traceview {

// Settings for RobotLogWidget: which device's LOG channel this instance
// shows -- the only setting, since (unlike SerialMonitorConfigEditor) one
// widget is always exactly one device's log, never a tab strip of several.
// Same minimal "Device" combo shape as controlconfigeditor.h.
class RobotLogConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit RobotLogConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    void emitChanged();

    bool m_updating = false;
    QComboBox* m_deviceCombo = nullptr;
};

}  // namespace traceview
