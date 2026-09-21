#pragma once

#include <QDialog>

class QListWidget;

namespace traceview {

// Developer-only account management: add/remove developer accounts, change
// a password. Opened from MainWindow's Acesso menu ("Gerenciar
// usuários..."), only reachable while already logged in -- every mutation
// here goes straight through UserModeManager, which itself no-ops outside
// Developer mode (see addAccount()/removeAccount()/changePassword()).
class ManageUsersDialog : public QDialog {
    Q_OBJECT

public:
    explicit ManageUsersDialog(QWidget* parent = nullptr);

private:
    void refreshList();
    void onAddClicked();
    void onRemoveClicked();
    void onChangePasswordClicked();

    QListWidget* m_list = nullptr;
};

}  // namespace traceview
