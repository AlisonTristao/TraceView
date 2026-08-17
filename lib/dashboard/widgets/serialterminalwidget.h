#pragma once

#include <QPlainTextEdit>
#include <QStringDecoder>

namespace traceview {

// Dumb terminal display over the BTP v1 TERMINAL_IN/OUT channel (topico 19).
// There is no separate input line, no send button, no chat-style bubbles,
// no local echo and no locally-drawn prompt: the dongle's own ShellSerial is
// the line editor (bally_protocol/topicos/19_terminal_protocolado.txt
// RESULTADO, PASSO 1/2 -- editing stays server-side), so this widget's job
// is only (a) forward raw keystrokes/escape sequences while it has focus
// and (b) render whatever comes back in appendData() using the same tiny
// line model ShellSerial's output already assumes: \r returns to column 0
// (without erasing), \b moves the cursor left one column, \n commits the
// current line to scrollback, and any other byte >=0x20 overwrites (or
// extends) the line at the current column -- no ANSI CSI sequences needed
// because ShellSerial never emits any (see ShellSerial::redrawInput()).
// Nothing here talks to SerialManager/BtpSession directly; SerialWidgetBridge
// wires sendRequested()/appendData() to the BTP terminal channel.
class SerialTerminalWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit SerialTerminalWidget(QWidget* parent = nullptr);

public slots:
    // Entry point for a TERMINAL_OUT frame's payload bytes.
    void appendData(const QByteArray& data);

    // Wipes the log and the in-progress line state.
    void clearTerminal();

signals:
    // Raw bytes to send as TERMINAL_IN, emitted per keystroke (never
    // buffered/batched here) -- backspace is 0x7f, Enter is '\r', Tab is
    // '\t', arrow keys become the same ESC [ A/B/C/D sequences a real
    // terminal emulator sends (ShellSerial parses those, see PASSO 3), and
    // Ctrl+<letter> becomes its ASCII control code (Ctrl+C -> 0x03, etc.).
    void sendRequested(const QByteArray& data);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void putChar(QChar c);
    void commitLine();
    void renderCurrentLineAndCursor();

    QString m_currentLine;
    int m_cursorCol = 0;
    QStringDecoder m_utf8Decoder{QStringConverter::Utf8};
};

} // namespace traceview
