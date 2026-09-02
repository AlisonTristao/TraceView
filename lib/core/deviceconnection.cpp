#include "deviceconnection.h"

#include <QTimer>
#include <btp/codec.hpp>

#include "backend/backend.h"
#include "hubtransport.h"
#include "preferences/appsettings.h"
#include "protocol/btpbackend.h"
#include "serialmanager.h"
#include "usbhidmanager.h"

namespace traceview {

namespace {
// How often a device with an unmet "should be online" intent retries
// open() -- covers both a freshly configured device's first attempt and
// recovering from an unplug/replug without any user action.
constexpr int kCloseWriteDrainTimeoutMs = 250;

// BtpBackend/BtpSession speak two independent axes -- link framing and
// encode profile (btp::TransportProfile, the BTP library's own type; see
// btpsession.h for why they are separate); Device speaks TransportType
// (devices/device.h, dependency-free of btp::codec). This is the one place
// that needs to know both -- traceview_devices still doesn't depend on
// btp::codec.
struct BtpSessionAxes {
    BtpSession::Framing framing;
    btp::TransportProfile encodeProfile;
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
            return {BtpSession::Framing::CobsStream, btp::TransportProfile::Serial};
        case TransportType::UsbHid:
            return {BtpSession::Framing::PreFramed, btp::TransportProfile::UsbHid};
        case TransportType::HubChannel:
            return {BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow};
    }
    return {BtpSession::Framing::CobsStream, btp::TransportProfile::Serial};
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
    }

    switch (commType) {
        case CommType::Btp: {
            const BtpSessionAxes axes = toBtpSessionAxes(transportType);
            m_backend = new BtpBackend(axes.framing, axes.encodeProfile, this);
            break;
        }
    }

    // Same wiring MainWindow::MainWindow() used to do once for the whole
    // app -- see core/mainwindow.cpp before the multi-device refactor. All
    // four connections are against the common Transport base (transport.h)
    // now, so this is identical regardless of which concrete transport
    // `transportType` picked above.
    connect(m_transport, &Transport::dataReceived, m_backend, &Backend::feedBytes);
    connect(m_backend, &Backend::bytesToWrite, m_transport, &Transport::write);
    connect(m_transport, &Transport::connectionStateChanged, m_backend,
            &Backend::onTransportConnectionChanged);
    connect(m_transport, &Transport::connectionStateChanged, this,
            &DeviceConnection::connectionStateChanged);
    connect(m_backend, &Backend::deviceIdentified, this, &DeviceConnection::deviceIdentified);
    connect(m_backend, &Backend::deviceInfoReported, this, &DeviceConnection::deviceInfoReported);
    // A dead session on a live transport: close it and let the retry timer
    // below reopen it, which restarts the handshake through
    // onTransportConnectionChanged(). Closing is what makes attemptReconnect()
    // eligible at all -- it returns early while the transport is still
    // connected, which is exactly the state a failed handshake leaves behind.
    connect(m_backend, &Backend::sessionRecoveryNeeded, this, [this] {
        if (m_shouldBeConnected && AppSettings::instance().autoReconnect()) {
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

    if (!m_shouldBeConnected) {
        m_retryTimer->stop();
        closeTransportGracefully();
        return;
    }

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
    m_shouldBeConnected = false;
    m_retryTimer->stop();
    closeTransportGracefully();
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
    if (!m_shouldBeConnected || m_transport->isConnected()) {
        return;
    }
    if (m_serialManager) {
        m_serialManager->open(m_target, m_baudRate);
    } else if (m_usbHidManager) {
        m_usbHidManager->open(m_target);
    }
    // HubChannel has nothing to retry: it has no port to reopen, and its
    // connected state is a function of its parent's, which it is already
    // watching. The retry timer still runs so that a parent attached before
    // it was connected is re-evaluated, which HubTransport does on its own
    // signal -- so this branch is deliberately empty rather than absent, to
    // say that the omission is a decision and not a missing case.
}

}  // namespace traceview
