#include "serialterminalwidget.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextCursor>

namespace traceview {

namespace {
constexpr const char* kPrompt = "> ";
} // namespace

SerialTerminalWidget::SerialTerminalWidget(QWidget* parent) : QPlainTextEdit(parent) {
    // Read-only stops QPlainTextEdit's own keyPressEvent from editing the
    // document; every keystroke is handled below instead, so this widget
    // is never "empty text input" in the accessibility/QLineEdit sense.
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    startInputLine();
}

void SerialTerminalWidget::appendData(const QByteArray& data) {
    moveCursorToDocEnd();
    insertPlainText(QString::fromUtf8(data));
    m_inputStart = textCursor().position();
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void SerialTerminalWidget::clearTerminal() {
    clear();
    startInputLine();
}

void SerialTerminalWidget::keyPressEvent(QKeyEvent* event) {
    // Ctrl+<letter> maps to its ASCII control code, matching how a real
    // TTY driver translates them (Ctrl+C -> 0x03, Ctrl+D -> 0x04, ...).
    // Text selection/copy stays reachable via the right-click context menu.
    if (event->modifiers() == Qt::ControlModifier && event->key() >= Qt::Key_A &&
        event->key() <= Qt::Key_Z) {
        emit sendRequested(QByteArray(1, static_cast<char>(event->key() - Qt::Key_A + 1)));
        return;
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit sendRequested(QByteArray(1, '\r'));
        moveCursorToDocEnd();
        insertPlainText("\n");
        startInputLine();
        return;
    case Qt::Key_Backspace:
        emit sendRequested(QByteArray(1, static_cast<char>(0x7f)));
        moveCursorToDocEnd();
        if (textCursor().position() > m_inputStart) {
            QTextCursor cursor = textCursor();
            cursor.deletePreviousChar();
            setTextCursor(cursor);
        }
        return;
    default:
        break;
    }

    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint()) {
        emit sendRequested(text.toUtf8());
        moveCursorToDocEnd();
        insertPlainText(text);
        return;
    }

    // Anything else (arrow keys, Home/End, PageUp/Down, mouse-driven
    // selection + Ctrl+C copy) falls back to the read-only default so the
    // terminal still scrolls/selects like a normal text view.
    QPlainTextEdit::keyPressEvent(event);
}

void SerialTerminalWidget::startInputLine() {
    moveCursorToDocEnd();
    insertPlainText(kPrompt);
    m_inputStart = textCursor().position();
}

void SerialTerminalWidget::moveCursorToDocEnd() {
    QTextCursor cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    setTextCursor(cursor);
}

} // namespace traceview
