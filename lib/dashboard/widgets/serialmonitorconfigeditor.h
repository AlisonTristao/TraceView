#pragma once

#include <QVector>

#include "dashboard/widgetconfigeditor.h"

class QComboBox;
class QVBoxLayout;

namespace traceview {

// Settings for SerialMonitorWidget (see widgets/serialmonitorwidget.h): the
// ordered list of tabs, one row per tab, each naming the device that tab's
// TERMINAL_IN/TERMINAL_OUT traffic is routed to. Persisted as
// { "tabs": [ { "deviceId": ... }, ... ] }; a pre-tabs config (a bare
// "deviceId", or none) is read as a single tab. Same dynamic-row shape as
// ChartConfigEditor's series table -- every add/remove/reorder/pick emits
// configChanged() and the new value is fetched via config().
class SerialMonitorConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit SerialMonitorConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    void rebuildRows(const QStringList& deviceIds);
    void addRowWidget(const QString& deviceId);
    QStringList currentDeviceIds() const;
    void onStructureChanged();  // rebuild + emit
    void onPickChanged();       // just emit

    bool m_updating = false;
    QVBoxLayout* m_rowsLayout = nullptr;
    QVector<QComboBox*> m_deviceCombos;
    QVector<DeviceOption> m_devices;
};

}  // namespace traceview
