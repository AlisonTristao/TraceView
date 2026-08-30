#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include "dashboard/dashboardwidget.h"

class QStackedWidget;
class QToolButton;

namespace traceview {

class SerialTerminalWidget;
class TerminalTabBar;

// Serial monitor: a tab strip (TerminalTabBar) over one miniterm-style
// terminal (SerialTerminalWidget) per tab, each talking to its own device.
// Port/baud/connect config lives per-device in the Devices tab
// (DeviceConfigDialog); which devices *this* instance's tabs talk to is its
// own config (SerialMonitorConfigEditor, a "tabs" array of {deviceId}) -- this
// widget stays unaware of SerialManager/DeviceConnection itself, same as the
// control widgets in widgets/controlwidgets.h. SerialWidgetBridge
// (lib/core/serialwidgetbridge.h) resolves each tab's device and wires the
// active terminal's keystrokes to its Backend::sendTerminalIn() and every
// bound device's Backend::terminalDataReceived() back to the matching tab via
// feedDevice(), re-deriving all of it whenever the tab list changes.
//
// A thin header row carries the tab strip (hidden when there's only one tab,
// the common case and every pre-tabs project after migration) and a
// right-aligned "Clear" button that wipes the visible terminal's scrollback.
class SerialMonitorWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit SerialMonitorWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    void setEditModeHint(bool editMode) override;

    // The per-tab device ids, tab order left to right. Empty strings are kept
    // (a tab with no device chosen yet) so indices line up with the tab bar.
    QStringList tabDeviceIds() const {
        return m_deviceIds;
    }

    // Tab labels: deviceId -> display name. Re-pushed by MainWindow (through
    // SerialWidgetBridge) on every device add/remove/rename.
    void setDeviceNames(const QHash<QString, QString>& namesById);

public slots:
    // Route TERMINAL_OUT bytes to every tab bound to `deviceId` (usually one).
    void feedDevice(const QString& deviceId, const QByteArray& data);

    // Bytes for the currently visible tab, no routing -- kept for the debug
    // window's loopback wiring (core/debugchartswindow.cpp).
    void appendData(const QByteArray& data);

signals:
    // One emission per keystroke from the active terminal, tagged with that
    // tab's device id (empty if the tab has no device configured).
    void terminalInput(const QString& deviceId, const QByteArray& bytes);

    // The set of tab device ids changed (a config edit) -- SerialWidgetBridge
    // re-derives the inbound wiring.
    void tabsChanged();

private:
    void rebuildTabs(const QStringList& deviceIds);
    void showTab(int index, bool giveFocus);
    QString labelFor(const QString& deviceId) const;
    void refreshTabLabels();

    TerminalTabBar* m_tabBar = nullptr;
    QToolButton* m_clearButton = nullptr;
    QStackedWidget* m_stack = nullptr;
    QVector<SerialTerminalWidget*> m_terminals;
    QStringList m_deviceIds;
    QHash<QString, QString> m_deviceNames;
    bool m_editMode = false;
};

}  // namespace traceview
