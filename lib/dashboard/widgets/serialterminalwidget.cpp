#include "serialterminalwidget.h"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

namespace traceview {

SerialTerminalWidget::SerialTerminalWidget(QWidget* parent) : QPlainTextEdit(parent) {
    // Read-only stops QPlainTextEdit's own keyPressEvent from editing the
    // document; every keystroke is handled below instead (forwarded, never
    // applied locally), so this widget is never "empty text input" in the
    // accessibility/QLineEdit sense. The visible prompt/echo comes only from
    // appendData(), fed by the dongle's own TERMINAL_OUT (PASSO 4/6: no
    // locally-drawn prompt, no double echo).
    setReadOnly(true);
    setUndoRedoEnabled(false);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

    // The local QTextCursor is only an implementation detail used to render
    // the line. Hide Qt's editable-text caret and paint a cursor at the
    // dongle-reported terminal position below, otherwise clicking/selecting
    // scrollback could make the visible caret disagree with the terminal.
    setCursorWidth(0);

    m_cursorBlinkTimer.setInterval(500);
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, &SerialTerminalWidget::toggleCursorBlink);
    connect(this, &SerialTerminalWidget::sendRequested, this,
            &SerialTerminalWidget::resetCursorBlink);
}

void SerialTerminalWidget::appendData(const QByteArray& data) {
    // Stateful UTF-8 decode (QStringDecoder keeps a partial-sequence tail
    // across calls) so a multibyte character split across two TERMINAL_OUT
    // frames -- e.g. an accented pt-br shell message truncated at
    // kOutboundPayloadCap -- decodes correctly instead of producing mojibake
    // at the boundary.
    const QString text = m_utf8Decoder(data);

    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == u'\r') {
            m_cursorCol = 0;
        } else if (code == u'\n') {
            commitLine();
        } else if (code == u'\b') {
            if (m_cursorCol > 0) {
                --m_cursorCol;
            }
        } else if (code < 0x20) {
            // Other control bytes: ShellSerial's own output never emits
            // them (see ShellSerial.cpp), so there is nothing meaningful to
            // render here; drop rather than show a mojibake glyph.
            continue;
        } else {
            putChar(ch);
        }
    }

    renderCurrentLineAndCursor();
    resetCursorBlink();
    QScrollBar* scrollBar = verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
}

void SerialTerminalWidget::clearTerminal() {
    clear();
    m_currentLine.clear();
    m_cursorCol = 0;
    resetCursorBlink();
}

void SerialTerminalWidget::keyPressEvent(QKeyEvent* event) {
    // Ctrl+<letter> => its ASCII control code (Ctrl+C -> 0x03, Ctrl+D ->
    // 0x04, Ctrl+R -> 0x12 for ShellSerial's reverse-search, ...), matching
    // how a real TTY driver translates them. Keyboard copy (Ctrl+C) is
    // intentionally not available here -- 0x03 always means "send SIGINT
    // byte to the dongle"; use the right-click context menu to copy
    // selected text (PASSO 7).
    if (event->modifiers() == Qt::ControlModifier && event->key() >= Qt::Key_A &&
        event->key() <= Qt::Key_Z) {
        emit sendRequested(QByteArray(1, static_cast<char>(event->key() - Qt::Key_A + 1)));
        return;
    }

    // Bare arrow keys are terminal input (history recall / cursor movement
    // inside ShellSerial on the dongle), translated to the same ESC [
    // A/B/C/D bytes a real terminal emulator sends. With a modifier held
    // (Shift/Ctrl, the usual keyboard text-selection combos) they fall
    // through to QPlainTextEdit's default handling instead, so
    // shift-selecting the read-only scrollback still works (PASSO 7).
    if (event->modifiers() == Qt::NoModifier) {
        switch (event->key()) {
            case Qt::Key_Up:
                emit sendRequested(QByteArrayLiteral("\x1b[A"));
                return;
            case Qt::Key_Down:
                emit sendRequested(QByteArrayLiteral("\x1b[B"));
                return;
            case Qt::Key_Right:
                emit sendRequested(QByteArrayLiteral("\x1b[C"));
                return;
            case Qt::Key_Left:
                emit sendRequested(QByteArrayLiteral("\x1b[D"));
                return;
            default:
                break;
        }
    }

    switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit sendRequested(QByteArray(1, '\r'));
            return;
        case Qt::Key_Backspace:
            emit sendRequested(QByteArray(1, static_cast<char>(0x7f)));
            return;
        case Qt::Key_Tab:
            emit sendRequested(QByteArray(1, '\t'));
            return;
        default:
            break;
    }

    const QString text = event->text();
    if (!text.isEmpty() && text.at(0).isPrint()) {
        emit sendRequested(text.toUtf8());
        return;
    }

    // Anything else (Home/End/PageUp/PageDown, mouse-driven selection +
    // right-click copy, wheel scroll) falls back to the read-only default
    // so the terminal still scrolls/selects like a normal text view.
    QPlainTextEdit::keyPressEvent(event);
}

void SerialTerminalWidget::focusInEvent(QFocusEvent* event) {
    QPlainTextEdit::focusInEvent(event);
    resetCursorBlink();
}

void SerialTerminalWidget::focusOutEvent(QFocusEvent* event) {
    QPlainTextEdit::focusOutEvent(event);
    m_cursorBlinkTimer.stop();
    m_cursorVisible = false;
    viewport()->update();
}

void SerialTerminalWidget::paintEvent(QPaintEvent* event) {
    QPlainTextEdit::paintEvent(event);

    if (!hasFocus() || !m_cursorVisible) {
        return;
    }

    const QRect cursor = terminalCursorRect();
    QPainter painter(viewport());
    painter.fillRect(cursor.x(), cursor.y(), 2, cursor.height(), palette().color(QPalette::Text));
}

void SerialTerminalWidget::putChar(QChar c) {
    if (m_cursorCol < m_currentLine.size()) {
        m_currentLine[m_cursorCol] = c;
    } else {
        m_currentLine.append(c);
    }
    ++m_cursorCol;
}

void SerialTerminalWidget::commitLine() {
    // renderCurrentLineAndCursor() only runs once, at the end of a whole
    // appendData() call -- if this same call already built up m_currentLine
    // from characters processed earlier (e.g. "info\r\n" in one chunk), the
    // document itself hasn't been synced with them yet. Flush first, then
    // append the newline, or that text is silently lost.
    QTextCursor cursor(document()->lastBlock());
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(m_currentLine);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText(QStringLiteral("\n"));
    setTextCursor(cursor);
    m_currentLine.clear();
    m_cursorCol = 0;
}

void SerialTerminalWidget::renderCurrentLineAndCursor() {
    // Selects the whole logical block (paragraph), not QTextCursor::
    // LineUnderCursor's *visual* line -- a long redrawn command line can
    // word-wrap into several visual lines at narrow widget widths, and
    // LineUnderCursor would only touch the first of them.
    QTextCursor lineCursor(document()->lastBlock());
    lineCursor.movePosition(QTextCursor::StartOfBlock);
    lineCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    lineCursor.insertText(m_currentLine);

    QTextCursor caret(document()->lastBlock());
    caret.movePosition(QTextCursor::StartOfBlock);
    const int column = qBound(0, m_cursorCol, m_currentLine.size());
    caret.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
    setTextCursor(caret);
}

QRect SerialTerminalWidget::terminalCursorRect() const {
    QTextCursor caret(document()->lastBlock());
    caret.movePosition(QTextCursor::StartOfBlock);
    const int column = qBound(0, m_cursorCol, m_currentLine.size());
    caret.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
    return cursorRect(caret);
}

void SerialTerminalWidget::resetCursorBlink() {
    m_cursorVisible = hasFocus();
    if (m_cursorVisible) {
        m_cursorBlinkTimer.start();
    } else {
        m_cursorBlinkTimer.stop();
    }
    viewport()->update();
}

void SerialTerminalWidget::toggleCursorBlink() {
    if (!hasFocus()) {
        m_cursorBlinkTimer.stop();
        m_cursorVisible = false;
        return;
    }

    m_cursorVisible = !m_cursorVisible;
    viewport()->update(terminalCursorRect());
}

}  // namespace traceview
