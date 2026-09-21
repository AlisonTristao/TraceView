#pragma once

#include <QBluetoothUuid>
#include <QByteArray>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "transport.h"

class QLowEnergyController;

namespace traceview {

// Asynchronous BLE byte transport over the BTP v1 GATT service
// (BTP/docs/fragmentation-and-transports.md section 8,
// TAREFAS_TCP_BLE_ANDROID.txt T04/T28-T30). Like TcpTransport, this owns no
// protocol state: BtpSession (constructed with Framing::CobsStream, the same
// choice deviceconnection.cpp's toBtpSessionAxes() makes for TCP -- see its
// own comment for why) does the COBS framing and frame reassembly on top of
// whatever raw bytes arrive here, because GATT notifications carry no
// message boundary of their own any more than a TCP socket's read() does;
// a single characteristic's notifications arrive in order over one ATT
// bearer, so forwarding each notification's payload as it arrives (see
// onCharacteristicChanged()) is already a correctly-ordered byte stream,
// with nothing else to reassemble at this layer.
//
// What this class DOES own, and TcpTransport does not need to, is fitting
// that byte stream through a link whose maximum single write (the
// negotiated ATT MTU minus 3 octets of protocol overhead) is far smaller
// than one COBS-encoded BTP frame and is not known until after connecting:
// write() queues whole logical writes (one `bytesToWrite` payload from
// BtpSession) and drains each as a sequence of MTU-sized characteristic
// writes, one in flight at a time -- the RX characteristic is "write with
// response" by contract (T04), so the next chunk is only sent once the
// previous one's write is acknowledged (onCharacteristicWritten()).
//
// connectionStateChanged(true) is deliberately NOT emitted when the
// controller itself connects -- only once the BTP service is found, RX/TX
// resolved and TX notifications enabled (T28's acceptance criterion: "HELLO
// só começa quando RX/TX estão utilizáveis"). BtpBackend sends HELLO the
// moment a DirectBtp transport reports connected (see deviceconnection.cpp),
// so firing early would race GATT service discovery.
class BleTransport : public Transport {
    Q_OBJECT

public:
    explicit BleTransport(QObject* parent = nullptr);
    ~BleTransport() override;

    // Starts an asynchronous connection to the peripheral at `address` (a
    // platform BLE address -- see Device::bleAddress's own comment; parsed
    // as a MAC first, then as a UUID for backends that identify peripherals
    // that way). Returns false for an empty address. Any existing controller
    // is torn down first, so calling this again (a new address, or a retry
    // of the same one) always starts a clean attempt.
    bool open(const QString& address);
    void close() override;

    bool isConnected() const override;
    bool write(const QByteArray& data) override;

    void setConnectTimeoutForTesting(int timeoutMs) {
        m_connectTimeoutMs = qMax(1, timeoutMs);
    }

    QString address() const {
        return m_address;
    }
    int pendingFrameCount() const {
        return m_pendingFrames.size();
    }

private slots:
    void onControllerConnected();
    void onControllerDisconnected();
    void onControllerError();
    void onDiscoveryFinished();
    void onServiceStateChanged(QLowEnergyService::ServiceState newState);
    void onServiceError();
    void onCharacteristicChanged(const QLowEnergyCharacteristic& characteristic,
                                 const QByteArray& value);
    void onCharacteristicWritten(const QLowEnergyCharacteristic& characteristic,
                                 const QByteArray& value);
    void onDescriptorWritten(const QLowEnergyDescriptor& descriptor, const QByteArray& value);
    void onConnectTimeout();

private:
    void teardown(bool disconnectController);
    void failConnection(const QString& reason);
    int mtuPayloadSize() const;
    void drainNext();

    // BTP v1 BLE service/characteristic UUIDs (BTP/docs/fragmentation-and-
    // transports.md section 8, TAREFAS_TCP_BLE_ANDROID.txt T04's note).
    static QBluetoothUuid rxCharacteristicUuid();
    static QBluetoothUuid txCharacteristicUuid();

    // Queue depth 8 (T05's note: "Profundidade de fila: BLE 8 frames"), one
    // whole logical write (a complete COBS-wrapped BTP frame from
    // BtpSession) per entry -- MTU-sized fragmentation happens below this,
    // in drainNext().
    static constexpr int kMaxPendingFrames = 8;

    QLowEnergyController* m_controller = nullptr;
    QLowEnergyService* m_service = nullptr;
    QLowEnergyCharacteristic m_rxCharacteristic;
    QLowEnergyCharacteristic m_txCharacteristic;
    // The exact descriptor writeDescriptor() was called with -- compared by
    // value (QLowEnergyDescriptor::operator==) in onDescriptorWritten(),
    // since QLowEnergyDescriptor/QLowEnergyCharacteristic's own handle()
    // accessors are private in this Qt version and cannot be read back to
    // identify "is this the one write I'm waiting for" any other way.
    QLowEnergyDescriptor m_txNotificationDescriptor;
    QString m_address;
    bool m_connected = false;
    // Set by close(); tells onControllerDisconnected()/onServiceError() not
    // to report an error for a drop this class itself caused.
    bool m_closing = false;
    int m_connectTimeoutMs = 10000;  // GATT discovery is slower than a TCP
                                     // handshake; TcpTransport's own 5000ms
                                     // default would be too tight here.
    QTimer m_connectTimer;

    QQueue<QByteArray> m_pendingFrames;
    QByteArray m_currentFrame;
    qsizetype m_currentOffset = 0;
    bool m_writeInFlight = false;
};

}  // namespace traceview
