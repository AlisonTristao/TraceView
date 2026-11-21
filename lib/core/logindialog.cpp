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
#include "theme/dialogchrome.h"
#include "traceview/theme.h"
#include "traceview/thememanager.h"

namespace traceview {

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Enter Developer Mode"));
    setMinimumWidth(320);

    auto* header = new DialogHeader(tr("Enter Developer Mode"),
                                    tr("Enter your developer username and password."), this);

    m_usernameEdit = new QLineEdit(this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto* form = new QFormLayout();
    form->addRow(tr("Username:"), m_usernameEdit);
    form->addRow(tr("Password:"), m_passwordEdit);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    // A state color, not chrome: the palette's danger token, re-applied when
    // the theme changes. (The app stylesheet sets every widget's text color,
    // so a QPalette change alone would be overridden.)
    const auto applyErrorColor = [this](const ThemePalette& palette) {
        m_errorLabel->setStyleSheet(QStringLiteral("color: %1;").arg(palette.danger.name()));
    };
    applyErrorColor(ThemeManager::instance().currentTheme());
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, applyErrorColor);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Log In"));
    connect(buttons, &QDialogButtonBox::accepted, this, &LoginDialog::attemptSubmit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout* layout = DialogChrome::setupRootLayout(this);
    layout->addWidget(header);
    layout->addLayout(form);
    layout->addWidget(m_errorLabel);
    layout->addLayout(DialogChrome::footer(buttons));

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
