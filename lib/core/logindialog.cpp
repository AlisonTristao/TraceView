#include "core/logindialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputMethod>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/usermodemanager.h"

namespace traceview {

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Enter Developer Mode"));
    setMinimumWidth(320);

    auto* titleLabel = new QLabel(tr("Enter your developer username and password."), this);
    titleLabel->setWordWrap(true);

    m_usernameEdit = new QLineEdit(this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto* form = new QFormLayout();
    form->addRow(tr("Username:"), m_usernameEdit);
    form->addRow(tr("Password:"), m_passwordEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet("color: #d9534f;");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Log In"));
    connect(buttons, &QDialogButtonBox::accepted, this, &LoginDialog::attemptSubmit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addLayout(form);
    layout->addWidget(m_errorLabel);
    layout->addWidget(buttons);

    connect(m_passwordEdit, &QLineEdit::returnPressed, this, &LoginDialog::attemptSubmit);
}

void LoginDialog::attemptSubmit() {
    const QString username = m_usernameEdit->text().trimmed();
    const QString password = m_passwordEdit->text();

    if (!UserModeManager::instance().login(username, password)) {
        m_errorLabel->setText(tr("Incorrect username or password."));
        m_errorLabel->setVisible(true);
        // Clear the rejected password so a retry isn't appended to it (easy
        // to miss behind the echo mask, especially on a phone) and hand focus
        // back to the field -- tapping "Log In" moved it to the button, which
        // on Android also dismissed the soft keyboard.
        m_passwordEdit->clear();
        m_passwordEdit->setFocus(Qt::OtherFocusReason);
        QGuiApplication::inputMethod()->show();
        return;
    }
    accept();
}

}  // namespace traceview
