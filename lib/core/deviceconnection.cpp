#include "deviceconnection.h"

#include <QTimer>
#include <btp/codec.hpp>

#include "backend/backend.h"
#include "core/applog.h"
#include "hubtransport.h"
#include "preferences/appsettings.h"
#include "protocol/btpbackend.h"
#include "tcptransport.h"
#ifdef TRACEVIEW_ENABLE_SERIAL
#ifdef Q_OS_ANDROID
#include "androidusbserialtransport.h"
#else
#include "serialmanager.h"
#endif
#endif
#ifdef TRACEVIEW_ENABLE_USB_HID
#include "usbhidmanager.h"
#endif
#ifdef TRACEVIEW_ENABLE_BLE
#include "bletransport.h"
#endif

namespace traceview {

namespace {
// How often a device with an unmet "should be online" intent retries
// open() -- covers both a freshly configured device's first attempt and
// recovering from an unplug/replug without any user action.
constexpr int kCloseWriteDrainTimeoutMs = 250;

// BtpBackend/BtpSession speak two independent axes -- link framing and
// encode profile (btp::TransportLimits, the BTP library's own type; see
// btpsession.h for why they are separate); Device speaks TransportType
// (devices/device.h, dependency-free of btp::codec). This is the one place
// that needs to know both -- traceview_devices still doesn't depend on
// btp::codec.
struct BtpSessionAxes {
    BtpSession::Framing framing;
    btp::TransportLimits encodeProfile;
};

// One entry per TransportType, and the pair is written out per transport
// rather than derived from the profile precisely because HubChannel breaks
// the correspondence that held while there were only two transports.
//
// HubChannel encodes under the ESP-NOW profile even though its own link is a
// serial cable, and that is the point: a child device's frames are going to
// end up on a radio, so they are built to fit a radio datagram from the very
// start. The hub then relays them without re-fragmenting -- it never has to,
// because they already fit. Encoding under the Serial profile instead would
// let a 4056-octet payload through, which the hub would have to cut into
// twenty ESP-NOW fragments with no retransmission behind them.
//
// PreFramed for the same reason UsbHid is: the layer below hands over exactly
// one frame's octets at a time, already unwrapped from the cable's COBS.
BtpSessionAxes toBtpSessionAxes(TransportType type) {
    switch (type) {
        case TransportType::Serial:
            return {BtpSession::Framing::CobsStream, btp::kSerialTransport};
        case TransportType::UsbHid:
            return {BtpSession::Framing::PreFramed, btp::kUsbHidTransport};
        case TransportType::HubChannel:
            return {BtpSession::Framing::PreFramed, btp::kEspNowTransport};
        case TransportType::Tcp:
            // btp::kTcpTransport already exists in the BTP library (added
            // alongside the TCP contract -- see TAREFAS_TCP_BLE_ANDROID.txt
            // T03), but this repo's FetchContent still pins tag v2.45.0
            // (root CMakeLists.txt), which predates it. Standing in with the
            // Serial profile until that pin moves is deliberate: it
            // under-provisions TCP's intended frame budget (4056 vs 8152
            // octets) rather than referencing a symbol this build doesn't
            // have. Revisit once the BTP dependency is bumped.
            return {BtpSession::Framing::CobsStream, btp::kSerialTransport};
        case TransportType::Ble:
            // Same reasoning and same placeholder as Tcp just above: rule
            // 19 of TAREFAS_TCP_BLE_ANDROID.txt mandates COBS on BLE too
            // (fragmentation-and-transports.md section 8), and
            // BleTransport (T28-T30) forwards raw GATT notification bytes
            // exactly as TcpTransport forwards raw socket bytes -- neither
            // transport bounds a frame itself, so BtpSession's own
            // CobsStream decoder has to. btp::kBleTransport now exists for
            // real (T34, added to the BTP library alongside bally_OS's own
            // BLE work), but this repo still pins the v2.45.0 tag (root
            // CMakeLists.txt), which predates it -- same kSerialTransport
            // stand-in TCP uses meanwhile, revisit together with Tcp's case
            // once the BTP dependency is bumped.
            return {BtpSession::Framing::CobsStream, btp::kSerialTransport};
    }
    return {BtpSession::Framing::CobsStream, btp::kSerialTransport};
}
}  // namespace

DeviceConnection::DeviceConnection(CommType commType, TransportType transportType, QObject* parent)
    : QObject(parent), m_transportType(transportType) {
    switch (transportType) {
        case TransportType::Serial:
#ifdef TRACEVIEW_ENABLE_SERIAL
#ifdef Q_OS_ANDROID
        {
            auto* usbSerial = new AndroidUsbSerialTransport(this);
            // A replug is the moment a retry can succeed -- take it now
            // rather than up to one reconnect interval later.
            // attemptReconnect() is already a no-op while connected or
            // mid-attempt.
            connect(usbSerial, &AndroidUsbSerialTransport::deviceAttached, this,
                    &DeviceConnection::attemptReconnect);
            m_serialTransport = usbSerial;
        }
#else
            m_serialTransport = new SerialManager(this);
#endif
            m_transport = m_serialTransport;
            break;
#else
            // Same as the Ble case below: without TRACEVIEW_ENABLE_SERIAL, no
            // SerialTransport implementation even exists to construct.
            // DeviceConfigDialog does not offer Serial as a selectable
            // transport in that configuration for exactly this reason.
            break;
#endif
        case TransportType::UsbHid:
#ifdef TRACEVIEW_ENABLE_USB_HID
            m_usbHidManager = new UsbHidManager(this);
            m_transport = m_usbHidManager;
            break;
#else
            // Same as Serial just above, for TRACEVIEW_ENABLE_USB_HID.
            break;
#endif
        case TransportType::HubChannel:
            // Built unconfigured (peer 0) and pointed at a robot later by
            // connectVia(), the same way the other two are built before
            // anyone knows which port or path they will open.
            m_hubTransport = new HubTransport(0, this);
            m_transport = m_hubTransport;
            break;
        case TransportType::Tcp:
            m_tcpTransport = new TcpTransport(this);
            // DeviceConnection's own m_retryTimer already retries every
            // transport uniformly (see attemptReconnect()) -- TcpTransport's
            // internal reconnect exists for its standalone/testing use and
            // must be off here. Left enabled, a drop would race two
            // independent retry loops: TcpTransport reopening the socket on
            // its own timer and emitting connectionStateChanged(true)
            // directly, without DeviceConnection ever having left
            // Disconnected for PreparingTransport first -- the phase-gated
            // lambda a few lines below would then discard that transition
            // silently (connected but m_connectionPhase never advances).
            m_tcpTransport->setReconnectEnabled(false);
            m_transport = m_tcpTransport;
            break;
        case TransportType::Ble:
#ifdef TRACEVIEW_ENABLE_BLE
            // Unlike TcpTransport, BleTransport has no internal reconnect
            // of its own to disable -- see its class comment: every attempt
            // is a clean rebuild, driven solely by DeviceConnection's own
            // m_retryTimer/attemptReconnect(), uniformly across transports.
            m_bleTransport = new BleTransport(this);
            m_transport = m_bleTransport;
            break;
#else
            // Without TRACEVIEW_ENABLE_BLE, no BleTransport type even
            // exists to construct. DeviceConfigDialog does not offer Ble as
            // a selectable transport in that configuration for exactly this
            // reason. m_transport stays null -- nothing calls
            // connectTo()/connectToBle()/attemptReconnect() for a Ble
            // device in this build, so the connect() calls just below see a
            // null sender and no-op (Qt warns, does not crash).
            break;
#endif
    }

    switch (commType) {
        case CommType::Btp: {
            const BtpSessionAxes axes = toBtpSessionAxes(transportType);
            const auto sessionStartMode =
                (transportType == TransportType::Tcp || transportType == TransportType::Ble)
                    ? BtpBackend::SessionStartMode::DirectBtp
                    : BtpBackend::SessionStartMode::Console;
            m_backend = new BtpBackend(axes.framing, axes.encodeProfile, sessionStartMode, this);
            break;
        }
    }

    // Same wiring MainWindow::MainWindow() used to do once for the whole
    // app -- see core/mainwindow.cpp before the multi-device refactor. All
    // four connections are against the common Transport base (transport.h)
    // now, so this is identical regardless of which concrete transport
    // `transportType` picked above.
    connect(m_transport, &Transport::dataReceived, m_backend, &Backend::feedBytes);
    connect(m_backend, &Backend::bytesToWrite, this, [this](const QByteArray& data) {
        if (!m_transport->write(data)) {
            const QString reason = tr("transport rejected %1 bytes").arg(data.size());
            emit writeRejected(reason);
            m_backend->onTransportWriteRejected(reason);
        }
    });
    connect(m_transport, &Transport::connectionStateChanged, m_backend,
            &Backend::onTransportConnectionChanged);
    connect(m_transport, &Transport::connectionStateChanged, this, [this](bool connected) {
        if (m_transportType == TransportType::HubChannel) {
            // A hub child makes no attempts of its own, so there is no
            // attempt for a stale signal to belong to: the phase gate below
            // does not apply. Gating it anyway is what left children red --
            // connectVia() sets Connecting and then attaches, and a parent
            // that is already up reports connected right there, in the one
            // phase the gate throws a `true` away in.
            syncHubState();
            return;
        }
        if (connected &&
            (!m_shouldBeConnected || m_connectionPhase != ConnectionPhase::PreparingTransport)) {
            // Not ours to act on now; attemptReconnect() reconciles it on the
            // next tick if the link really is up and wanted.
            return;
        }
        if (!connected && m_shouldBeConnected &&
            (m_connectionPhase == ConnectionPhase::Connecting ||
             m_connectionPhase == ConnectionPhase::PreparingTransport)) {
            // A late close from a superseded attempt must not tear down the
            // phase of the new attempt -- but the link it reported on is
            // gone, so the app must still stop showing it as connected.
            m_attemptInProgress = false;
            if (m_reportedConnected) {
                m_reportedConnected = false;
                emit connectionStateChanged(false);
            }
            return;
        }
        m_attemptInProgress = false;
        reportConnected(connected);
    });
    connect(m_transport, &Transport::errorOccurred, this, &DeviceConnection::errorOccurred);
    connect(m_backend, &Backend::deviceIdentified, this, &DeviceConnection::deviceIdentified);
        connect(m_backend, &Backend::deviceIdentified, this,
            [this](const QString&, const QString&) {
                if (m_shouldBeConnected && m_connectionPhase == ConnectionPhase::NegotiatingBtp) {
                    setConnectionPhase(ConnectionPhase::Ready);
                }
            });
    connect(m_backend, &Backend::deviceInfoReported, this, &DeviceConnection::deviceInfoReported);
    connect(m_transport, &Transport::errorOccurred, this, [this](const QString&) {
        if (!m_transport->isConnected() &&
            (m_connectionPhase == ConnectionPhase::PreparingTransport ||
             m_connectionPhase == ConnectionPhase::NegotiatingBtp ||
             m_connectionPhase == ConnectionPhase::Ready)) {
            m_attemptInProgress = false;
            reportConnected(false);
        }
    });
    // A dead session on a live transport: close it and let the retry timer
    // below reopen it, which restarts the handshake through
    // onTransportConnectionChanged(). Closing is what makes attemptReconnect()
    // eligible at all -- it returns early while the transport is still
    // connected, which is exactly the state a failed handshake leaves behind.
    //
    // Ready counts too, not only NegotiatingBtp: a session can also die after
    // it was established (the dongle rebooting back to BTP/1 CONSOLE, a
    // rejected write), and a port left open under a dead session is a device
    // -- and every hub child riding it -- that looks connected and never
    // talks again. Never for a hub child: closing it would only detach it
    // from a parent that is fine.
    connect(m_backend, &Backend::sessionRecoveryNeeded, this, [this] {
        if (m_transportType != TransportType::HubChannel && m_shouldBeConnected &&
            (m_connectionPhase == ConnectionPhase::NegotiatingBtp ||
             m_connectionPhase == ConnectionPhase::Ready) &&
            AppSettings::instance().autoReconnect()) {
            qCInfo(lcConnection) << "session lost on a live transport, recycling" << m_target;
            m_transport->close();
        }
    });

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(AppSettings::instance().reconnectIntervalSeconds() * 1000);
    connect(m_retryTimer, &QTimer::timeout, this, &DeviceConnection::attemptReconnect);
    connect(&AppSettings::instance(), &AppSettings::connectionPreferencesChanged, this, [this] {
        m_retryTimer->setInterval(AppSettings::instance().reconnectIntervalSeconds() * 1000);
        if (!AppSettings::instance().autoReconnect()) {
            m_retryTimer->stop();
        } else if (m_shouldBeConnected) {
            // Also while connected: the tick is what reconciles the reported
            // state (see attemptReconnect()).
            m_retryTimer->start();
        }
    });
}

DeviceConnection::~DeviceConnection() {
    m_shouldBeConnected = false;
    m_attemptInProgress = false;
    if (m_retryTimer) {
        m_retryTimer->stop();
    }
    closeTransportGracefully();
}

bool DeviceConnection::isConnected() const {
    return m_transport->isConnected();
}

void DeviceConnection::connectTo(const QString& target, qint32 baudRate) {
    const bool targetChanged = target != m_target || baudRate != m_baudRate;
    // Same target, already wanted, already up or on its way up: nothing to
    // do. MainWindow calls this on every deviceUpdated, which DevicesGrid
    // also emits for live state (connected, HELLO identity) -- from inside
    // this very connection's own signals. Falling through would knock the
    // phase back to Connecting mid-handshake, and in Connecting both the
    // Ready promotion and a later drop are ignored.
    if (!targetChanged && !target.isEmpty() && m_shouldBeConnected && m_transportAvailable &&
        (m_transport->isConnected() || m_attemptInProgress)) {
        if (AppSettings::instance().autoReconnect() && !m_retryTimer->isActive()) {
            m_retryTimer->start();
        }
        return;
    }
    m_target = target;
    m_baudRate = baudRate;
    m_shouldBeConnected = !target.isEmpty();
    m_attemptInProgress = false;

    if (!m_shouldBeConnected) {
        qCInfo(lcConnection) << "target cleared, intent now offline";
        m_retryTimer->stop();
        closeTransportGracefully();
        reportConnected(false);
        return;
    }
    if (!m_transportAvailable) {
        setConnectionPhase(ConnectionPhase::Disconnected);
        return;
    }
    setConnectionPhase(ConnectionPhase::Connecting);
    qCInfo(lcConnection) << "target set to" << target << "baud" << baudRate
                        << (targetChanged ? "(changed)" : "(unchanged)");

    if (targetChanged && m_transport->isConnected()) {
        closeTransportGracefully();
    }
    attemptReconnect();
    if (AppSettings::instance().autoReconnect()) {
        m_retryTimer->start();
    }
}

void DeviceConnection::connectToTcp(const QString& host, quint16 port,
                                    const QByteArray& endpointKey) {
    if (m_tcpTransport == nullptr) {
        return;
    }

    const QString trimmedHost = host.trimmed();
    const bool targetChanged = trimmedHost != m_tcpHost || port != m_tcpPort;
    m_tcpHost = trimmedHost;
    m_tcpPort = port;
    const bool wasWanted = m_shouldBeConnected;
    m_shouldBeConnected = !trimmedHost.isEmpty() && port != 0;

    // Told before the link can come up, same reasoning as connectVia()'s own
    // setHubEndpoint() call: a sealed reply from the robot could in principle
    // arrive as early as the first bytes after connect. Unlike connectVia(),
    // this never touches this backend's identity or its outbound sealing --
    // see BtpBackend::setDirectEndpointKey()'s own comment for why a direct
    // TCP session must not become a hub child.
    if (auto* btpBackend = qobject_cast<BtpBackend*>(m_backend)) {
        btpBackend->setDirectEndpointKey(endpointKey);
    }

    if (!m_shouldBeConnected) {
        qCInfo(lcConnection) << "TCP target cleared, intent now offline";
        m_retryTimer->stop();
        closeTransportGracefully();
        reportConnected(false);
        return;
    }
    // Same idempotency as connectTo() -- see its comment.
    if (!targetChanged && wasWanted && m_transportAvailable &&
        (m_transport->isConnected() || m_attemptInProgress)) {
        if (AppSettings::instance().autoReconnect() && !m_retryTimer->isActive()) {
            m_retryTimer->start();
        }
        return;
    }
    m_attemptInProgress = false;
    if (!m_transportAvailable) {
        setConnectionPhase(ConnectionPhase::Disconnected);
        return;
    }
    setConnectionPhase(ConnectionPhase::Connecting);
    qCInfo(lcConnection) << "TCP target set to" << m_tcpHost << m_tcpPort
                        << (targetChanged ? "(changed)" : "(unchanged)");

    if (targetChanged && m_transport->isConnected()) {
        closeTransportGracefully();
    }
    attemptReconnect();
    if (AppSettings::instance().autoReconnect()) {
        m_retryTimer->start();
    }
}

void DeviceConnection::connectToBle(const QString& address, const QByteArray& endpointKey) {
#ifdef TRACEVIEW_ENABLE_BLE
    if (m_bleTransport == nullptr) {
        return;
    }

    const QString trimmedAddress = address.trimmed();
    const bool targetChanged = trimmedAddress != m_bleAddress;
    m_bleAddress = trimmedAddress;
    const bool wasWanted = m_shouldBeConnected;
    m_shouldBeConnected = !trimmedAddress.isEmpty();

    // Same reasoning as connectToTcp()'s own call: told before the link can
    // come up, since a sealed reply from the robot could arrive as early as
    // the first notification after HELLO. A direct BLE session is not a hub
    // child either, for the same reason a direct TCP session isn't -- see
    // BtpBackend::setDirectEndpointKey()'s own comment.
    if (auto* btpBackend = qobject_cast<BtpBackend*>(m_backend)) {
        btpBackend->setDirectEndpointKey(endpointKey);
    }

    if (!m_shouldBeConnected) {
        qCInfo(lcConnection) << "BLE target cleared, intent now offline";
        m_retryTimer->stop();
        closeTransportGracefully();
        reportConnected(false);
        return;
    }
    // Same idempotency as connectTo() -- see its comment.
    if (!targetChanged && wasWanted && m_transportAvailable &&
        (m_transport->isConnected() || m_attemptInProgress)) {
        if (AppSettings::instance().autoReconnect() && !m_retryTimer->isActive()) {
            m_retryTimer->start();
        }
        return;
    }
    m_attemptInProgress = false;
    if (!m_transportAvailable) {
        setConnectionPhase(ConnectionPhase::Disconnected);
        return;
    }
    setConnectionPhase(ConnectionPhase::Connecting);
    qCInfo(lcConnection) << "BLE target set to" << m_bleAddress
                        << (targetChanged ? "(changed)" : "(unchanged)");

    if (targetChanged && m_transport->isConnected()) {
        closeTransportGracefully();
    }
    attemptReconnect();
    if (AppSettings::instance().autoReconnect()) {
        m_retryTimer->start();
    }
#else
    Q_UNUSED(address);
    Q_UNUSED(endpointKey);
#endif
}

void DeviceConnection::connectVia(DeviceConnection* parentConnection, quint32 selfSourceId,
                                  quint32 peerSourceId, const QByteArray& endpointKey) {
    // The HubChannel counterpart of connectTo(), and separate from it for the
    // reason transport.h gives for keeping open() off the Transport
    // interface: a hub channel's target is a parent device plus a source_id,
    // which has no honest spelling as the (port name, baud rate) pair the
    // other two share. Squeezing it into that shape would be a stringly-typed
    // muddle, so this is the specific call for the transport this connection
    // knows it built.
    if (m_hubTransport == nullptr) {
        return;
    }

    // Same ambient-intent model as connectTo(): "not configured" means no
    // parent or no peer, and anything else means this child should be online
    // whenever its parent is. A parent that drops does NOT clear the intent,
    // so the child comes back on its own when the cable does -- which is the
    // behavior a hub needs, since the parent going away is the common case
    // (unplugging the dongle) and not an instruction from the user.
    const bool wantConnection = parentConnection != nullptr && peerSourceId != 0;

    // MainWindow::reattachHubChildren() re-issues this for every child on
    // every device update -- including the parent's own connected/identity
    // changes, from inside the parent's signals. With nothing changed that
    // must be a no-op, not a reset of an established child.
    const bool unchanged = m_hubParent == parentConnection && m_hubSelfSourceId == selfSourceId &&
                           m_hubTransport->peerSourceId() == peerSourceId &&
                           m_hubEndpointKey == endpointKey &&
                           m_shouldBeConnected == wantConnection &&
                           m_hubTransport->isAttached() == wantConnection;
    if (unchanged) {
        syncHubState();
        return;
    }

    m_hubParent = parentConnection;
    m_hubSelfSourceId = selfSourceId;
    m_hubEndpointKey = endpointKey;
    // Set before attaching: attaching to a parent that is already up reports
    // connected synchronously, and that report is judged against the intent.
    m_shouldBeConnected = wantConnection;
    m_attemptInProgress = false;

    // Told before the link can come up, because coming up is what makes the
    // child ask its robot for a catalog -- and it has to know which robot by
    // then. It also fixes the child's own source_id, which BtpBackend latches
    // exactly once on its first connect: attaching first would let a parent
    // that is already up bring this backend up as a console session under a
    // random identity, for good.
    if (auto* btpBackend = qobject_cast<BtpBackend*>(m_backend)) {
        btpBackend->setHubEndpoint(selfSourceId, peerSourceId, endpointKey);
    }

    if (!wantConnection) {
        m_retryTimer->stop();
        m_hubTransport->setPeerSourceId(peerSourceId);
        closeTransportGracefully();
        reportConnected(false);
        return;
    }

    m_hubTransport->setPeerSourceId(peerSourceId);
    m_hubTransport->attachTo(parentConnection);
    syncHubState();
    if (AppSettings::instance().autoReconnect()) {
        m_retryTimer->start();
    }
}

void DeviceConnection::disconnectFrom() {
    qCInfo(lcConnection) << "disconnect requested for" << m_target;
    m_shouldBeConnected = false;
    m_attemptInProgress = false;
    m_retryTimer->stop();
    closeTransportGracefully();
    reportConnected(false);
}

void DeviceConnection::setAvailable(bool available) {
    if (m_transportAvailable == available) {
        return;
    }
    m_transportAvailable = available;
    emit availabilityChanged(available);
    m_attemptInProgress = false;

    if (!available) {
        m_retryTimer->stop();
        closeTransportGracefully();
        reportConnected(false);
        return;
    }

    if (m_shouldBeConnected && !m_transport->isConnected()) {
        setConnectionPhase(ConnectionPhase::Connecting);
        attemptReconnect();
        if (AppSettings::instance().autoReconnect()) {
            m_retryTimer->start();
        }
    }
}

void DeviceConnection::setConnectionPhase(ConnectionPhase phase) {
    if (m_connectionPhase == phase) {
        return;
    }
    m_connectionPhase = phase;
    emit connectionPhaseChanged(phase);
}

void DeviceConnection::reportConnected(bool connected) {
    if (m_reportedConnected != connected) {
        m_reportedConnected = connected;
        emit connectionStateChanged(connected);
        // A listener may have changed our state re-entrantly (a disconnect
        // from the UI, say); its outcome wins over this one's phase.
        if (m_reportedConnected != connected) {
            return;
        }
    }
    if (!connected) {
        // A listener of the emit above may already have started the next
        // attempt from inside it (MainWindow -> applyDeviceTarget() ->
        // connectTo*() -> attemptReconnect(): attempt in progress, phase
        // PreparingTransport). Overwriting that with Disconnected orphaned
        // the attempt: an attempt that then failed WITHOUT ever connecting
        // (an mDNS lookup for a robot still booting) was ignored by the
        // phase-gated error handler, m_attemptInProgress stayed true, and
        // attemptReconnect() refused every later retry -- forever.
        if (m_attemptInProgress && m_connectionPhase == ConnectionPhase::PreparingTransport) {
            return;
        }
        setConnectionPhase(ConnectionPhase::Disconnected);
        return;
    }
    if (m_connectionPhase != ConnectionPhase::NegotiatingBtp &&
        m_connectionPhase != ConnectionPhase::Ready) {
        setConnectionPhase(m_transportType == TransportType::HubChannel
                               ? ConnectionPhase::Ready
                               : ConnectionPhase::NegotiatingBtp);
    }
}

void DeviceConnection::syncHubState() {
    if (m_hubTransport == nullptr) {
        return;
    }
    if (m_shouldBeConnected && m_transportAvailable && m_hubTransport->isConnected()) {
        reportConnected(true);
        return;
    }
    if (m_reportedConnected) {
        reportConnected(false);
    }
    // Waiting on the parent is Connecting, not Disconnected: the intent is
    // still on and nothing but the parent stands in the way.
    setConnectionPhase(m_shouldBeConnected && m_transportAvailable ? ConnectionPhase::Connecting
                                                                   : ConnectionPhase::Disconnected);
}

void DeviceConnection::closeTransportGracefully() {
    if (m_hubTransport != nullptr) {
        // Always, not only while connected: close() is also what detaches a
        // child from its parent, and a child left attached under an "offline"
        // intent would still be handed the parent's link coming up.
        m_hubTransport->close();
        return;
    }
    if (m_transport == nullptr || !m_transport->isConnected()) {
        return;
    }

#ifdef TRACEVIEW_ENABLE_SERIAL
    if (m_serialTransport != nullptr) {
        if (auto* btpBackend = qobject_cast<BtpBackend*>(m_backend)) {
            if (btpBackend->requestSessionClose()) {
                // bytesToWrite -> SerialTransport::write is a direct connection
                // in this thread, so the frame is already queued here. Drain
                // it before close() lowers DTR and tears the native CDC down.
                m_serialTransport->drainWrites(kCloseWriteDrainTimeoutMs);
            }
        }
    }
#endif
    m_transport->close();
}

void DeviceConnection::setLineTerminator(int terminator) {
#ifdef TRACEVIEW_ENABLE_SERIAL
    if (m_serialTransport) {
        m_serialTransport->setLineTerminator(LineTerminator(terminator));
    }
#else
    Q_UNUSED(terminator);
#endif
}

void DeviceConnection::attemptReconnect() {
    if (!m_shouldBeConnected || !m_transportAvailable || m_reconnectPaused) {
        return;
    }
    if (m_hubTransport != nullptr) {
        // A hub child has no port to reopen: its link is its parent's, which
        // it already watches. What the tick does here is re-attach a child
        // that close() detached (suspend/resume) and reconcile.
        if (!m_hubTransport->isAttached() && m_hubParent != nullptr) {
            m_hubTransport->attachTo(m_hubParent);
        }
        syncHubState();
        return;
    }
    if (m_transport->isConnected()) {
        // The link is up; make sure the app has been told so. A `true` the
        // phase gate dropped (a 1200-baud warning arriving before a
        // successful open, an async open finishing late) would otherwise
        // leave the device painted offline over a working link for good --
        // nothing else would ever re-announce it.
        if (!m_reportedConnected) {
            qCInfo(lcConnection) << "link up but not reported, reconciling" << m_target;
            m_attemptInProgress = false;
            reportConnected(true);
        }
        return;
    }
    if (m_reportedConnected && !m_attemptInProgress) {
        // The mirror case: a drop that was never reported.
        qCInfo(lcConnection) << "link down but reported up, reconciling" << m_target;
        reportConnected(false);
    }
    if (m_attemptInProgress) {
        return;
    }
    QString attemptTarget = m_target;
    if (m_tcpTransport) {
        attemptTarget = QString("%1:%2").arg(m_tcpHost).arg(m_tcpPort);
#ifdef TRACEVIEW_ENABLE_BLE
    } else if (m_bleTransport) {
        attemptTarget = m_bleAddress;
#endif
    }
    qCInfo(lcConnection) << "attempting connect to" << attemptTarget;
    m_attemptInProgress = true;
    setConnectionPhase(ConnectionPhase::PreparingTransport);
    if (m_serialTransport) {
#ifdef TRACEVIEW_ENABLE_SERIAL
        m_serialTransport->open(m_target, m_baudRate);
#endif
    } else if (m_usbHidManager) {
#ifdef TRACEVIEW_ENABLE_USB_HID
        m_usbHidManager->open(m_target);
#endif
    } else if (m_tcpTransport) {
        m_tcpTransport->open(m_tcpHost, m_tcpPort);
#ifdef TRACEVIEW_ENABLE_BLE
    } else if (m_bleTransport) {
        m_bleTransport->open(m_bleAddress);
#endif
    }
}

}  // namespace traceview
