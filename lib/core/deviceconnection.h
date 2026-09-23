#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtGlobal>

#include "devices/device.h"

class QTimer;

namespace traceview {

class Backend;
class SerialTransport;
class UsbHidManager;
class HubTransport;
class TcpTransport;
#ifdef TRACEVIEW_ENABLE_BLE
class BleTransport;
#endif
class Transport;

enum class ConnectionPhase {
    Disconnected,
    Connecting,
    PreparingTransport,
    NegotiatingBtp,
    Ready,
};

// Owns one device's real, independent connection: a Transport (raw bytes --
// concretely a SerialManager or a UsbHidManager, chosen once at construction
// by `transportType`) plus a Backend (protocol decode/encode) -- extracted
// from what MainWindow used to build exactly once for the whole app, now
// built once per Device instead (see the multi-device connection refactor
// plan). `commType` picks which concrete Backend gets constructed; only
// CommType::Btp exists today (devices/device.h), so this always builds a
// BtpBackend, but it's the switch point for a future protocol.
// `transportType` similarly picks the concrete Transport -- see
// TransportType (devices/device.h) -- and is threaded into BtpBackend too,
// since BTP's own wire framing differs per transport profile (Serial/COBS
// vs UsbHid, see BtpSession).
//
// Reconnection is ambient rather than a one-shot action: connectTo()
// records intent ("this device should be online at this target") separately
// from whatever the transport's isConnected() currently reports, and a
// timer keeps retrying open() while the two disagree -- covers both the
// first connection attempt for a freshly configured device and silently
// recovering from an unplug/replug. disconnectFrom() (or clearing the
// target via connectTo()) is the only thing that turns the intent back off;
// a transport error/drop does not.
//
// What the rest of the app sees (connectionStateChanged) is tracked
// separately again, in m_reportedConnected, and the retry timer keeps
// reconciling it against the transport even after a successful connect: a
// transition the phase gate dropped (or one that raced a re-entrant
// connectTo()/connectVia() from MainWindow::onDeviceUpdated) must not leave
// a device painted offline while its link is actually up, or vice versa.
class DeviceConnection : public QObject {
    Q_OBJECT

    // Allows the transport regression fixture to simulate a physical hub link.
    friend class TestHubTransport;

public:
    explicit DeviceConnection(CommType commType,
                              TransportType transportType = TransportType::Serial,
                              QObject* parent = nullptr);
    ~DeviceConnection() override;

    // Non-null only when transportType == TransportType::Serial; nullptr
    // otherwise. Concretely a SerialManager on desktop or an
    // AndroidUsbSerialTransport on Android (see core/serialtransport.h).
    // Callers that need serial-only extras (writeCommand(),
    // lineTerminator()) must check for null first -- see
    // SerialWidgetBridge::wireWidget() for the pattern.
    SerialTransport* serialTransport() const {
        return m_serialTransport;
    }
    // Non-null only when transportType == TransportType::UsbHid; nullptr
    // otherwise.
    UsbHidManager* usbHidManager() const {
        return m_usbHidManager;
    }
    Backend* backend() const {
        return m_backend;
    }

    TransportType transportType() const {
        return m_transportType;
    }

    bool isConnected() const;
    ConnectionPhase connectionPhase() const {
        return m_connectionPhase;
    }
    // Current intent: true from connectTo() (with a non-empty target) until
    // disconnectFrom() or a connectTo() with an empty target -- stays true
    // across a transport drop/retry, unlike isConnected(). What the
    // status-dot click toggle (DeviceCard::connectToggleRequested) reads to
    // decide whether to connect or disconnect.
    bool wantsConnection() const {
        return m_shouldBeConnected;
    }
    bool isAvailable() const {
        return m_transportAvailable;
    }

    // Updates the connection target and marks intent "online" (unless
    // `target` is empty, which instead means "not configured" -- clears
    // intent and closes without ever attempting a connection). `target` is
    // a COM port name for TransportType::Serial or a hidapi device path for
    // TransportType::UsbHid (Device::portName/usbPath respectively --
    // MainWindow picks the right one before calling this); `baudRate` is
    // ignored for UsbHid. Safe to call again with different values while
    // already connected/retrying: a changed target or baud closes the
    // current connection (if any) and retries against the new target
    // immediately.
    void connectTo(const QString& target, qint32 baudRate);
    // The HubChannel counterpart of connectTo(): this device's target is the
    // parent device that carries it plus the source_id of the robot behind
    // that parent, which has no honest spelling as a (target, baudRate) pair.
    // No-op on any other transport. A null parent or a zero peer means "not
    // configured" and clears the intent, exactly as an empty target does for
    // connectTo().
    // `selfSourceId` is this child's own stable identity on the wire (see
    // hubChannelSourceId() in devices/device.h); `peerSourceId` is the robot
    // it addresses. Both are needed because a child is an endpoint, not just a
    // reader: it originates commands, terminal input and manifest requests,
    // and the hub routes the downstream direction by the child's own id.
    // `endpointKey` is the derived channel-B key (deriveChannelKey() applied
    // to Device::peerPassword by whoever calls this, since traceview_devices
    // can't depend on traceview_protocol) -- empty means "hub, but no key
    // configured yet", which every sealed send/receive on this connection
    // then refuses rather than treat as channel A's "in the clear".
    void connectVia(DeviceConnection* parentConnection, quint32 selfSourceId, quint32 peerSourceId,
                    const QByteArray& endpointKey);
    // The TCP counterpart of connectTo(): a host plus a numeric port has no
    // honest spelling as the (target, baudRate) pair connectTo() shares
    // between Serial and UsbHid, so -- for the same reason connectVia() is
    // separate -- this is its own call. No-op if transportType is not
    // TransportType::Tcp. An empty host or a zero port means "not
    // configured", exactly as an empty target does for connectTo().
    //
    // `endpointKey` is the SAME derived channel-B key connectVia() takes
    // (deriveChannelKey() applied to Device::peerPassword by the caller --
    // one password per device now covers hub, TCP and, later, BLE; see
    // BtpBackend::setDirectEndpointKey()'s own comment for why this is a
    // distinct method from setHubEndpoint()/connectVia() rather than a
    // reuse). Empty means "not configured yet", same convention as
    // connectVia(): every sealed reply from the robot is dropped rather
    // than forwarded unauthenticated, but this connection still comes up
    // and accepts the robot's unsealed traffic (a direct session is not a
    // hub child -- see setDirectEndpointKey()).
    void connectToTcp(const QString& host, quint16 port, const QByteArray& endpointKey);
    // The BLE counterpart of connectToTcp(): `address` is a platform BLE
    // address (Device::bleAddress -- a discovery hint, not identity; see
    // its own comment), and `endpointKey` is the SAME derived channel-B key
    // connectToTcp()/connectVia() take (Device::peerPassword covers hub, TCP
    // and BLE alike -- see BtpBackend::setDirectEndpointKey()). No-op if
    // transportType is not TransportType::Ble, and a no-op built entirely
    // without TRACEVIEW_ENABLE_BLE (no BleTransport exists in that
    // configuration at all -- see the constructor's Ble case). Empty address
    // means "not configured", same convention as connectToTcp()'s empty
    // host.
    void connectToBle(const QString& address, const QByteArray& endpointKey);
    // Marks intent "offline" and closes. Stops the retry timer -- unlike a
    // transport drop, this does not come back on its own.
    void disconnectFrom();

    // Platform/lifecycle boundary. Availability does not erase the user's
    // connection intent: suspension closes the current session and pauses
    // retries; making the environment available starts a fresh session.
    void setAvailable(bool available);
    void suspend() {
        setAvailable(false);
    }
    void resume() {
        setAvailable(true);
    }

    // No-op unless transportType == TransportType::Serial -- USB HID has no
    // console/raw-text channel for a line terminator to apply to (see
    // Device::lineTerminator's own comment).
    void setLineTerminator(int terminator);

signals:
    // Mirrors Transport::connectionStateChanged so callers don't have to
    // reach through serialTransport()/usbHidManager() themselves.
    void connectionStateChanged(bool connected);
    // Rich asynchronous phase for UI/platform consumers. The boolean signal
    // above remains for existing dashboard consumers.
    void connectionPhaseChanged(traceview::ConnectionPhase phase);
    // Emitted when the transport refuses bytes produced by the backend.
    void writeRejected(const QString& reason);
    void availabilityChanged(bool available);
    // Mirrors Backend::deviceIdentified so callers don't have to reach
    // through backend() themselves.
    void deviceIdentified(const QString& btpVersion, const QString& btpId);
    // Mirrors Backend::deviceInfoReported (the device's MANIFEST_DATA
    // source_info block, BTP's docs/commands.md section 3.12).
    void deviceInfoReported(const QVector<traceview::DeviceInfoRecord>& info);
    // Mirrors Transport::errorOccurred (e.g. QSerialPort::open() failing with
    // PermissionError on Linux when the user isn't in the dialout/uucp group)
    // so a failed connect attempt is not silently swallowed -- previously
    // nothing forwarded this past the transport, so clicking Connect against
    // an unopenable port did nothing visible at all.
    void errorOccurred(const QString& message);

private:
    void setConnectionPhase(ConnectionPhase phase);
    // The single place connectionStateChanged is emitted from: deduplicated
    // against m_reportedConnected, and moves the phase along with it
    // (connected -> NegotiatingBtp, or Ready for a hub child; disconnected ->
    // Disconnected).
    void reportConnected(bool connected);
    // A hub child has no attempt of its own -- its state is a pure function
    // of its parent's (HubTransport::isConnected()) and the intent. Brings
    // the reported state and phase in line with that, idempotently.
    void syncHubState();
    void attemptReconnect();
    // Intentional close path. Unlike session-recovery recycling, this first
    // asks an established serial BTP session to close and drains that final
    // frame before lowering DTR/RTS.
    void closeTransportGracefully();

    TransportType m_transportType;
    Transport* m_transport = nullptr;
    SerialTransport* m_serialTransport = nullptr;
    UsbHidManager* m_usbHidManager = nullptr;
    HubTransport* m_hubTransport = nullptr;
    TcpTransport* m_tcpTransport = nullptr;
#ifdef TRACEVIEW_ENABLE_BLE
    BleTransport* m_bleTransport = nullptr;
#endif
    Backend* m_backend = nullptr;
    QTimer* m_retryTimer;
    QString m_target;
    qint32 m_baudRate = 0;
    QString m_tcpHost;
    quint16 m_tcpPort = 0;
    QString m_bleAddress;
    bool m_shouldBeConnected = false;
    bool m_transportAvailable = true;
    bool m_attemptInProgress = false;
    // Last value emitted through connectionStateChanged -- see
    // reportConnected().
    bool m_reportedConnected = false;
    // What connectVia() last configured, so a repeat call with the same
    // arguments (MainWindow::reattachHubChildren() runs on every device
    // update, including every live connected/identity change) is a no-op
    // instead of knocking an established child back to Connecting.
    QPointer<DeviceConnection> m_hubParent;
    quint32 m_hubSelfSourceId = 0;
    QByteArray m_hubEndpointKey;
    ConnectionPhase m_connectionPhase = ConnectionPhase::Disconnected;
};

}  // namespace traceview
