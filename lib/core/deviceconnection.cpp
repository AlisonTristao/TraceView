#include "deviceconnection.h"

#include <QTimer>
#include <btp/codec.hpp>

#include "backend/backend.h"
#include "core/applog.h"
#include "hubtransport.h"
#include "preferences/appsettings.h"
#include "protocol/btpbackend.h"
#include "serialmanager.h"
#include "tcptransport.h"
#include "usbhidmanager.h"

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
            // No BleTransport exists yet (TAREFAS_TCP_BLE_ANDROID.txt
            // T26+); nothing constructs a BtpBackend with this transport
            // today (see the ctor below). Present only for switch
            // exhaustiveness, using the USB HID profile as the closer
            // analog -- both hand over one already-framed block at a time.
            return {BtpSession::Framing::PreFramed, btp::kUsbHidTransport};
    }
    return {BtpSession::Framing::CobsStream, btp::kSerialTransport};
}
}  // namespace

DeviceConnection::DeviceConnection(CommType commType, TransportType transportType, QObject* parent)
    : QObject(parent), m_transportType(transportType) {
    switch (transportType) {
        case TransportType::Serial:
            m_serialManager = new SerialManager(this);
            m_transport = m_serialManager;
            break;
        case TransportType::UsbHid:
            m_usbHidManager = new UsbHidManager(this);
            m_transport = m_usbHidManager;
            break;
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
            // No BleTransport exists yet (TAREFAS_TCP_BLE_ANDROID.txt
            // T26+); DeviceConfigDialog does not offer Ble as a selectable
            // transport for exactly this reason. m_transport stays null --
            // nothing calls connectTo()/connectToTcp()/attemptReconnect()
            // for a Ble device today, so the connect() calls just below see
            // a null sender and no-op (Qt warns, does not crash).
            break;
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
        if (connected &&
            (!m_shouldBeConnected || m_connectionPhase != ConnectionPhase::PreparingTransport)) {
            return;
        }
        if (!connected && m_shouldBeConnected &&
            (m_connectionPhase == ConnectionPhase::Connecting ||
             m_connectionPhase == ConnectionPhase::PreparingTransport)) {
            // A late close from a superseded attempt must not tear down the
            // state of the new attempt.
            m_attemptInProgress = false;
            return;
        }
        m_attemptInProgress = false;
        emit connectionStateChanged(connected);
        setConnectionPhase(connected
                       ? (m_transportType == TransportType::HubChannel
                          ? ConnectionPhase::Ready
                          : ConnectionPhase::NegotiatingBtp)
                       : ConnectionPhase::Disconnected);
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
            setConnectionPhase(ConnectionPhase::Disconnected);
        }
    });
    // A dead session on a live transport: close it and let the retry timer
    // below reopen it, which restarts the handshake through
    // onTransportConnectionChanged(). Closing is what makes attemptReconnect()
    // eligible at all -- it returns early while the transport is still
    // connected, which is exactly the state a failed handshake leaves behind.
    connect(m_backend, &Backend::sessionRecoveryNeeded, this, [this] {
        if (m_shouldBeConnected && m_connectionPhase == ConnectionPhase::NegotiatingBtp &&
            AppSettings::instance().autoReconnect()) {
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
        } else if (m_shouldBeConnected && !m_transport->isConnected()) {
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
    m_target = target;
    m_baudRate = baudRate;
    m_shouldBeConnected = !target.isEmpty();
    m_attemptInProgress = false;

    if (!m_shouldBeConnected) {
        qCInfo(lcConnection) << "target cleared, intent now offline";
        m_retryTimer->stop();
        closeTransportGracefully();
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
    m_shouldBeConnected = !trimmedHost.isEmpty() && port != 0;
    m_attemptInProgress = false;

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
        return;
    }
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

    m_attemptInProgress = false;
    setConnectionPhase(ConnectionPhase::Connecting);
    m_hubTransport->setPeerSourceId(peerSourceId);
    m_hubTransport->attachTo(parentConnection);

    // Told before the link can come up, because coming up is what makes the
    // child ask its robot for a catalog -- and it has to know which robot by
    // then.
    if (auto* btpBackend = qobject_cast<BtpBackend*>(m_backend)) {
        btpBackend->setHubEndpoint(selfSourceId, peerSourceId, endpointKey);
    }

    // Same ambient-intent model as connectTo(): "not configured" means no
    // parent or no peer, and anything else means this child should be online
    // whenever its parent is. A parent that drops does NOT clear the intent,
    // so the child comes back on its own when the cable does -- which is the
    // behavior a hub needs, since the parent going away is the common case
    // (unplugging the dongle) and not an instruction from the user.
    m_shouldBeConnected = parentConnection != nullptr && peerSourceId != 0;
    if (!m_shouldBeConnected) {
        m_retryTimer->stop();
        closeTransportGracefully();
        return;
    }
    if (AppSettings::instance().autoReconnect()) {
        m_retryTimer->start();
    }
}

void DeviceConnection::disconnectFrom() {
    qCInfo(lcConnection) << "disconnect requested for" << m_target;
    m_shouldBeConnected = false;
    m_attemptInProgress = false;
    m_retryTimer->stop();
    setConnectionPhase(ConnectionPhase::Disconnected);
    closeTransportGracefully();
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
        setConnectionPhase(ConnectionPhase::Disconnected);
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

void DeviceConnection::closeTransportGracefully() {
    if (m_transport == nullptr || !m_transport->isConnected()) {
        return;
    }

    if (m_serialManager != nullptr) {
        if (auto* btpBackend = qobject_cast<BtpBackend*>(m_backend)) {
            if (btpBackend->requestSessionClose()) {
                // bytesToWrite -> SerialManager::write is a direct connection
                // in this thread, so the frame is already queued here. Drain
                // it before close() lowers DTR and tears the native CDC down.
                m_serialManager->drainWrites(kCloseWriteDrainTimeoutMs);
            }
        }
    }
    m_transport->close();
}

void DeviceConnection::setLineTerminator(int terminator) {
    if (m_serialManager) {
        m_serialManager->setLineTerminator(LineTerminator(terminator));
    }
}

void DeviceConnection::attemptReconnect() {
    if (!m_shouldBeConnected || !m_transportAvailable || m_transport->isConnected() ||
        m_attemptInProgress) {
        return;
    }
    qCInfo(lcConnection) << "attempting connect to"
                        << (m_tcpTransport ? QString("%1:%2").arg(m_tcpHost).arg(m_tcpPort)
                                           : m_target);
    m_attemptInProgress = true;
    setConnectionPhase(ConnectionPhase::PreparingTransport);
    if (m_serialManager) {
        m_serialManager->open(m_target, m_baudRate);
    } else if (m_usbHidManager) {
        m_usbHidManager->open(m_target);
    } else if (m_tcpTransport) {
        m_tcpTransport->open(m_tcpHost, m_tcpPort);
    }
    // HubChannel has nothing to retry: it has no port to reopen, and its
    // connected state is a function of its parent's, which it is already
    // watching. The retry timer still runs so that a parent attached before
    // it was connected is re-evaluated, which HubTransport does on its own
    // signal -- so this branch is deliberately empty rather than absent, to
    // say that the omission is a decision and not a missing case.
}

}  // namespace traceview
