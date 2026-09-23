#pragma once

#include <QLineEdit>
#include <QMessageBox>
#include <QString>

class QDialog;
class QWidget;

namespace traceview {

// Single entry point for every dialog the app opens, so each one can be
// presented the way the current platform actually handles it.
//
// On desktop this is a thin pass-through: exec()/show() and the message
// helpers behave exactly like QDialog::exec()/show() and QMessageBox's
// static functions. On Android there is no window manager -- a QDialog
// becomes a separate undecorated top-level window with an arbitrary
// size/position and no way to close it but its own buttons. So while
// embedding is on (MainWindow turns it on whenever compact chrome is
// active, i.e. on Android and in a desktop Small/Medium preview), the
// dialog is instead reparented INTO the registered host widget, on an
// overlay covering it:
//
//   Style::Card -- centered card over a dimmed backdrop, sized to the
//                  dialog's own content and scrolling past ~90% of the
//                  screen. For short dialogs: About, Donate, login,
//                  confirmations, messages, text prompts.
//   Style::Page -- covers the whole host, with a top bar (back arrow +
//                  title) and scrolling content. For big dialogs: device
//                  config, shortcuts, user management, block scripts,
//                  notification history.
//
// Android's Back key rejects the topmost embedded dialog in both styles.
namespace DialogPresenter {

enum class Style { Card, Page };

// The widget embedded dialogs overlay -- MainWindow's m_appShell, so a
// desktop Small/Medium preview shows them inside the device frame, same
// as a real phone would.
void setHost(QWidget* host);
void setEmbedded(bool embedded);
bool embedded();

// Drop-in replacement for dialog.exec().
int exec(QDialog& dialog, Style style);

// Drop-in replacement for dialog->show()/raise()/activateWindow() on a
// non-modal dialog. An embedded dialog is released back to its original
// parent (hidden) once it finishes, so the same instance can be shown
// again later.
void show(QDialog* dialog, Style style);

// Same contracts as the QMessageBox/QInputDialog static functions of the
// same name. Closing an embedded one with Back counts as the escape
// button (No/Cancel/Close), like closing a QMessageBox's window does.
QMessageBox::StandardButton question(
    QWidget* parent, const QString& title, const QString& text,
    QMessageBox::StandardButtons buttons = QMessageBox::Yes | QMessageBox::No,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);
void information(QWidget* parent, const QString& title, const QString& text);
void warning(QWidget* parent, const QString& title, const QString& text);
QString getText(QWidget* parent, const QString& title, const QString& label,
                QLineEdit::EchoMode echo = QLineEdit::Normal, const QString& text = {},
                bool* ok = nullptr);

// Two-button confirmation with custom button texts; true when the accept
// button was chosen.
bool confirm(QWidget* parent, const QString& title, const QString& text,
             const QString& acceptText, const QString& rejectText);

}  // namespace DialogPresenter

}  // namespace traceview
