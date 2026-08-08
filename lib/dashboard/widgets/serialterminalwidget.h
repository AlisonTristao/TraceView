#pragma once

#include <QPlainTextEdit>

namespace traceview {

// Serial I/O terminal in the style of pyserial's miniterm / PlatformIO's
// Serial Monitor: there is no separate input line, no send button, no
// chat-style bubbles. The terminal surface itself is the input — while it
// has focus, every key pressed is transmitted immediately, one character
// at a time, exactly like a real TTY. Nothing here talks to QSerialPort;
// wiring sendRequested()/appendData() to a real connection is a later,
// separate step.
class SerialTerminalWidget : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit SerialTerminalWidget(QWidget* parent = nullptr);

public slots:
    // Entry point for bytes arriving from the serial connection.
    void appendData(const QByteArray& data);

    // Wipes the log and draws a fresh prompt.
    void clearTerminal();

signals:
    // One emission per keystroke, raw bytes, sent immediately — never
    // buffered until Enter.
    void sendRequested(const QByteArray& data);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void startInputLine();
    void moveCursorToDocEnd();

    int m_inputStart = 0;
};

} // namespace traceview
