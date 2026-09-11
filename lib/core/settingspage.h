#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;

namespace traceview {

class SettingsPage final : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

    // Called by MainWindow after any update check (auto or manual) resolves,
    // so the "Last checked" label reflects it without needing this tab to be
    // reopened. A no-op if called after this instance has already been torn
    // down is impossible by construction -- MainWindow only calls it through
    // its own m_settingsTab, which it nulls out on tab close.
    void setUpdateStatusText(const QString& text);

signals:
    void clearRecentProjectsRequested();
    void restartRequested();
    void checkForUpdatesRequested();

private:
    void refreshRestartNotice();

    QLabel* m_restartNotice = nullptr;
    QPushButton* m_restartButton = nullptr;
    QLabel* m_updateStatusLabel = nullptr;
    QString m_initialLanguageId;
    int m_initialFrameLogCapacity = 0;
    int m_initialNotificationHistoryCapacity = 0;
};

}  // namespace traceview
