#pragma once

#include <QByteArray>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QStringList>

#include "serialtransport.h"

namespace traceview {

// Owns one QSerialPort. Purely a bytes-in/bytes-out transport: no frame
// parsing and no routing to widgets happens here -- see protocol/
// btpsession.h (BtpSession) for BTP framing and core/serialwidgetbridge.h
// for the raw control/terminal wiring. As of the multi-device connection
// refactor, one instance is owned per Device by core/deviceconnection.h
// (DeviceConnection) rather than a single MainWindow-owned instance shared
// by the whole app -- each device's port/baud/line-terminator is configured
// independently (Devices tab, DeviceConfigDialog), not from a single global
// Run ribbon bar.
class SerialManager : public SerialTransport {
    Q_OBJECT

public:
    explicit SerialManager(QObject* parent = nullptr);

    // Port names currently reported by the OS (QSerialPortInfo), refreshed
    // on every call -- callers needing a live list (e.g. a combo box) should
    // call this again rather than caching it.
    QStringList availablePorts() const;

    // See SerialTransport::open(). Always synchronous here: returns false
    // and emits errorOccurred() on failure; emits connectionStateChanged(true)
    // on success.
    bool open(const QString& portName, qint32 baudRate) override;
    // No-op if not currently open. Emits connectionStateChanged(false).
    void close() override;

    bool isConnected() const override;
    QString portName() const override;
    qint32 baudRate() const override;

    // Writes raw bytes to the port. Returns false without effect if the
    // port isn't open -- callers that can't guarantee an open connection
    // (control widgets, the terminal) should treat a false return as "went
    // nowhere," not an error to surface.
    bool write(const QByteArray& data) override;

    // Pushes every byte currently queued by QSerialPort toward the OS/USB
    // driver -- see SerialTransport::drainWrites().
    bool drainWrites(int timeoutMs) override;

private:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError error);

    QSerialPort* m_port = nullptr;
    bool m_closing = false;
};

}  // namespace traceview
