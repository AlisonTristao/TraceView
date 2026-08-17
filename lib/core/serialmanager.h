#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <QSerialPort>

namespace traceview {

// Terminator appended to outbound control-widget commands (docs/PROTOCOL.md
// "Outbound: control commands") -- a single global setting tied to the port
// connection (Run ribbon tab), not per-widget. Does not apply to
// SerialTerminalWidget's raw per-keystroke passthrough, which stays
// unterminated. Unrelated to the BTP framing added in topico 14 (see
// protocol/btpsession.h): this is purely a transport-level convenience for
// the still-raw-text control-widget outbound path (COMMAND-channel
// migration is future work, see docs/PROTOCOL.md).
enum class LineTerminator { None, Lf, Cr, CrLf };

// Byte sequence for `terminator` -- empty for None.
QByteArray lineTerminatorBytes(LineTerminator terminator);

// Owns the single QSerialPort the whole app shares (see BACKEND_TODO.txt --
// there is one connection for the entire app, not one per widget). Purely a
// bytes-in/bytes-out transport: no frame parsing and no routing to widgets
// happens here -- see protocol/btpsession.h (BtpSession) for BTP framing and
// core/serialwidgetbridge.h for the raw control/terminal wiring. Meant to be
// created once (MainWindow-owned) and handed to whatever needs to read/write
// the port -- the Run ribbon tab for connect/disconnect UI,
// SerialTerminalWidget for raw passthrough, BtpSession for decoded data.
class SerialManager : public QObject {
    Q_OBJECT

public:
    explicit SerialManager(QObject* parent = nullptr);

    // Port names currently reported by the OS (QSerialPortInfo), refreshed
    // on every call -- callers needing a live list (e.g. a combo box) should
    // call this again rather than caching it.
    QStringList availablePorts() const;

    // Closes any existing connection first, then opens `portName` at
    // `baudRate` (8N1, no flow control). Returns false and emits
    // errorOccurred() on failure; emits connectionStateChanged(true) on
    // success.
    bool open(const QString& portName, qint32 baudRate);
    // No-op if not currently open. Emits connectionStateChanged(false).
    void close();

    bool isConnected() const;
    QString portName() const;
    qint32 baudRate() const;

    // Writes raw bytes to the port. Returns false without effect if the
    // port isn't open -- callers that can't guarantee an open connection
    // (control widgets, the terminal) should treat a false return as "went
    // nowhere," not an error to surface.
    bool write(const QByteArray& data);

    // The terminator writeCommand() appends -- a global, port-level setting
    // (Run ribbon tab), not per-widget (docs/PROTOCOL.md "Outbound: control
    // commands"). Defaults to Lf, matching the inbound frame terminator.
    LineTerminator lineTerminator() const { return m_lineTerminator; }
    void setLineTerminator(LineTerminator terminator) { m_lineTerminator = terminator; }

    // Writes `command` followed by lineTerminator()'s bytes. For
    // control-widget-triggered commands (PushButton/ToggleSwitch/Slider,
    // Task 9) -- SerialTerminalWidget's raw keystroke passthrough (Task 10)
    // uses write() directly and is untouched by this setting.
    bool writeCommand(const QByteArray& command);

signals:
    void connectionStateChanged(bool connected);
    // Raw bytes as they arrive off the wire -- no line assembly, no frame
    // decoding.
    void dataReceived(const QByteArray& data);
    // Transport-level errors only (port open failure, device unplugged
    // mid-session, etc.) -- never for malformed protocol data, which isn't
    // this module's concern.
    void errorOccurred(const QString& message);

private:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

    QSerialPort* m_port = nullptr;
    LineTerminator m_lineTerminator = LineTerminator::Lf;
};

} // namespace traceview
