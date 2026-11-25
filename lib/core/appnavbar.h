#pragma once

#include <QWidget>

class QButtonGroup;
class QToolButton;

namespace traceview {

class NotificationLog;

// The bottom navigation bar of compact chrome (Android, or a Developer-mode
// Small/Medium preview -- see MainWindow::compactChromeActive()), in the slot
// m_statusRow has on desktop: Home, Dashboard, Robots (Developer mode only,
// the Devices tab) and Notifications, four equal items with the icon above a
// small label. The first three follow the ribbon's current tab (none is
// checked on a log/OTA tab); Notifications is an action, with the unread
// count as a badge. Dumb like WorkspaceStrip: MainWindow connects the signals
// and calls setCurrent() from onRibbonTabChanged().
class AppNavBar : public QWidget {
    Q_OBJECT

public:
    enum class Tab { None, Home, Dashboard, Robots };

    explicit AppNavBar(QWidget* parent = nullptr);

    // Shows/hides the Robots item.
    void setDeveloperMode(bool developer);
    // Checks the matching item (Tab::None unchecks all).
    void setCurrent(Tab tab);
    // Source of the Notifications badge; optional.
    void setNotificationLog(NotificationLog* log);

    // For tests and the MCP bridge.
    QToolButton* homeButton() const { return m_home; }
    QToolButton* dashboardButton() const { return m_dashboard; }
    QToolButton* robotsButton() const { return m_robots; }
    QToolButton* notificationsButton() const { return m_notifications; }

signals:
    void homeRequested();
    void dashboardRequested();
    void robotsRequested();
    void notificationsRequested();

private:
    void updateIcons();

    QButtonGroup* m_group;
    QToolButton* m_home;
    QToolButton* m_dashboard;
    QToolButton* m_robots;
    QToolButton* m_notifications;
    NotificationLog* m_log = nullptr;
};

}  // namespace traceview
