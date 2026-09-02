#pragma once

#include <QPlainTextEdit>
#include <QStringDecoder>
#include <QTextCharFormat>
#include <QTimer>
#include <QVector>

namespace traceview {

struct ThemePalette;

// Dumb terminal display over the BTP v1 TERMINAL_IN/OUT channel (topico 19).
// There is no separate input line, no send button, no chat-style bubbles,
// no local echo and no locally-drawn prompt: the dongle's own ShellSerial is
// the line editor (bally_protocol/topicos/19_terminal_protocolado.txt
// RESULTADO, PASSO 1/2 -- editing stays server-side), so this widget's job
// is only (a) forward raw keystrokes/escape sequences while it has focus
// (Ctrl+Shift+C / Ctrl+Shift+V, and Ctrl+C with a selection, are the
// clipboard exceptions -- see keyPressEvent) and
// (b) render whatever comes back in appendData() using the same tiny
// line model ShellSerial's output already assumes: \r returns to column 0
// (without erasing), \b moves the cursor left one column, \n commits the
// current line to scrollback, and any other byte >=0x20 overwrites (or
// extends) the line at the current column.
// Nothing here talks to SerialManager/BtpSession directly; SerialWidgetBridge
// wires sendRequested()/appendData() to the BTP terminal channel.
//
// ANSI SGR: since TinyShell 1.2.0 (ShellStyle) the dongle MAY wrap output
// lines in a small, standard subset of colour escapes -- SGR (ESC [ ... m:
// 0/1/2/3/22/23/30/31/32/33/34/36/39/90) and erase-in-line (ESC [ K). appendData()
// parses that subset and renders it with per-run QTextCharFormat; the basic
// ANSI colours are mapped to ThemePalette tokens (success/warning/danger/
// accent/textDisabled/textSecondary/default), never literal RGB, so a theme switch
// re-tints the scrollback. Any other CSI sequence is swallowed silently --
// never shown as literal text. A dongle built without -DTINYSHELL_COLOR emits
// none of this and the stream is exactly what it was before.
class SerialTerminalWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit SerialTerminalWidget(QWidget* parent = nullptr);

public slots:
    // Entry point for a TERMINAL_OUT frame's payload bytes.
    void appendData(const QByteArray& data);

    // Wipes the log and the in-progress line state.
    void clearTerminal();

    // Layout mode owns the mouse for arranging cells, so it can hide this
    // operational cursor until the dashboard returns to Run mode.
    void setCursorBlinkEnabled(bool enabled);

signals:
    // Raw bytes to send as TERMINAL_IN, emitted per keystroke (never
    // buffered/batched here) -- backspace is 0x7f, Enter is '\r', Tab is
    // '\t', arrow keys become the same ESC [ A/B/C/D sequences a real
    // terminal emulator sends (ShellSerial parses those, see PASSO 3), and
    // Ctrl+<letter> becomes its ASCII control code (Ctrl+C -> 0x03 when
    // nothing is selected, etc.). Ctrl+Shift+V also comes through here, as
    // the clipboard text typed in one go.
    void sendRequested(const QByteArray& data);

    // Ctrl+Left / Ctrl+Right while this terminal has focus. Emitted instead
    // of forwarding anything to the dongle (the bare arrow keys still go out
    // as ESC [ D / ESC [ C) -- SerialMonitorWidget connects these to cycling
    // its sibling per-device tabs.
    void previousTabRequested();
    void nextTabRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    static constexpr int kCursorBlinkIntervalMs = 600;

    // The active graphic rendition, carried across appendData() calls and
    // across committed lines exactly as a real terminal carries SGR state.
    // `role` indexes the ANSI-colour -> ThemePalette-token table in the .cpp
    // (0 == default foreground); the flags are bold / faint / italic.
    struct Pen {
        quint8 role = 0;
        bool bold = false;
        bool faint = false;
        bool italic = false;
        bool operator==(const Pen& o) const {
            return role == o.role && bold == o.bold && faint == o.faint && italic == o.italic;
        }
        bool operator!=(const Pen& o) const { return !(*this == o); }
    };
    enum class EscState { Normal, Esc, Csi };

    void putChar(QChar c);
    void commitLine();
    void renderCurrentLineAndCursor();
    void insertLineRuns(QTextCursor& cursor, const QString& line, const QVector<Pen>& pens);
    QTextCharFormat charFormatForPen(const Pen& pen) const;
    void applySgr(const QString& params);
    void eraseInLine(int mode);
    void retintScrollback();
    QRect terminalCursorRect() const;
    void updateCursorOverlay();
    void resetCursorBlink();
    void toggleCursorBlink();
    void applyPreferences();

    QString m_currentLine;
    QVector<Pen> m_linePens;  // one per QChar of m_currentLine (short entries -> default Pen)
    int m_cursorCol = 0;
    Pen m_pen;
    EscState m_escState = EscState::Normal;
    QString m_csiParams;
    QStringDecoder m_utf8Decoder{QStringConverter::Utf8};
    QTimer m_cursorBlinkTimer;
    QWidget* m_cursorOverlay = nullptr;
    bool m_cursorBlinkEnabled = true;
    bool m_cursorVisible = false;
    bool m_autoScroll = true;
};

}  // namespace traceview
