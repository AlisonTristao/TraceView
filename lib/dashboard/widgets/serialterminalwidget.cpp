#include "serialterminalwidget.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

namespace traceview {

namespace {

class TerminalCursorOverlay final : public QWidget {
public:
    explicit TerminalCursorOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));
    }
};

}  // namespace

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

    m_cursorOverlay = new TerminalCursorOverlay(viewport());
    m_cursorOverlay->setObjectName(QStringLiteral("terminalCursorOverlay"));
    m_cursorOverlay->hide();

    m_cursorBlinkTimer.setInterval(kCursorBlinkIntervalMs);
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, &SerialTerminalWidget::toggleCursorBlink);
    connect(this, &SerialTerminalWidget::sendRequested, this,
            &SerialTerminalWidget::resetCursorBlink);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &SerialTerminalWidget::updateCursorOverlay);

    // A dashboard cell, rather than this child widget, can own keyboard
    // focus. The terminal's remote cursor remains useful in that state, so
    // its visibility must not depend on QWidget::hasFocus().
    m_cursorVisible = true;
    m_cursorBlinkTimer.start();
    updateCursorOverlay();
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
    QScrollBar* scrollBar = verticalScrollBar();
    scrollBar->setValue(scrollBar->maximum());
    // Terminal output may arrive continuously (for example, while the
    // dongle forwards logs). It must not continually restart the blink
    // cycle, or the cursor would remain visibly on forever.
    updateCursorOverlay();
}

void SerialTerminalWidget::clearTerminal() {
    clear();
    m_currentLine.clear();
    m_cursorCol = 0;
    resetCursorBlink();
}

void SerialTerminalWidget::setCursorBlinkEnabled(bool enabled) {
    if (m_cursorBlinkEnabled == enabled) {
        return;
    }

    m_cursorBlinkEnabled = enabled;
    if (enabled) {
        resetCursorBlink();
        return;
    }

    m_cursorBlinkTimer.stop();
    m_cursorVisible = false;
    updateCursorOverlay();
}

void SerialTerminalWidget::keyPressEvent(QKeyEvent* event) {
    // Ctrl+Left / Ctrl+Right cycle SerialMonitorWidget's sibling per-device
    // tabs rather than reaching the dongle -- ShellSerial only ever sees the
    // *bare* arrow keys (handled further down as history/cursor input), and
    // Ctrl+arrow had no terminal meaning here before.
    if (event->modifiers() == Qt::ControlModifier) {
        if (event->key() == Qt::Key_Left) {
            emit previousTabRequested();
            return;
        }
        if (event->key() == Qt::Key_Right) {
            emit nextTabRequested();
            return;
        }
    }

    // Ctrl+Shift+C copies the selection unconditionally; Ctrl+Shift+V sends
    // the clipboard as if it were typed. These are the terminal-emulator
    // clipboard chords (Windows Terminal, GNOME Terminal, the VS Code
    // terminal), kept off the bare Ctrl+C/Ctrl+V so those stay free for the
    // control bytes below.
    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_C) {
            copy();
            return;
        }
        if (event->key() == Qt::Key_V) {
            const QString clip = QGuiApplication::clipboard()->text();
            if (!clip.isEmpty()) {
                emit sendRequested(clip.toUtf8());
            }
            return;
        }
    }

    // Ctrl+C with a selection copies (matching every terminal emulator);
    // with nothing selected it falls through to the Ctrl+<letter> block
    // below and still sends 0x03, so "interrupt the running command" stays
    // on the bare chord.
    if (event->modifiers() == Qt::ControlModifier && event->key() == Qt::Key_C &&
        textCursor().hasSelection()) {
        copy();
        return;
    }

    // Ctrl+<letter> => its ASCII control code (Ctrl+C -> 0x03, Ctrl+D ->
    // 0x04, Ctrl+R -> 0x12 for ShellSerial's reverse-search, ...), matching
    // how a real TTY driver translates them. 0x03 always means "send SIGINT
    // byte to the dongle" here; copy selected text with Ctrl+Shift+C or the
    // right-click context menu (PASSO 7).
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
}

void SerialTerminalWidget::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    updateCursorOverlay();
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

void SerialTerminalWidget::updateCursorOverlay() {
    const QRect cursor = terminalCursorRect();
    m_cursorOverlay->setGeometry(cursor.x(), cursor.y(), 2, cursor.height());

    QPalette overlayPalette = m_cursorOverlay->palette();
    overlayPalette.setColor(QPalette::Window, palette().color(QPalette::Text));
    m_cursorOverlay->setPalette(overlayPalette);
    m_cursorOverlay->setVisible(m_cursorBlinkEnabled && m_cursorVisible);
    m_cursorOverlay->raise();
    m_cursorOverlay->update();
}

void SerialTerminalWidget::resetCursorBlink() {
    if (!m_cursorBlinkEnabled) {
        m_cursorVisible = false;
        updateCursorOverlay();
        return;
    }

    m_cursorVisible = true;
    m_cursorBlinkTimer.start();
    updateCursorOverlay();
}

void SerialTerminalWidget::toggleCursorBlink() {
    m_cursorVisible = !m_cursorVisible;
    updateCursorOverlay();
}

}  // namespace traceview
