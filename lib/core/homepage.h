#pragma once

#include <QPixmap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "core/nearbyrobots.h"

class QBoxLayout;
class QGridLayout;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace traceview {

class DialogCard;
class HomeWordmark;
class HomePreview;

// One workspace of the open project, as a shortcut on the Home page.
struct HomeWorkspace {
    QString id;
    QString name;
    QString icon;  // Lucide id, empty for the default
    int widgets = 0;
};

// What the Home page shows of the open project (MainWindow fills it in).
struct HomeProjectSummary {
    QString title;
    QString path;
    QVector<HomeWorkspace> workspaces;
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

// The "Home" tab: where the app starts. A header with the brand and three
// numbers (robots connected, workspaces, widgets); then the open project
// ("continue where you left off", with a picture of the dashboard and one
// button per workspace) and -- in Developer mode -- recent projects, the
// dashboard gallery and the New/Open buttons; and the robots around this
// computer: the project's own devices with their state, then robots the
// NearbyRobotsService found that the project does not have yet.
//
// Lays itself out by width: two columns on a wide window, one on a tablet
// or phone, where button rows also stack (see applyWidth()).
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
    // What the dashboard last looked like; a null pixmap shows a placeholder.
    void setDashboardPreview(const QPixmap& preview);
    void setDevices(const QVector<HomeDevice>& devices);
    void setRecentProjects(const QStringList& paths);
    void setGallery(const QStringList& names, const QString& defaultName);

signals:
    void openDashboardRequested();
    void openWorkspaceRequested(const QString& workspaceId);
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
    void refreshStats();
    void refreshProjectCard();
    void refreshProjectsCard();
    void refreshRobots(bool force = false);
    void refreshScanControls();
    void refreshIcons();
    void applyWidth();
    QWidget* robotRow(const QString& title, const QString& detail, const QString& avatarIcon,
                      const QStringList& linkIcons, const QString& badgeText, int badgeTone,
                      QPushButton* action);
    QPushButton* listRow(const QString& iconId, const QString& text, const QString& tooltip);

    NearbyRobotsService* m_nearby = nullptr;
    bool m_developer = false;
    // -1 until the first layout pass, then 0 (one column) or 1 (two).
    int m_wideState = -1;
    bool m_compact = false;
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

    // Header.
    QWidget* m_hero = nullptr;
    QBoxLayout* m_heroLayout = nullptr;
    HomeWordmark* m_wordmark = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_subtitle = nullptr;
    QLabel* m_statValue[3] = {};
    QLabel* m_statCaption[3] = {};

    QBoxLayout* m_columns = nullptr;
    QVBoxLayout* m_leftColumn = nullptr;
    QVBoxLayout* m_rightColumn = nullptr;

    // Continue.
    DialogCard* m_projectCard = nullptr;
    HomePreview* m_preview = nullptr;
    QBoxLayout* m_continueRow = nullptr;
    QLabel* m_projectName = nullptr;
    QLabel* m_projectDetail = nullptr;
    QPushButton* m_openDashboardButton = nullptr;
    QLabel* m_workspacesCaption = nullptr;
    QGridLayout* m_workspaceGrid = nullptr;

    // Projects (Developer).
    DialogCard* m_projectsCard = nullptr;
    QLabel* m_recentCaption = nullptr;
    QVBoxLayout* m_recentList = nullptr;
    QLabel* m_galleryCaption = nullptr;
    QVBoxLayout* m_galleryList = nullptr;
    QGridLayout* m_projectButtons = nullptr;
    QPushButton* m_newButton = nullptr;
    QPushButton* m_openButton = nullptr;
    QPushButton* m_openLogButton = nullptr;
    QPushButton* m_manageGalleryButton = nullptr;

    // Nearby robots.
    DialogCard* m_robotsCard = nullptr;
    QLabel* m_robotsStatus = nullptr;
    QVBoxLayout* m_robotsList = nullptr;
    QGridLayout* m_scanButtons = nullptr;
    QPushButton* m_bluetoothButton = nullptr;
    QPushButton* m_deepScanButton = nullptr;
    QLabel* m_deepScanHint = nullptr;
    QProgressBar* m_deepScanProgress = nullptr;
};

}  // namespace traceview
