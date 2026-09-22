#include <QAction>
#include <QMenu>
#include <QLabel>
#include <QLayout>
#include <QTreeWidget>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QtTest>

#include "core/mainwindow.h"
#include "core/workspaceswitcher.h"
#include "project/workspacemanager.h"
#include "core/usermodemanager.h"
#include "dashboard/dashboardgrid.h"
#include "preferences/appsettings.h"
#include "usermodedefaults.h"

using namespace traceview;

class TestMainWindowChrome : public QObject {
    Q_OBJECT

private slots:
    void userPreviewKeepsManualSizes();
};

void TestMainWindowChrome::userPreviewKeepsManualSizes() {
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QCoreApplication::setOrganizationName("TraceViewTest");
    QCoreApplication::setApplicationName("test_mainwindowchrome");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    AppSettings::instance().setUpdateAutoCheckEnabled(false);

    MainWindow window;
    window.show();
    auto* tabs = window.findChild<QTabBar*>();
    auto* selector = window.findChild<QToolButton*>("screenSizeButton");
    auto* grid = window.findChild<DashboardGrid*>();
    auto* options = window.findChild<QToolButton*>("optionsButton");
    auto* deviceStatus = window.findChild<QLabel*>("deviceStatusLabel");
    QVERIFY(tabs);
    QVERIFY(selector);
    QVERIFY(grid);
    QVERIFY(options);
    QVERIFY(deviceStatus);
    QVERIFY(!tabs->isVisible());
    QVERIFY(!selector->isVisible());
    QCOMPARE(selector->parentWidget()->objectName(), QString("statusRow"));

    auto& manager = UserModeManager::instance();
    QVERIFY(manager.login(kDefaultAdminUsername, kDefaultAdminPassword));
    QVERIFY(tabs->isVisible());
    QVERIFY(selector->isVisible());
    const auto sizes = selector->menu()->actions();
    QCOMPARE(sizes.size(), 3);
    QCOMPARE(sizes[0]->text(), QString("Small"));
    QCOMPARE(sizes[1]->text(), QString("Medium"));
    QCOMPARE(sizes[2]->text(), QString("Large"));

    const auto previewAction = [&window]() -> QAction* {
        for (auto* action : window.findChildren<QAction*>()) {
            if (action->text() == "View as user") {
                return action;
            }
        }
        return nullptr;
    };
    QVERIFY(previewAction());
    tabs->setCurrentIndex(1);
    previewAction()->trigger();
    QTRY_VERIFY(!tabs->isVisible());
    QCOMPARE(tabs->currentIndex(), 0);
    QVERIFY(selector->isVisible());
    QVERIFY(previewAction()->isChecked());
    QCOMPARE(manager.mode(), UserModeManager::UserMode::Developer);

    for (int i = 0; i < sizes.size(); ++i) {
        sizes[i]->trigger();
        QCoreApplication::processEvents();
        QCOMPARE(int(grid->currentBreakpoint()), sizes[i]->data().toInt());
        QVERIFY(sizes[i]->isChecked());
        QVERIFY(!tabs->isVisible());
        QVERIFY(selector->isVisible());
        QCOMPARE(options->parentWidget(), deviceStatus->parentWidget());
        QCOMPARE(options->parentWidget()->layout()->indexOf(options), 0);
        QCOMPARE(options->parentWidget()->layout()->indexOf(deviceStatus), 1);
        QCOMPARE(options->isVisible(), i < 2);
    }
    previewAction()->trigger();
    QTRY_VERIFY(tabs->isVisible());
    QVERIFY(!previewAction()->isChecked());

    previewAction()->trigger();
    QTRY_VERIFY(!tabs->isVisible());
    manager.logout();
    auto* switcher = window.findChild<WorkspaceSwitcher*>();
    auto* subscriptions = window.findChild<QTreeWidget*>("subscriptionsWorkspace");
    QVERIFY(switcher);
    QVERIFY(subscriptions);
    auto& workspaces = WorkspaceManager::instance();
    const QString originalId = workspaces.activeId();
    const auto originalDashboard = grid->toJson();
    const auto workspaceCount = workspaces.workspaces().size();
    switcher->workspaceSelected("builtin:subscriptions");
    QVERIFY(subscriptions->isVisible());
    QVERIFY(!grid->isVisible());
    QCOMPARE(workspaces.activeId(), originalId);
    QCOMPARE(workspaces.workspaces().size(), workspaceCount);
    QCOMPARE(grid->toJson(), originalDashboard);
    QCOMPARE(subscriptions->topLevelItem(0)->text(0), QString("No subscriptions"));
    switcher->workspaceDeleteRequested("builtin:subscriptions");
    QCOMPARE(workspaces.workspaces().size(), workspaceCount);
    switcher->workspaceSelected(originalId);
    QVERIFY(grid->isVisible());
    QVERIFY(!subscriptions->isVisible());
    QCOMPARE(grid->toJson(), originalDashboard);
    QVERIFY(!selector->isVisible());
    QVERIFY(!tabs->isVisible());
    QVERIFY(manager.login(kDefaultAdminUsername, kDefaultAdminPassword));
    QVERIFY(tabs->isVisible());
    QVERIFY(!previewAction()->isChecked());
    manager.logout();
}

QTEST_MAIN(TestMainWindowChrome)
#include "test_mainwindowchrome.moc"
