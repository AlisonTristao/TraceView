#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include "dashboard/dashboardwidget.h"

class QStackedWidget;
class QTableView;
class QToolButton;

namespace traceview {

class RobotLogModel;
class TerminalTabBar;

// A read-only, live "serial monitor" for one or more robots' LOG channels:
// a tab strip (TerminalTabBar, reused as-is from SerialMonitorWidget -- see
// widgets/terminaltabbar.h) over one QTableView/RobotLogModel pair per tab,
// each bound to its own device. Same shape as SerialMonitorWidget (thin
// header row, tab strip hidden when there's only one tab, right-aligned
// Clear button) but no input line -- LOG has no reply half to type into,
// only firmware output.
//
// Port/baud/connect config lives per-device in the Devices tab; which
// devices *this* instance's tabs read from is its own config
// (RobotLogConfigEditor, a "tabs" array of {deviceId}) -- this widget stays
// unaware of Backend/DeviceConnection itself, same as SerialMonitorWidget.
// core/serialwidgetbridge.h resolves each tab's device and wires every bound
// device's Backend::logReceived() to feedDevice(), re-deriving all of it
// whenever the tab list changes.
class RobotLogWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit RobotLogWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;

    // The per-tab device ids, tab order left to right. Empty strings are
    // kept (a tab with no device chosen yet) so indices line up with the tab
    // bar -- same contract as SerialMonitorWidget::tabDeviceIds().
    QStringList tabDeviceIds() const {
        return m_deviceIds;
    }

    // Tab labels: deviceId -> display name. Re-pushed by MainWindow (through
    // SerialWidgetBridge) on every device add/remove/rename.
    void setDeviceNames(const QHash<QString, QString>& namesById);

public slots:
    // One LOG record for `deviceId` -- routed to every tab bound to it
    // (usually one). `severity` is the wire's raw octet (traceview::
    // LogSeverity, protocol/logseverity.h) -- see Backend::logReceived().
    void feedDevice(const QString& deviceId, quint64 timestampUs, quint32 sourceId,
                    quint32 bootId, quint32 sequence, quint8 severity, const QString& message);

    // Clears the currently visible tab's log.
    void clearLog();

signals:
    // The set of tab device ids changed (a config edit) -- SerialWidgetBridge
    // re-derives the inbound wiring.
    void tabsChanged();

private:
    void rebuildTabs(const QStringList& deviceIds);
    void showTab(int index);
    QString labelFor(const QString& deviceId) const;
    void refreshTabLabels();

    TerminalTabBar* m_tabBar = nullptr;
    QToolButton* m_clearButton = nullptr;
    QStackedWidget* m_stack = nullptr;
    QVector<QTableView*> m_tables;
    QVector<RobotLogModel*> m_models;
    QStringList m_deviceIds;
    QHash<QString, QString> m_deviceNames;
};

}  // namespace traceview
