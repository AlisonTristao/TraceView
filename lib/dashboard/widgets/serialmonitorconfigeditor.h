#pragma once

#include "dashboard/widgetconfigeditor.h"

class QComboBox;
class QFormLayout;

namespace traceview {

// Settings for SerialMonitorWidget (see widgets/serialmonitorwidget.h): just
// which device its TERMINAL_IN/TERMINAL_OUT traffic is routed to. The
// terminal has no other per-instance config -- SerialMonitorWidget itself
// stays a passive view onto whichever device's Backend::terminalDataReceived
// SerialWidgetBridge fans this instance's bytes from/to (see
// core/serialwidgetbridge.h).
class SerialMonitorConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit SerialMonitorConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    bool m_updating = false;

    QComboBox* m_deviceCombo = nullptr;
};

} // namespace traceview
