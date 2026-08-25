#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

#include "project/workspacemanager.h"

using traceview::Workspace;
using traceview::WorkspaceManager;

namespace {

// WorkspaceManager::instance() is a process-wide singleton; every test
// resets it first (rather than relying on QTest's declaration-order
// execution like test_projectstore.cpp does) so each one starts from the
// same known state regardless of run order.
class TestWorkspaceManager : public QObject {
    Q_OBJECT

private slots:
    void resetStartsWithOneDefaultWorkspace();
    void createWorkspaceBecomesActiveWithEmptyDashboard();
    void createWorkspaceDisambiguatesDuplicateNames();
    void removingActiveWorkspaceMovesActiveIdElsewhere();
    void removingNonActiveWorkspaceLeavesActiveIdUnchanged();
    void removingTheLastWorkspaceIsANoOp();
    void removingAnUnknownIdIsANoOp();
    void toJsonFromJsonRoundTrips();
    void fromJsonOnEmptyInputResets();
    void fromJsonOnMalformedInputResets();
};

void TestWorkspaceManager::resetStartsWithOneDefaultWorkspace() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();

    QCOMPARE(manager.workspaces().size(), 1);
    QCOMPARE(manager.activeId(), manager.workspaces().first().id);
    QVERIFY(manager.dashboardFor(manager.activeId()).isEmpty());
}

void TestWorkspaceManager::createWorkspaceBecomesActiveWithEmptyDashboard() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();

    const QString id = manager.createWorkspace("Charts");
    QCOMPARE(manager.workspaces().size(), 2);
    QCOMPARE(manager.activeId(), id);
    QCOMPARE(manager.nameFor(id), QString("Charts"));
    QVERIFY(manager.dashboardFor(id).isEmpty());
}

void TestWorkspaceManager::createWorkspaceDisambiguatesDuplicateNames() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();

    const QString first = manager.createWorkspace("Workspace");
    const QString second = manager.createWorkspace("Workspace");
    QVERIFY(first != second);
    QCOMPARE(manager.nameFor(first), QString("Workspace"));
    QCOMPARE(manager.nameFor(second), QString("Workspace 2"));
}

void TestWorkspaceManager::removingActiveWorkspaceMovesActiveIdElsewhere() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    const QString defaultId = manager.activeId();
    const QString newId = manager.createWorkspace("Second");
    QCOMPARE(manager.activeId(), newId);

    manager.removeWorkspace(newId);
    QCOMPARE(manager.workspaces().size(), 1);
    QCOMPARE(manager.activeId(), defaultId);
}

void TestWorkspaceManager::removingNonActiveWorkspaceLeavesActiveIdUnchanged() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    const QString defaultId = manager.activeId();
    const QString newId = manager.createWorkspace("Second");
    manager.setActiveId(defaultId);

    manager.removeWorkspace(newId);
    QCOMPARE(manager.workspaces().size(), 1);
    QCOMPARE(manager.activeId(), defaultId);
}

void TestWorkspaceManager::removingTheLastWorkspaceIsANoOp() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    const QString onlyId = manager.activeId();

    manager.removeWorkspace(onlyId);
    QCOMPARE(manager.workspaces().size(), 1);
    QCOMPARE(manager.activeId(), onlyId);
}

void TestWorkspaceManager::removingAnUnknownIdIsANoOp() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    manager.createWorkspace("Second");
    const int countBefore = manager.workspaces().size();

    manager.removeWorkspace("not-a-real-id");
    QCOMPARE(manager.workspaces().size(), countBefore);
}

void TestWorkspaceManager::toJsonFromJsonRoundTrips() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    const QString defaultId = manager.activeId();
    QJsonObject dashboard;
    dashboard["items"] = QJsonArray{QJsonObject{{"id", "abc"}}};
    manager.setDashboardFor(defaultId, dashboard);
    const QString secondId = manager.createWorkspace("Second");
    manager.setActiveId(defaultId);

    const QJsonObject serialized = manager.toJson();

    manager.reset();  // scramble state before reloading, like ProjectStore::load() would
    manager.fromJson(serialized);

    QCOMPARE(manager.workspaces().size(), 2);
    QCOMPARE(manager.activeId(), defaultId);
    QCOMPARE(manager.dashboardFor(defaultId), dashboard);
    QCOMPARE(manager.nameFor(secondId), QString("Second"));
}

void TestWorkspaceManager::fromJsonOnEmptyInputResets() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    manager.createWorkspace("Second");
    QCOMPARE(manager.workspaces().size(), 2);

    manager.fromJson(QJsonObject());
    QCOMPARE(manager.workspaces().size(), 1);
}

void TestWorkspaceManager::fromJsonOnMalformedInputResets() {
    WorkspaceManager& manager = WorkspaceManager::instance();
    manager.reset();
    manager.createWorkspace("Second");

    QJsonObject malformed;
    malformed["list"] = QJsonArray{QJsonObject{{"name", "no id field"}}};
    manager.fromJson(malformed);

    QCOMPARE(manager.workspaces().size(), 1);
}

}  // namespace

QTEST_MAIN(TestWorkspaceManager)
#include "test_workspacemanager.moc"
