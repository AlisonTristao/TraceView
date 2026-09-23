#pragma once

#include <QByteArray>
#include <QString>

#include "transport.h"

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

// What TransportType::Serial means regardless of how the platform reaches the
// port: SerialManager (QSerialPort, desktop) or AndroidUsbSerialTransport
// (Android USB Host API over JNI -- Qt has no QSerialPort backend there).
// DeviceConnection and SerialWidgetBridge only ever talk to this, so neither
// needs to know which one a given build compiled in.
//
// Both implementations must keep docs/PROTOCOL.md's "Serial line state: DTR"
// contract: DTR+RTS asserted on open, RTS lowered before DTR on close, and a
// configured 1200 baud folded up to 115200.
class SerialTransport : public Transport {
    Q_OBJECT

public:
    explicit SerialTransport(QObject* parent = nullptr) : Transport(parent) {}

    // Closes any existing connection first, then opens `portName` at
    // `baudRate` (8N1, no flow control). `portName` is whatever the
    // platform's port list offered: "COM3"/"ttyACM0" on desktop, a
    // "usb:VVVV:PPPP[:serial]" key on Android. Returns false (with
    // errorOccurred()) when it already knows the attempt failed; true means
    // either open now (connectionStateChanged(true) already emitted) or, on
    // Android, still pending a USB permission prompt -- the outcome then
    // arrives later as connectionStateChanged(true) or errorOccurred().
    virtual bool open(const QString& portName, qint32 baudRate) = 0;

    virtual QString portName() const = 0;
    virtual qint32 baudRate() const = 0;

    // Pushes every byte currently queued toward the OS/USB driver, waiting up
    // to timeoutMs. Used before dropping DTR so a final BTP SESSION_CLOSE is
    // not discarded with the port. Returns false when disconnected or when
    // the deadline expires.
    virtual bool drainWrites(int timeoutMs) = 0;

    // The terminator writeCommand() appends -- a global, port-level setting
    // (Run ribbon tab), not per-widget (docs/PROTOCOL.md "Outbound: control
    // commands"). Defaults to Lf, matching the inbound frame terminator.
    LineTerminator lineTerminator() const {
        return m_lineTerminator;
    }
    void setLineTerminator(LineTerminator terminator) {
        m_lineTerminator = terminator;
    }

    // Writes `command` followed by lineTerminator()'s bytes. For
    // control-widget sends only; SerialTerminalWidget's raw passthrough
    // should keep calling write() directly (no terminator).
    bool writeCommand(const QByteArray& command) {
        return write(command + lineTerminatorBytes(m_lineTerminator));
    }

    // The 1200-baud touch is esptool's shortcut into the ESP32-S3 ROM
    // bootloader (arduino-esp32 USBCDC::_onLineCoding reboots on
    // bit_rate == 1200), not a data rate -- see SerialManager::open(). Both
    // implementations substitute the result of this before configuring the
    // line.
    static qint32 safeBaudRate(qint32 requested) {
        return requested == 1200 ? 115200 : requested;
    }

private:
    LineTerminator m_lineTerminator = LineTerminator::Lf;
};

}  // namespace traceview
