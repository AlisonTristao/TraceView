#include "deviceconnection.h"

#include <QTimer>

#include "backend/backend.h"
#include "protocol/btpbackend.h"
#include "serialmanager.h"

namespace traceview {

namespace {
// How often a device with an unmet "should be online" intent retries
// open() -- covers both a freshly configured device's first attempt and
// recovering from an unplug/replug without any user action.
constexpr int kRetryIntervalMs = 3000;
} // namespace

DeviceConnection::DeviceConnection(CommType commType, QObject* parent)
    : QObject(parent), m_serialManager(new SerialManager(this)) {
    switch (commType) {
        case CommType::Btp:
            m_backend = new BtpBackend(this);
            break;
    }

    // Same wiring MainWindow::MainWindow() used to do once for the whole
    // app -- see core/mainwindow.cpp before the multi-device refactor.
    connect(m_serialManager, &SerialManager::dataReceived, m_backend, &Backend::feedBytes);
    connect(m_backend, &Backend::bytesToWrite, m_serialManager, &SerialManager::write);
    connect(m_serialManager, &SerialManager::connectionStateChanged, m_backend,
            &Backend::onTransportConnectionChanged);
    connect(m_serialManager, &SerialManager::connectionStateChanged, this,
            &DeviceConnection::connectionStateChanged);
    connect(m_backend, &Backend::deviceIdentified, this, &DeviceConnection::deviceIdentified);

    m_retryTimer = new QTimer(this);
    m_retryTimer->setInterval(kRetryIntervalMs);
    connect(m_retryTimer, &QTimer::timeout, this, &DeviceConnection::attemptReconnect);
}

bool DeviceConnection::isConnected() const {
    return m_serialManager->isConnected();
}

void DeviceConnection::connectTo(const QString& portName, qint32 baudRate) {
    const bool targetChanged = portName != m_portName || baudRate != m_baudRate;
    m_portName = portName;
    m_baudRate = baudRate;
    m_shouldBeConnected = !portName.isEmpty();

    if (!m_shouldBeConnected) {
        m_retryTimer->stop();
        m_serialManager->close();
        return;
    }

    if (targetChanged && m_serialManager->isConnected()) {
        m_serialManager->close();
    }
    attemptReconnect();
    m_retryTimer->start();
}

void DeviceConnection::disconnectFrom() {
    m_shouldBeConnected = false;
    m_retryTimer->stop();
    m_serialManager->close();
}

void DeviceConnection::setLineTerminator(int terminator) {
    m_serialManager->setLineTerminator(LineTerminator(terminator));
}

void DeviceConnection::attemptReconnect() {
    if (!m_shouldBeConnected || m_serialManager->isConnected()) {
        return;
    }
    m_serialManager->open(m_portName, m_baudRate);
}

} // namespace traceview
