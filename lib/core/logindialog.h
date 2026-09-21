#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;

namespace traceview {

// Prompts for developer-mode credentials -- opened by MainWindow's Acesso
// menu ("Entrar como desenvolvedor..."). Every install always has at least
// the default "admin" account (see UserModeManager::seedDefaultAccount()),
// so this is always a plain login form, never a first-run setup step. On
// accept, the account is already logged into via UserModeManager -- the
// caller just needs to close the dialog.
class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(QWidget* parent = nullptr);

private:
    void attemptSubmit();

    QLabel* m_errorLabel = nullptr;
    QLineEdit* m_usernameEdit = nullptr;
    QLineEdit* m_passwordEdit = nullptr;
};

}  // namespace traceview
