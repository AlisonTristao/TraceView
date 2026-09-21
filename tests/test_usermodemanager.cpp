#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "core/usermodemanager.h"

using traceview::UserModeManager;

namespace {

// UserModeManager::instance() is a true process-wide singleton -- unlike
// every other DashboardGrid/DashboardItem test in this suite (each of which
// builds its own local, independent instance), there is no way to reset it
// between test functions. These slots run in declaration order (QtTest's
// normal behavior) and deliberately build on the account state the previous
// one left behind, rather than each starting from a clean slate.
class TestUserModeManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void seedsDefaultAdminAccountOnFirstRun();
    void loginRejectsWrongCredentials();
    void logoutIsANoOpWhenAlreadyLoggedOut();
    void addAccountRequiresDeveloperMode();
    void addAccountRejectsDuplicateOrEmptyFields();
    void removeAccountRefusesTheLastOne();
    void changePasswordUpdatesLogin();

private:
    QTemporaryDir m_settingsDir;
};

void TestUserModeManager::initTestCase() {
    // Isolates every QSettings-backed read/write this test (and the
    // UserModeManager singleton it drives) makes to a throwaway directory --
    // without this, running the test would read/write the real TraceView
    // account list and leave test accounts behind in it. Must happen before
    // the very first UserModeManager::instance() call anywhere in this
    // binary, since its constructor loads (and maybe seeds) accounts
    // immediately.
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName("TraceViewTest");
    QCoreApplication::setApplicationName("test_usermodemanager");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestUserModeManager::seedsDefaultAdminAccountOnFirstRun() {
    // First instance() call in the process -- constructs (and, finding no
    // accounts under the isolated settings path from initTestCase() above,
    // seeds) the singleton right here.
    UserModeManager& manager = UserModeManager::instance();
    QCOMPARE(manager.mode(), UserModeManager::UserMode::User);
    QVERIFY(manager.accountNames().contains("admin"));

    QVERIFY(manager.login("admin", "admin233#"));
    QCOMPARE(manager.mode(), UserModeManager::UserMode::Developer);
    QCOMPARE(manager.currentUserName(), QString("admin"));

    manager.logout();
    QCOMPARE(manager.mode(), UserModeManager::UserMode::User);
    QVERIFY(manager.currentUserName().isEmpty());
}

void TestUserModeManager::loginRejectsWrongCredentials() {
    UserModeManager& manager = UserModeManager::instance();
    QVERIFY(!manager.login("admin", "not-the-password"));
    QVERIFY(!manager.login("nobody", "admin233#"));
    QCOMPARE(manager.mode(), UserModeManager::UserMode::User);
}

void TestUserModeManager::logoutIsANoOpWhenAlreadyLoggedOut() {
    UserModeManager& manager = UserModeManager::instance();
    QSignalSpy modeSpy(&manager, &UserModeManager::modeChanged);

    manager.logout();  // already logged out (previous test ended that way)
    QCOMPARE(modeSpy.count(), 0);

    QVERIFY(manager.login("admin", "admin233#"));
    QCOMPARE(modeSpy.count(), 1);
    manager.logout();
    QCOMPARE(modeSpy.count(), 2);
    manager.logout();  // redundant -- must not emit a second time
    QCOMPARE(modeSpy.count(), 2);
}

void TestUserModeManager::addAccountRequiresDeveloperMode() {
    UserModeManager& manager = UserModeManager::instance();
    QCOMPARE(manager.mode(), UserModeManager::UserMode::User);
    QVERIFY(!manager.addAccount("bob", "password1"));
    QVERIFY(!manager.accountNames().contains("bob"));

    QVERIFY(manager.login("admin", "admin233#"));
    QVERIFY(manager.addAccount("bob", "password1"));
    QVERIFY(manager.accountNames().contains("bob"));
    QVERIFY(manager.login("bob", "password1"));  // the new account works too
    manager.logout();
}

void TestUserModeManager::addAccountRejectsDuplicateOrEmptyFields() {
    UserModeManager& manager = UserModeManager::instance();
    QVERIFY(manager.login("admin", "admin233#"));

    QVERIFY(!manager.addAccount("bob", "somethingElse"));  // "bob" already exists
    QVERIFY(!manager.addAccount("", "somepassword"));      // empty username
    QVERIFY(!manager.addAccount("carol", ""));             // empty password
    QVERIFY(!manager.accountNames().contains("carol"));

    manager.logout();
}

void TestUserModeManager::removeAccountRefusesTheLastOne() {
    UserModeManager& manager = UserModeManager::instance();
    QVERIFY(manager.login("admin", "admin233#"));

    QVERIFY(manager.removeAccount("bob"));  // fine, "admin" still remains
    QVERIFY(!manager.accountNames().contains("bob"));

    // "admin" is now the sole remaining account.
    QVERIFY(!manager.removeAccount("admin"));
    QVERIFY(manager.accountNames().contains("admin"));

    manager.logout();
}

void TestUserModeManager::changePasswordUpdatesLogin() {
    UserModeManager& manager = UserModeManager::instance();
    QVERIFY(manager.login("admin", "admin233#"));
    QVERIFY(manager.changePassword("admin", "a-new-password"));
    manager.logout();

    QVERIFY(!manager.login("admin", "admin233#"));      // old password rejected
    QVERIFY(manager.login("admin", "a-new-password"));  // new one works
    manager.logout();
}

}  // namespace

QTEST_MAIN(TestUserModeManager)
#include "test_usermodemanager.moc"
