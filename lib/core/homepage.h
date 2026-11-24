#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/nearbyrobots.h"

class QBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace traceview {

class DialogCard;

// What the Home page shows of the open project (MainWindow fills it in).
struct HomeProjectSummary {
    QString title;
    QString path;
    int workspaces = 0;
    int devices = 0;
};

// One of the project's devices, as the "Nearby robots" list shows it.
struct HomeDevice {
    QString id;
    QString name;
    QString robotName;
    QString serialPort;
    QString tcpHost;
    QString bleAddress;
    bool connected = false;
    // Wanted but not up yet (dialing, or retrying).
    bool connecting = false;
};

// The "Home" tab: where the app starts. Left, the open project ("continue
// where you left off") and -- in Developer mode -- recent projects, the
// dashboard gallery and the New/Open buttons. Right, the robots around this
// computer: the project's own devices with their state, then robots the
// NearbyRobotsService found that the project does not have yet.
//
// The page only displays and asks: every action is a signal MainWindow
// carries out. While the page is visible it keeps the service's passive
// watch running; hidden, the watch stops (see showEvent/hideEvent).
class HomePage : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(NearbyRobotsService* nearby, QWidget* parent = nullptr);

    void setDeveloperMode(bool developer);
    void setProjectSummary(const HomeProjectSummary& summary);
    void setDevices(const QVector<HomeDevice>& devices);
    void setRecentProjects(const QStringList& paths);
    void setGallery(const QStringList& names, const QString& defaultName);

signals:
    void openDashboardRequested();
    void newProjectRequested();
    void openProjectRequested();
    void openLogRequested();
    void manageGalleryRequested();
    void openPathRequested(const QString& path);
    void openGalleryRequested(const QString& name);
    void connectToggleRequested(const QString& deviceId);
    void addRobotRequested(const traceview::NearbyRobot& robot);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildUi();
    void retranslate();
    void refreshProjectCard();
    void refreshProjectsCard();
    void refreshRobots(bool force = false);
    void refreshScanControls();
    void updateColumns();
    QWidget* robotRow(const QString& title, const QString& detail, const QStringList& linkIcons,
                      const QString& badgeText, int badgeTone, QPushButton* action);
    QPushButton* linkButton(const QString& text, const QString& tooltip);

    NearbyRobotsService* m_nearby = nullptr;
    bool m_developer = false;
    HomeProjectSummary m_project;
    QVector<HomeDevice> m_devices;
    QStringList m_recent;
    QStringList m_gallery;
    QString m_galleryDefault;
    QString m_robotsSignature;
    int m_scanProgressDone = 0;
    int m_scanProgressTotal = 0;
    QString m_lastScanResult;

    QScrollArea* m_scroll = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QBoxLayout* m_columns = nullptr;
    QVBoxLayout* m_leftColumn = nullptr;
    QVBoxLayout* m_rightColumn = nullptr;
    bool m_wide = true;

    DialogCard* m_projectCard = nullptr;
    QLabel* m_projectName = nullptr;
    QLabel* m_projectDetail = nullptr;
    QPushButton* m_openDashboardButton = nullptr;

    DialogCard* m_projectsCard = nullptr;
    QLabel* m_recentCaption = nullptr;
    QVBoxLayout* m_recentList = nullptr;
    QLabel* m_galleryCaption = nullptr;
    QVBoxLayout* m_galleryList = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_openLogButton = nullptr;
    QPushButton* m_manageGalleryButton = nullptr;

    DialogCard* m_robotsCard = nullptr;
    QLabel* m_robotsStatus = nullptr;
    QVBoxLayout* m_robotsList = nullptr;
    QPushButton* m_bluetoothButton = nullptr;
    QPushButton* m_deepScanButton = nullptr;
    QLabel* m_deepScanHint = nullptr;
    QProgressBar* m_deepScanProgress = nullptr;
};

}  // namespace traceview
