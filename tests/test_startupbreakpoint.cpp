// The app opens on the Home page with the Dashboard hidden behind it. A
// project loaded then must not pick its screen size from the hidden canvas's
// placeholder width (which reads as a phone); it waits for the Dashboard's
// real size. Developer mode never re-detects on resize, so it showed the
// phone layout on every launch (5.0.0, 5.0.1).

#include <QScrollArea>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>

#include "core/mainwindow.h"
#include "core/usermodemanager.h"
#include "dashboard/dashboardgrid.h"
#include "preferences/appsettings.h"
#include "usermodedefaults.h"

using namespace traceview;

class TestStartupBreakpoint : public QObject {
    Q_OBJECT

private slots:
    void developerStartOnHomeKeepsDesktopSize();
};

void TestStartupBreakpoint::developerStartOnHomeKeepsDesktopSize() {
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    QCoreApplication::setOrganizationName("TraceViewTest");
    QCoreApplication::setApplicationName("test_startupbreakpoint");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    AppSettings::instance().setUpdateAutoCheckEnabled(false);

    // Developer mode before the window exists: the startup project loads in it.
    auto& manager = UserModeManager::instance();
    QVERIFY(manager.login(kDefaultAdminUsername, kDefaultAdminPassword));

    MainWindow window;
    window.resize(1500, 950);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::qWait(100);

    auto* grid = window.findChild<DashboardGrid*>();
    auto* tabs = window.findChild<QTabBar*>();
    QVERIFY(grid);
    QVERIFY(tabs);
    int dashboardTab = -1;
    for (int i = 0; i < tabs->count(); ++i) {
        if (tabs->tabText(i).contains("Dashboard")) {
            dashboardTab = i;
        }
    }
    QVERIFY(dashboardTab >= 0);
    QVERIFY2(tabs->currentIndex() != dashboardTab, "expected the app to open on Home");
    // While hidden: nothing decided from the placeholder width.
    QCOMPARE(grid->currentBreakpoint(), DashboardBreakpoint::Large);

    tabs->setCurrentIndex(dashboardTab);
    QTest::qWait(100);
    // Shown: picked from the canvas's real width. (The offscreen screen is
    // small, so the window may be clamped: compare against what it measures,
    // with MainWindow's 700/1280 px thresholds.)
    QScrollArea* area = nullptr;
    for (QWidget* w = grid; w != nullptr && area == nullptr; w = w->parentWidget()) {
        area = qobject_cast<QScrollArea*>(w);
    }
    QVERIFY(area);
    const int width = area->viewport()->width();
    QVERIFY2(width > 700, "the canvas should be wider than a phone here");
    const DashboardBreakpoint expected =
        width > 1280 ? DashboardBreakpoint::Large : DashboardBreakpoint::Medium;
    QCOMPARE(grid->currentBreakpoint(), expected);

    manager.logout();
}

QTEST_MAIN(TestStartupBreakpoint)
#include "test_startupbreakpoint.moc"
