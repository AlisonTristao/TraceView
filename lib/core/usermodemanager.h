#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace traceview {

// Session-only gate between the two ways TraceView can be operated: a
// restricted "user" mode (the default on every launch, never persisted) and
// an unlocked "developer" mode reached by logging in as one of the
// registered accounts. MainWindow reacts to modeChanged() to hide/show the
// Devices tab, the dashboard edit-mode lock, workspace management, and the
// screen-size breakpoint toggle -- see its own modeChanged wiring for what
// exactly that covers.
//
// Accounts themselves DO persist (QSettings, app-wide, not per .tvproj)
// since they represent who is allowed to operate this installation, not
// state belonging to any one project. Passwords are never stored in the
// clear -- only a salted SHA-256 hash. The very first account ("admin",
// see the .cpp) is seeded automatically the first time no accounts exist
// at all -- the same fixed default on every TraceView install/device,
// not a per-machine setup step -- so login always works out of the box;
// change its password (or add other accounts) via Manage Users.
class UserModeManager : public QObject {
    Q_OBJECT

public:
    enum class UserMode { User, Developer };

    static UserModeManager& instance();

    UserMode mode() const {
        return m_mode;
    }
    // Empty in User mode; the username passed to login() once in Developer
    // mode. Exposed so future auditing (log entries) can tag who made a
    // change -- no such logging exists yet, this just makes the identity
    // available.
    QString currentUserName() const {
        return m_currentUserName;
    }

    QStringList accountNames() const;

    bool login(const QString& username, const QString& password);
    void logout();

    // Only meaningful while already in Developer mode -- every mutator below
    // is a no-op (returns false) otherwise.
    bool addAccount(const QString& username, const QString& password);
    // Refuses to remove the last remaining account -- dropping to zero would
    // just re-seed the default "admin" account on the next launch anyway
    // (see seedDefaultAccount()), so there's no point letting it happen.
    bool removeAccount(const QString& username);
    bool changePassword(const QString& username, const QString& newPassword);

signals:
    void modeChanged(UserMode mode);
    void accountsChanged();

private:
    UserModeManager();

    struct Account {
        QString username;
        QByteArray passwordHash;
        QByteArray salt;
    };

    void loadAccounts();
    void saveAccounts() const;
    // Called once from the constructor when loadAccounts() leaves m_accounts
    // empty (a fresh install, or every account somehow got removed outside
    // the UI, e.g. a wiped QSettings) -- adds the fixed default "admin"
    // account so login always works without any first-run setup dialog.
    void seedDefaultAccount();
    int indexOfAccount(const QString& username) const;
    static QByteArray randomSalt();
    static QByteArray hashPassword(const QString& password, const QByteArray& salt);

    UserMode m_mode = UserMode::User;
    QString m_currentUserName;
    QVector<Account> m_accounts;
};

}  // namespace traceview
