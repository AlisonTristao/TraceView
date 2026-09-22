#include "core/usermodemanager.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>

#include "usermodedefaults.h"

namespace traceview {

namespace {
constexpr char kAccountsGroup[] = "developerAccounts";
// Multiple of sizeof(quint32) -- randomSalt() fills it via
// QRandomGenerator::fillRange(), which works in quint32 units.
constexpr int kSaltLength = 16;
}  // namespace

UserModeManager& UserModeManager::instance() {
    static UserModeManager manager;
    return manager;
}

UserModeManager::UserModeManager() {
    loadAccounts();
    if (m_accounts.isEmpty()) {
        seedDefaultAccount();
    }
}

QStringList UserModeManager::accountNames() const {
    QStringList names;
    names.reserve(m_accounts.size());
    for (const Account& account : m_accounts) {
        names.append(account.username);
    }
    return names;
}

int UserModeManager::indexOfAccount(const QString& username) const {
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i].username.compare(username, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

QByteArray UserModeManager::randomSalt() {
    QByteArray salt;
    salt.resize(kSaltLength);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(salt.data()),
                                          kSaltLength / int(sizeof(quint32)));
    return salt;
}

QByteArray UserModeManager::hashPassword(const QString& password, const QByteArray& salt) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(salt);
    hash.addData(password.toUtf8());
    return hash.result();
}

bool UserModeManager::usingDefaultPassword() const {
    // Only worth checking at all if the build left the password at its
    // shipped placeholder in the first place -- a deployment that set its
    // own TRACEVIEW_DEFAULT_ADMIN_PASSWORD never matches kDefaultAdminPassword
    // below (see usermodedefaults.h.in), so this is also just a cheap
    // early-out.
    if (!kDefaultAdminPasswordIsPlaceholder) {
        return false;
    }
    const int index = indexOfAccount(QString::fromLatin1(kDefaultAdminUsername));
    if (index < 0) {
        return false;
    }
    // Reuses the same hashPassword() the real login() path uses, against
    // that account's own salt -- true only if nobody has changed "admin"'s
    // password away from the seeded default since.
    const Account& account = m_accounts[index];
    return hashPassword(QString::fromLatin1(kDefaultAdminPassword), account.salt) ==
           account.passwordHash;
}

void UserModeManager::seedDefaultAccount() {
    const QByteArray salt = randomSalt();
    m_accounts.append(Account{QString::fromLatin1(kDefaultAdminUsername),
                              hashPassword(QString::fromLatin1(kDefaultAdminPassword), salt),
                              salt});
    saveAccounts();
}

bool UserModeManager::login(const QString& username, const QString& password) {
    const int index = indexOfAccount(username);
    if (index < 0) {
        return false;
    }
    const Account& account = m_accounts[index];
    if (hashPassword(password, account.salt) != account.passwordHash) {
        return false;
    }
    m_currentUserName = account.username;
    m_mode = UserMode::Developer;
    emit modeChanged(m_mode);
    return true;
}

void UserModeManager::logout() {
    if (m_mode == UserMode::User) {
        return;
    }
    m_mode = UserMode::User;
    m_currentUserName.clear();
    emit modeChanged(m_mode);
}

bool UserModeManager::addAccount(const QString& username, const QString& password) {
    if (m_mode != UserMode::Developer || username.trimmed().isEmpty() || password.isEmpty()) {
        return false;
    }
    if (indexOfAccount(username) >= 0) {
        return false;
    }
    const QByteArray salt = randomSalt();
    m_accounts.append(Account{username.trimmed(), hashPassword(password, salt), salt});
    saveAccounts();
    emit accountsChanged();
    return true;
}

bool UserModeManager::removeAccount(const QString& username) {
    if (m_mode != UserMode::Developer || m_accounts.size() <= 1) {
        return false;
    }
    const int index = indexOfAccount(username);
    if (index < 0) {
        return false;
    }
    m_accounts.removeAt(index);
    saveAccounts();
    emit accountsChanged();
    return true;
}

bool UserModeManager::changePassword(const QString& username, const QString& newPassword) {
    if (m_mode != UserMode::Developer || newPassword.isEmpty()) {
        return false;
    }
    const int index = indexOfAccount(username);
    if (index < 0) {
        return false;
    }
    const QByteArray salt = randomSalt();
    m_accounts[index].salt = salt;
    m_accounts[index].passwordHash = hashPassword(newPassword, salt);
    saveAccounts();
    emit accountsChanged();
    return true;
}

void UserModeManager::loadAccounts() {
    QSettings settings;
    const int count = settings.beginReadArray(kAccountsGroup);
    m_accounts.clear();
    m_accounts.reserve(count);
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Account account;
        account.username = settings.value("username").toString();
        account.passwordHash = settings.value("passwordHash").toByteArray();
        account.salt = settings.value("salt").toByteArray();
        if (!account.username.isEmpty()) {
            m_accounts.append(account);
        }
    }
    settings.endArray();
}

void UserModeManager::saveAccounts() const {
    QSettings settings;
    settings.beginWriteArray(kAccountsGroup);
    for (int i = 0; i < m_accounts.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("username", m_accounts[i].username);
        settings.setValue("passwordHash", m_accounts[i].passwordHash);
        settings.setValue("salt", m_accounts[i].salt);
    }
    settings.endArray();
}

}  // namespace traceview
