#include "deviceconnection.h"

#include <QTimer>

#include <btp/codec.hpp>

#include "backend/backend.h"
#include "protocol/btpbackend.h"
#include "serialmanager.h"
#include "usbhidmanager.h"

namespace traceview {

namespace {
// How often a device with an unmet "should be online" intent retries
// open() -- covers both a freshly configured device's first attempt and
// recovering from an unplug/replug without any user action.
constexpr int kRetryIntervalMs = 3000;

// BtpBackend/BtpSession speak btp::TransportProfile (the BTP library's own
// type, see btpsession.h); Device speaks TransportType (devices/device.h,
// dependency-free of btp::codec). This is the one place that needs to know
// both -- traceview_devices still doesn't depend on btp::codec.
btp::TransportProfile toBtpTransportProfile(TransportType type) {
    switch (type) {
        case TransportType::Serial:
            return btp::TransportProfile::Serial;
        case TransportType::UsbHid:
            return btp::TransportProfile::UsbHid;
    }
    return btp::TransportProfile::Serial;
}
} // namespace

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
    }

    switch (commType) {
        case CommType::Btp:
            m_backend = new BtpBackend(toBtpTransportProfile(transportType), this);
            break;
    }

    // Same wiring MainWindow::MainWindow() used to do once for the whole
    // app -- see core/mainwindow.cpp before the multi-device refactor. All
    // four connections are against the common Transport base (transport.h)
    // now, so this is identical regardless of which concrete transport
    // `transportType` picked above.
    connect(m_transport, &Transport::dataReceived, m_backend, &Backend::feedBytes);
    connect(m_backend, &Backend::bytesToWrite, m_transport, &Transport::write);
    connect(m_transport, &Transport::connectionStateChanged, m_backend, &Backend::onTransportConnectionChanged);
    connect(m_transport, &Transport::connectionStateChanged, this, &DeviceConnection::connectionStateChanged);
    connect(m_backend, &Backend::deviceIdentified, this, &DeviceConnection::deviceIdentified);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(kRetryIntervalMs);
    connect(m_retryTimer, &QTimer::timeout, this, &DeviceConnection::attemptReconnect);
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
        m_transport->close();
        return;
    }

    if (targetChanged && m_transport->isConnected()) {
        m_transport->close();
    }
    attemptReconnect();
    m_retryTimer->start();
}

void DeviceConnection::disconnectFrom() {
    m_shouldBeConnected = false;
    m_retryTimer->stop();
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
}

} // namespace traceview
