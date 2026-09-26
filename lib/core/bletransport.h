#pragma once

#include <QBluetoothUuid>
#include <QByteArray>
#include <QHash>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>
#include <QLowEnergyService>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "transport.h"

class QLowEnergyController;

namespace traceview {

class BleDiscoveryService;

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

    // Starts an asynchronous connection to `target` -- see Device::bleAddress's
    // own comment. A platform address (a MAC, or a UUID on backends that
    // identify peripherals that way -- isPlatformAddress()) is dialed
    // directly. Anything else is the robot's advertised NAME, the BLE
    // counterpart of a TCP hostname: a short scan finds the one BTP
    // peripheral advertising it (bleNameMatches()) and dials its address. Two
    // robots answering to the same name fail the attempt rather than picking
    // one -- connecting to the wrong robot, silently, is the one outcome a
    // name lookup must never have. Returns false for an empty target. The
    // connection itself starts once the Bluetooth permission is granted (see
    // blepermission.h); a denial is reported through errorOccurred(). Any
    // existing controller is torn down first, so calling this again (a new
    // target, or a retry of the same one) always starts a clean attempt --
    // and a name is looked up again on every attempt, so a robot whose
    // address changed is still found.
    bool open(const QString& target);

    // True when `target` parses as a MAC or a UUID, i.e. is dialed without a
    // scan. Everything else open() treats as a name.
    static bool isPlatformAddress(const QString& target);
    // Whether a peripheral advertising `advertised` is the one a user who
    // typed `wanted` means: trimmed, case-insensitive. An empty name never
    // matches -- a peripheral whose name has not arrived yet is not a match.
    static bool bleNameMatches(const QString& advertised, const QString& wanted);
    // The address `name` last resolved to in this run, or empty. A lookup
    // accepts that address even before its name arrives, and dials it
    // directly when the scan does not hear the robot at all.
    static QString cachedAddressForName(const QString& name);
    // Seeds that cache with a hint (an address the user once set by hand).
    // Never overrides what a lookup actually found.
    static void rememberAddressForName(const QString& name, const QString& address);
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
    void startController(const QString& address);
    void startNameLookup();
    void finishNameLookup();
    void stopNameLookup();
    void onNameLookupTimeout();
    static QHash<QString, QString>& nameCache();
    void teardown(bool disconnectController);
    void failConnection(const QString& reason);
    int mtuPayloadSize() const;
    void drainNext();

    // BTP v1 BLE service/characteristic UUIDs (BTP/docs/fragmentation-and-
    // transports.md section 8, TAREFAS_TCP_BLE_ANDROID.txt T04's note).
    static QBluetoothUuid rxCharacteristicUuid();
    static QBluetoothUuid txCharacteristicUuid();

    // One whole COBS-wrapped BTP frame from BtpSession per entry;
    // drainNext() packs as many of them as fit into each MTU-sized write.
    // Was 8 (T05's "BLE 8 frames"), sized for one write per frame: at one
    // write-with-response round trip per frame, a burst of terminal
    // keystrokes plus the session's own control traffic overflowed it and
    // write() started refusing frames ("transport rejected 60 bytes").
    static constexpr int kMaxPendingFrames = 64;

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
    // Bumped by every teardown() (so by open() and close() too): a
    // permission answer for an older attempt compares unequal and is dropped.
    quint64 m_attempt = 0;
    bool m_connected = false;
    // Set by close(); tells onControllerDisconnected()/onServiceError() not
    // to report an error for a drop this class itself caused.
    bool m_closing = false;
    int m_connectTimeoutMs = 10000;  // GATT discovery is slower than a TCP
                                     // handshake; TcpTransport's own 5000ms
                                     // default would be too tight here.
    QTimer m_connectTimer;

    // --- Name lookup (open() with a name, not an address) ---------------
    // How long a scan may look for the name before the attempt fails. Kept
    // well under DeviceLinkCycler::kAttemptMs, so a robot that is simply not
    // there moves the cycler on instead of eating its whole window.
    static constexpr int kNameLookupTimeoutMs = 6000;
    // After the first peripheral answering to the name, how long the scan
    // keeps listening for a SECOND one before dialing the first. The price
    // of refusing an ambiguous name instead of guessing.
    static constexpr int kNameSettleMs = 1000;
    BleDiscoveryService* m_lookup = nullptr;
    QTimer m_lookupTimer;
    QTimer m_settleTimer;
    // Distinct addresses seen advertising the name during this lookup.
    QStringList m_lookupMatches;
    // Every BTP peripheral this lookup heard ("name address"), for the log
    // and the error when the name is not among them.
    QStringList m_lookupSeen;
    // cachedAddressForName(m_address) when the lookup started.
    QString m_cachedAddress;

    QQueue<QByteArray> m_pendingFrames;
    QByteArray m_currentFrame;
    qsizetype m_currentOffset = 0;
    bool m_writeInFlight = false;
};

}  // namespace traceview
