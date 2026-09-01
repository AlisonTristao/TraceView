#include "serialterminalwidget.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include "traceview/theme.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {

// SGR "role" values stored in SerialTerminalWidget::Pen::role and, for
// committed scrollback, as a QTextCharFormat property so a later theme switch
// can re-resolve the colour (retintScrollback).
enum : quint8 {
    RoleDefault = 0,
    RoleSuccess,
    RoleWarning,
    RoleDanger,
    RoleMuted,
    RoleAccent,
    RoleSecondary,
};

// Custom QTextCharFormat properties: the pen role + its faint flag, enough for
// retintScrollback() to recompute the foreground against a new ThemePalette.
constexpr int kPenRoleProperty = QTextFormat::UserProperty;
constexpr int kPenFaintProperty = QTextFormat::UserProperty + 1;

QColor colorForRole(quint8 role, bool faint, const ThemePalette& p) {
    switch (role) {
        case RoleSuccess:   return p.success;
        case RoleWarning:   return p.warning;
        case RoleDanger:    return p.danger;
        case RoleMuted:     return p.textDisabled;
        case RoleAccent:    return p.accent;
        case RoleSecondary: return p.textSecondary;
        case RoleDefault:
        default:            return faint ? p.textSecondary : QColor();  // invalid -> inherit
    }
}

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

    // ANSI colours map to palette tokens, not literal RGB, so a mid-session
    // theme switch has to re-resolve every coloured run already in scrollback.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) {
                retintScrollback();
                renderCurrentLineAndCursor();
                updateCursorOverlay();
            });

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

        // ESC [ ... <final> -- the parser state carries across appendData()
        // calls, same as m_utf8Decoder, so a CSI split across two TERMINAL_OUT
        // frames still resolves.
        if (m_escState == EscState::Esc) {
            m_escState = (code == u'[') ? EscState::Csi : EscState::Normal;
            if (m_escState == EscState::Csi) {
                m_csiParams.clear();
            }
            continue;
        }
        if (m_escState == EscState::Csi) {
            if (code >= 0x20 && code <= 0x3f) {  // parameter + intermediate bytes
                if (m_csiParams.size() < 48) {
                    m_csiParams.append(ch);
                }
                continue;
            }
            // final byte (0x40..0x7e), or a stray control char aborting the seq
            if (code == u'm') {
                applySgr(m_csiParams);
            } else if (code == u'K') {
                eraseInLine(m_csiParams.isEmpty() ? 0 : m_csiParams.toInt());
            }
            // every other final (cursor moves, ED, ...) is swallowed silently
            m_escState = EscState::Normal;
            continue;
        }

        if (code == 0x1b) {
            m_escState = EscState::Esc;
        } else if (code == u'\r') {
            m_cursorCol = 0;
        } else if (code == u'\n') {
            commitLine();
        } else if (code == u'\b') {
            if (m_cursorCol > 0) {
                --m_cursorCol;
            }
        } else if (code < 0x20) {
            // Other control bytes have no meaning in this line model; drop
            // rather than show a mojibake glyph.
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
    m_linePens.clear();
    m_cursorCol = 0;
    m_pen = Pen{};
    m_escState = EscState::Normal;
    m_csiParams.clear();
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
        if (m_cursorCol < m_linePens.size()) {
            m_linePens[m_cursorCol] = m_pen;
        }
    } else {
        while (m_currentLine.size() < m_cursorCol) {
            m_currentLine.append(u' ');
            m_linePens.append(Pen{});
        }
        m_currentLine.append(c);
        m_linePens.append(m_pen);
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
    insertLineRuns(cursor, m_currentLine, m_linePens);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText(QStringLiteral("\n"));
    setTextCursor(cursor);
    m_currentLine.clear();
    m_linePens.clear();
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
    insertLineRuns(lineCursor, m_currentLine, m_linePens);

    QTextCursor caret(document()->lastBlock());
    caret.movePosition(QTextCursor::StartOfBlock);
    const int column = qBound(0, m_cursorCol, m_currentLine.size());
    caret.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, column);
    setTextCursor(caret);
}

// One block's worth of text, split into maximal same-Pen runs. A default Pen
// keeps the exact "plain insertText, clean cursor format" path the widget used
// before ShellStyle existed; only a coloured run carries a QTextCharFormat.
void SerialTerminalWidget::insertLineRuns(QTextCursor& cursor, const QString& line,
                                          const QVector<Pen>& pens) {
    int i = 0;
    while (i < line.size()) {
        const Pen pen = (i < pens.size()) ? pens.at(i) : Pen{};
        int j = i + 1;
        while (j < line.size() && (j < pens.size() ? pens.at(j) : Pen{}) == pen) {
            ++j;
        }
        const QString run = line.mid(i, j - i);
        if (pen == Pen{}) {
            cursor.setCharFormat(QTextCharFormat());
            cursor.insertText(run);
        } else {
            cursor.insertText(run, charFormatForPen(pen));
        }
        i = j;
    }
    cursor.setCharFormat(QTextCharFormat());  // don't leave a coloured pen on the cursor
}

QTextCharFormat SerialTerminalWidget::charFormatForPen(const Pen& pen) const {
    QTextCharFormat fmt;
    if (pen == Pen{}) {
        return fmt;
    }

    const QColor color = colorForRole(pen.role, pen.faint, ThemeManager::instance().currentTheme());
    if (color.isValid()) {
        fmt.setForeground(color);
    }
    if (pen.bold) {
        fmt.setFontWeight(QFont::Bold);
    }
    if (pen.italic) {
        fmt.setFontItalic(true);
    }
    fmt.setProperty(kPenRoleProperty, int(pen.role));
    fmt.setProperty(kPenFaintProperty, pen.faint);
    return fmt;
}

// ESC [ <params> m -- only the subset ShellStyle emits; unknown codes leave
// the pen untouched rather than guessing.
void SerialTerminalWidget::applySgr(const QString& params) {
    const QStringList tokens = params.isEmpty() ? QStringList{QStringLiteral("0")}
                                                : params.split(u';');
    for (const QString& token : tokens) {
        const int code = token.isEmpty() ? 0 : token.toInt();
        switch (code) {
            case 0:  m_pen = Pen{}; break;
            case 1:  m_pen.bold = true; break;
            case 2:  m_pen.faint = true; break;
            case 3:  m_pen.italic = true; break;
            case 22: m_pen.bold = false; m_pen.faint = false; break;
            case 23: m_pen.italic = false; break;
            case 30: m_pen.role = RoleDefault; break;
            case 31: m_pen.role = RoleDanger; break;
            case 32: m_pen.role = RoleSuccess; break;
            case 33: m_pen.role = RoleWarning; break;
            case 34: m_pen.role = RoleAccent; break;
            case 36: m_pen.role = RoleAccent; break;
            case 39: m_pen.role = RoleDefault; break;
            case 90: m_pen.role = RoleMuted; break;
            default: break;  // 35/37, 9x, 38;5;n, background codes, ... ignored
        }
    }
}

// ESC [ K (mode 0: cursor->end, 1: start->cursor, 2: whole line). The dongle's
// console path emits a bare "\r\033[K" to wipe the prompt line before printing.
void SerialTerminalWidget::eraseInLine(int mode) {
    if (mode == 2) {
        m_currentLine.clear();
        m_linePens.clear();
        m_cursorCol = 0;
        return;
    }
    if (mode == 1) {
        const int upto = qMin(m_cursorCol, m_currentLine.size());
        for (int i = 0; i < upto; ++i) {
            m_currentLine[i] = u' ';
            if (i < m_linePens.size()) {
                m_linePens[i] = Pen{};
            }
        }
        return;
    }
    if (m_cursorCol < m_currentLine.size()) {
        m_currentLine.truncate(m_cursorCol);
    }
    if (m_cursorCol < m_linePens.size()) {
        m_linePens.resize(m_cursorCol);
    }
}

// A theme switch changes what each ANSI colour resolves to; walk the committed
// scrollback and recolour every run that carries a pen-role property. Collect
// the ranges first -- mergeCharFormat() re-flows fragments as it goes.
void SerialTerminalWidget::retintScrollback() {
    struct Range {
        int position;
        int length;
        quint8 role;
        bool faint;
    };
    QVector<Range> ranges;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) {
                continue;
            }
            const QTextCharFormat fmt = fragment.charFormat();
            if (!fmt.hasProperty(kPenRoleProperty)) {
                continue;
            }
            ranges.push_back({fragment.position(), fragment.length(),
                              static_cast<quint8>(fmt.intProperty(kPenRoleProperty)),
                              fmt.boolProperty(kPenFaintProperty)});
        }
    }
    if (ranges.isEmpty()) {
        return;
    }

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (const Range& range : ranges) {
        const QColor color = colorForRole(range.role, range.faint, palette);
        QTextCharFormat fmt;
        fmt.setForeground(color.isValid() ? color : this->palette().color(QPalette::Text));
        cursor.setPosition(range.position);
        cursor.setPosition(range.position + range.length, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(fmt);
    }
    cursor.endEditBlock();
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
