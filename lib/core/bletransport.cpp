#include "bletransport.h"

#include <QBluetoothAddress>
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QMetaEnum>

#include "blediscoveryservice.h"
#include "core/applog.h"
#include "core/blepermission.h"

namespace traceview {

namespace {

// Little-endian uint16 0x0001: "enable notifications", the fixed value BLE
// itself defines for a Client Characteristic Configuration Descriptor
// (Bluetooth Core Spec, not a BTP choice) -- 0x0002 would ask for
// indications instead, which TX is not (T04: "TX para notificações").
QByteArray enableNotificationValue() {
    return QByteArray::fromHex("0100");
}

}  // namespace

QBluetoothUuid BleTransport::rxCharacteristicUuid() {
    return QBluetoothUuid(QStringLiteral("f9160b78-c242-42f4-8f5e-88df2c51cbe6"));
}

QBluetoothUuid BleTransport::txCharacteristicUuid() {
    return QBluetoothUuid(QStringLiteral("20f96ede-2f3b-4e02-b2cf-be6bb75dbe35"));
}

BleTransport::BleTransport(QObject* parent) : Transport(parent) {
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, &BleTransport::onConnectTimeout);
}

BleTransport::~BleTransport() {
    teardown(/*disconnectController=*/false);
}

bool BleTransport::open(const QString& address) {
    const QString trimmed = address.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }

    // Always a clean rebuild -- see the class comment on why this class
    // does not try to reuse a QLowEnergyController across attempts the way
    // TcpTransport reuses one QTcpSocket.
    teardown(/*disconnectController=*/false);

    m_address = trimmed;
    m_closing = false;

    // Connecting waits for the Bluetooth permission (see blepermission.h):
    // synchronous on desktop, a system prompt on Android/macOS/iOS. A
    // reconnect after an earlier Grant is synchronous everywhere. m_attempt
    // is bumped by teardown(), so a close() or a newer open() made while the
    // prompt is up discards this attempt's late answer.
    const quint64 attempt = m_attempt;
    withBluetoothPermission(this, [this, attempt](bool granted) {
        if (attempt != m_attempt) {
            return;
        }
        if (!granted) {
            failConnection(tr("Bluetooth permission denied. Allow TraceView to use "
                              "Bluetooth in the system settings."));
            return;
        }
        startController();
    });
    return true;
}

void BleTransport::startController() {
    const QBluetoothAddress macAddress(m_address);
    QBluetoothDeviceInfo info = !macAddress.isNull()
        ? QBluetoothDeviceInfo(macAddress, QString(), 0)
        : QBluetoothDeviceInfo(QBluetoothUuid(m_address), QString(), 0);
    info.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);

    m_controller = QLowEnergyController::createCentral(info, this);
    if (m_controller == nullptr) {
        // Not observed in practice on any backend this project targets, but
        // cheaper to guard than to let connectToDevice() below dereference a
        // null pointer if it ever does happen.
        failConnection(tr("failed to create a BLE controller"));
        return;
    }
    connect(m_controller, &QLowEnergyController::connected, this,
            &BleTransport::onControllerConnected);
    connect(m_controller, &QLowEnergyController::disconnected, this,
            &BleTransport::onControllerDisconnected);
    connect(m_controller, &QLowEnergyController::errorOccurred, this,
            &BleTransport::onControllerError);
    connect(m_controller, &QLowEnergyController::discoveryFinished, this,
            &BleTransport::onDiscoveryFinished);

    qCInfo(lcConnection) << "connecting BLE to" << m_address;
    m_connectTimer.start(m_connectTimeoutMs);
    m_controller->connectToDevice();
}

void BleTransport::close() {
    const bool wasConnected = m_connected;
    m_closing = true;
    // Synchronous from this class's own point of view -- unlike
    // TcpTransport (whose abort() relies on the socket's own disconnected()
    // signal to fire connectionStateChanged(false)), teardown() here tears
    // the controller/service down immediately, so the state change has to
    // be reported explicitly rather than waiting for a signal that may
    // never come once everything is disconnected first (see teardown()).
    teardown(/*disconnectController=*/true);
    if (wasConnected) {
        emit connectionStateChanged(false);
    }
}

bool BleTransport::isConnected() const {
    return m_connected;
}

bool BleTransport::write(const QByteArray& data) {
    if (!m_connected || data.isEmpty() || m_pendingFrames.size() >= kMaxPendingFrames) {
        return false;
    }
    m_pendingFrames.enqueue(data);
    drainNext();
    return true;
}

void BleTransport::onControllerConnected() {
    qCInfo(lcConnection) << "BLE link up with" << m_address << "-- discovering services";
    m_controller->discoverServices();
}

void BleTransport::onControllerDisconnected() {
    const bool wasClosing = m_closing;
    const bool wasConnected = m_connected;
    qCInfo(lcConnection) << "BLE disconnected from" << m_address;
    teardown(/*disconnectController=*/false);
    if (!wasClosing && wasConnected) {
        emit connectionStateChanged(false);
    } else if (!wasClosing) {
        // Dropped before RX/TX ever became usable -- connectionStateChanged
        // was never true, so DeviceConnection is still waiting on the
        // PreparingTransport phase. Surface it as an error instead of a
        // silent, indefinite hang; DeviceConnection's own retry timer drives
        // the next attempt (see attemptReconnect()), exactly as it does for
        // a TCP connect failure.
        emit errorOccurred(tr("BLE peripheral disconnected before HELLO"));
    }
}

void BleTransport::onControllerError() {
    if (m_controller == nullptr) {
        return;
    }
    failConnection(m_controller->errorString());
}

void BleTransport::onDiscoveryFinished() {
    if (m_controller == nullptr) {
        return;
    }
    if (!m_controller->services().contains(BleDiscoveryService::btpServiceUuid())) {
        failConnection(tr("peripheral does not advertise the BTP service"));
        return;
    }
    m_service = m_controller->createServiceObject(BleDiscoveryService::btpServiceUuid(), this);
    if (m_service == nullptr) {
        failConnection(tr("failed to create BTP service object"));
        return;
    }
    connect(m_service, &QLowEnergyService::stateChanged, this,
            &BleTransport::onServiceStateChanged);
    connect(m_service, &QLowEnergyService::errorOccurred, this, &BleTransport::onServiceError);
    connect(m_service, &QLowEnergyService::characteristicChanged, this,
            &BleTransport::onCharacteristicChanged);
    connect(m_service, &QLowEnergyService::characteristicWritten, this,
            &BleTransport::onCharacteristicWritten);
    connect(m_service, &QLowEnergyService::descriptorWritten, this,
            &BleTransport::onDescriptorWritten);
    m_service->discoverDetails();
}

void BleTransport::onServiceStateChanged(QLowEnergyService::ServiceState newState) {
    if (newState != QLowEnergyService::RemoteServiceDiscovered) {
        return;
    }

    m_rxCharacteristic = m_service->characteristic(rxCharacteristicUuid());
    m_txCharacteristic = m_service->characteristic(txCharacteristicUuid());
    // Explicit failure for an incompatible service (T28's acceptance
    // criterion) rather than silently sitting in PreparingTransport forever.
    if (!m_rxCharacteristic.isValid() || !m_txCharacteristic.isValid()) {
        failConnection(tr("BTP service is missing its RX/TX characteristics"));
        return;
    }

    m_txNotificationDescriptor =
        m_txCharacteristic.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
    if (!m_txNotificationDescriptor.isValid()) {
        failConnection(tr("TX characteristic has no notification descriptor"));
        return;
    }
    // connectionStateChanged(true) fires only once this descriptor write is
    // acknowledged (onDescriptorWritten()) -- see the class comment for why.
    m_service->writeDescriptor(m_txNotificationDescriptor, enableNotificationValue());
}

void BleTransport::onServiceError() {
    // QLowEnergyService has no errorString() in this Qt version -- only the
    // enum (error()). QMetaEnum::valueToKey() turns it back into its
    // constant name (e.g. "CharacteristicWriteError"), which is a perfectly
    // usable message for a log line/status toast even if not prose.
    QString message = tr("BLE service error");
    if (m_service != nullptr) {
        const QMetaEnum metaEnum = QMetaEnum::fromType<QLowEnergyService::ServiceError>();
        if (const char* name = metaEnum.valueToKey(int(m_service->error()))) {
            message = QString::fromLatin1(name);
        }
    }
    failConnection(message);
}

void BleTransport::onDescriptorWritten(const QLowEnergyDescriptor& descriptor,
                                       const QByteArray& value) {
    if (descriptor != m_txNotificationDescriptor || value != enableNotificationValue()) {
        return;
    }
    qCInfo(lcConnection) << "BLE RX/TX usable for" << m_address;
    m_connectTimer.stop();
    m_connected = true;
    emit connectionStateChanged(true);
}

void BleTransport::onCharacteristicChanged(const QLowEnergyCharacteristic& characteristic,
                                           const QByteArray& value) {
    if (characteristic.uuid() != txCharacteristicUuid() || value.isEmpty()) {
        return;
    }
    // Forwarded exactly as received, with no framing applied here -- see
    // the class comment: BtpSession's CobsStream decoder reassembles the
    // BTP frame out of this notification stream the same way it already
    // does for TcpTransport's raw socket reads.
    emit dataReceived(value);
}

void BleTransport::onCharacteristicWritten(const QLowEnergyCharacteristic& characteristic,
                                           const QByteArray& value) {
    Q_UNUSED(value);
    if (characteristic.uuid() != rxCharacteristicUuid() || !m_writeInFlight) {
        return;
    }
    m_writeInFlight = false;
    drainNext();
}

void BleTransport::onConnectTimeout() {
    if (m_connected) {
        return;
    }
    failConnection(tr("BLE connection timed out"));
}

void BleTransport::drainNext() {
    if (!m_connected || m_writeInFlight) {
        return;
    }
    if (m_currentFrame.isEmpty()) {
        if (m_pendingFrames.isEmpty()) {
            return;
        }
        m_currentFrame = m_pendingFrames.dequeue();
        m_currentOffset = 0;
    }

    const int chunkSize =
        qMin(qsizetype(mtuPayloadSize()), m_currentFrame.size() - m_currentOffset);
    const QByteArray chunk = m_currentFrame.mid(m_currentOffset, chunkSize);
    m_currentOffset += chunkSize;
    m_writeInFlight = true;
    // RX is "write with response" by contract (T04) -- the next chunk is
    // only sent from onCharacteristicWritten(), which is also what gives a
    // slow/congested link real backpressure instead of queuing writes the
    // controller has no room for.
    m_service->writeCharacteristic(m_rxCharacteristic, chunk, QLowEnergyService::WriteWithResponse);

    if (m_currentOffset >= m_currentFrame.size()) {
        m_currentFrame.clear();
        m_currentOffset = 0;
    }
}

int BleTransport::mtuPayloadSize() const {
    // ATT_MTU minus the 3-octet ATT write-request header. Before the
    // controller reports a negotiated value (or on a backend that never
    // does), 23 is BLE's own default/minimum ATT_MTU -- an honest
    // conservative floor, not a BTP-specific number.
    const int mtu = m_controller != nullptr ? m_controller->mtu() : 0;
    return qMax(20, (mtu > 3 ? mtu : 23) - 3);
}

void BleTransport::failConnection(const QString& reason) {
    qCWarning(lcConnection) << "BLE error on" << m_address << ":" << reason;
    const bool wasConnected = m_connected;
    teardown(/*disconnectController=*/false);
    emit errorOccurred(reason);
    if (wasConnected) {
        emit connectionStateChanged(false);
    }
}

void BleTransport::teardown(bool disconnectController) {
    ++m_attempt;
    m_connectTimer.stop();
    m_connected = false;
    m_pendingFrames.clear();
    m_currentFrame.clear();
    m_currentOffset = 0;
    m_writeInFlight = false;
    m_rxCharacteristic = QLowEnergyCharacteristic();
    m_txCharacteristic = QLowEnergyCharacteristic();
    m_txNotificationDescriptor = QLowEnergyDescriptor();

    if (m_service != nullptr) {
        m_service->disconnect(this);
        m_service->deleteLater();
        m_service = nullptr;
    }
    if (m_controller != nullptr) {
        m_controller->disconnect(this);
        if (disconnectController) {
            m_controller->disconnectFromDevice();
        }
        m_controller->deleteLater();
        m_controller = nullptr;
    }
}

}  // namespace traceview
