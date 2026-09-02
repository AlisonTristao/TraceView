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

signals:
    void clearRecentProjectsRequested();
    void restartRequested();

private:
    void refreshRestartNotice();

    QLabel* m_restartNotice = nullptr;
    QPushButton* m_restartButton = nullptr;
    QString m_initialLanguageId;
    int m_initialFrameLogCapacity = 0;
    int m_initialNotificationHistoryCapacity = 0;
};

}  // namespace traceview
