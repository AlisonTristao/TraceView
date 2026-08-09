#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <QSerialPort>

#include "serialprotocol.h"

namespace traceview {

// Owns the single QSerialPort the whole app shares (see BACKEND_TODO.txt --
// there is one connection for the entire app, not one per widget). Purely a
// bytes-in/bytes-out transport: no frame parsing (SerialProtocol, a later
// module) and no routing to widgets happens here. Meant to be created once
// (MainWindow-owned) and handed to whatever needs to read/write the port --
// the Run ribbon tab for connect/disconnect UI, SerialTerminalWidget for raw
// passthrough, the future frame router for decoded data.
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
