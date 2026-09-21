#include "core/manageusersdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/usermodemanager.h"

namespace traceview {

ManageUsersDialog::ManageUsersDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Manage Developer Users"));
    setMinimumSize(320, 280);

    m_list = new QListWidget(this);

    auto* addButton = new QPushButton(tr("Add..."), this);
    auto* removeButton = new QPushButton(tr("Remove"), this);
    auto* changePasswordButton = new QPushButton(tr("Change Password..."), this);
    connect(addButton, &QPushButton::clicked, this, &ManageUsersDialog::onAddClicked);
    connect(removeButton, &QPushButton::clicked, this, &ManageUsersDialog::onRemoveClicked);
    connect(changePasswordButton, &QPushButton::clicked, this,
            &ManageUsersDialog::onChangePasswordClicked);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addWidget(addButton);
    buttonRow->addWidget(removeButton);
    buttonRow->addWidget(changePasswordButton);
    buttonRow->addStretch();

    auto* closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addLayout(buttonRow);
    layout->addWidget(closeButtons);

    // Every mutation (add/remove/change password) applies immediately
    // through UserModeManager -- there's no separate save step, so the list
    // just mirrors whatever accountsChanged() reports.
    connect(&UserModeManager::instance(), &UserModeManager::accountsChanged, this,
            &ManageUsersDialog::refreshList);
    refreshList();
}

void ManageUsersDialog::refreshList() {
    m_list->clear();
    m_list->addItems(UserModeManager::instance().accountNames());
}

void ManageUsersDialog::onAddClicked() {
    bool ok = false;
    const QString username =
        QInputDialog::getText(this, tr("Add User"), tr("Username:"), QLineEdit::Normal, {}, &ok);
    if (!ok || username.trimmed().isEmpty()) {
        return;
    }
    const QString password =
        QInputDialog::getText(this, tr("Add User"), tr("Password:"), QLineEdit::Password, {}, &ok);
    if (!ok || password.isEmpty()) {
        return;
    }
    if (!UserModeManager::instance().addAccount(username, password)) {
        QMessageBox::warning(this, tr("Add User"),
                             tr("Couldn't add that user — the username may already be taken."));
    }
}

void ManageUsersDialog::onRemoveClicked() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        return;
    }
    const QString username = item->text();
    if (QMessageBox::question(this, tr("Remove User"),
                              tr("Remove developer account \"%1\"?").arg(username)) !=
        QMessageBox::Yes) {
        return;
    }
    if (!UserModeManager::instance().removeAccount(username)) {
        QMessageBox::warning(this, tr("Remove User"),
                             tr("Couldn't remove the last remaining developer account."));
    }
}

void ManageUsersDialog::onChangePasswordClicked() {
    QListWidgetItem* item = m_list->currentItem();
    if (!item) {
        return;
    }
    const QString username = item->text();
    bool ok = false;
    const QString password = QInputDialog::getText(this, tr("Change Password"),
                                                   tr("New password for \"%1\":").arg(username),
                                                   QLineEdit::Password, {}, &ok);
    if (!ok || password.isEmpty()) {
        return;
    }
    UserModeManager::instance().changePassword(username, password);
}

}  // namespace traceview
