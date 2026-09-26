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

bool BleTransport::isPlatformAddress(const QString& target) {
    const QString trimmed = target.trimmed();
    return !QBluetoothAddress(trimmed).isNull() || !QBluetoothUuid(trimmed).isNull();
}

QHash<QString, QString>& BleTransport::nameCache() {
    static QHash<QString, QString> cache;
    return cache;
}

QString BleTransport::cachedAddressForName(const QString& name) {
    return nameCache().value(name.trimmed().toCaseFolded());
}

void BleTransport::rememberAddressForName(const QString& name, const QString& address) {
    const QString key = name.trimmed().toCaseFolded();
    if (key.isEmpty() || !isPlatformAddress(address) || nameCache().contains(key)) {
        return;
    }
    nameCache().insert(key, address.trimmed());
}

bool BleTransport::bleNameMatches(const QString& advertised, const QString& wanted) {
    const QString name = advertised.trimmed();
    return !name.isEmpty() && name.compare(wanted.trimmed(), Qt::CaseInsensitive) == 0;
}

BleTransport::BleTransport(QObject* parent) : Transport(parent) {
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, &BleTransport::onConnectTimeout);
    m_lookupTimer.setSingleShot(true);
    connect(&m_lookupTimer, &QTimer::timeout, this, &BleTransport::onNameLookupTimeout);
    m_settleTimer.setSingleShot(true);
    connect(&m_settleTimer, &QTimer::timeout, this, &BleTransport::finishNameLookup);
}

BleTransport::~BleTransport() {
    teardown(/*disconnectController=*/false);
}

bool BleTransport::open(const QString& target) {
    const QString trimmed = target.trimmed();
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
        if (isPlatformAddress(m_address)) {
            startController(m_address);
        } else {
            startNameLookup();
        }
    });
    return true;
}

void BleTransport::startNameLookup() {
    m_cachedAddress = cachedAddressForName(m_address);
    qCInfo(lcConnection) << "looking up BLE robot named" << m_address
                         << (m_cachedAddress.isEmpty()
                                 ? QString()
                                 : QStringLiteral("(last seen at %1)").arg(m_cachedAddress));
    m_lookupMatches.clear();
    m_lookupSeen.clear();
    m_lookup = new BleDiscoveryService(this);
    // BleDiscoveryService already filters on the BTP service UUID, so only
    // the name is left to compare. It re-emits the same peripheral on every
    // sweep; the list below keeps each address once.
    //
    // The address this name last resolved to counts as a match even while
    // its advertised name is still empty: the name travels in the scan
    // response, which Windows in particular often reports late or not at
    // all, while the UUID-bearing advertisement is already in.
    connect(m_lookup, &BleDiscoveryService::deviceDiscovered, this,
            [this](const BleDiscoveryService::DiscoveredDevice& device) {
                const QString seen = device.name.isEmpty()
                                         ? device.address
                                         : QStringLiteral("%1 %2").arg(device.name, device.address);
                if (!m_lookupSeen.contains(seen)) {
                    m_lookupSeen.append(seen);
                }
                const bool nameMatches = bleNameMatches(device.name, m_address);
                const bool cachedMatches = device.name.trimmed().isEmpty() &&
                                           !m_cachedAddress.isEmpty() &&
                                           device.address == m_cachedAddress;
                if ((!nameMatches && !cachedMatches) ||
                    m_lookupMatches.contains(device.address)) {
                    return;
                }
                m_lookupMatches.append(device.address);
                if (m_lookupMatches.size() == 1) {
                    m_settleTimer.start(kNameSettleMs);
                }
            });
    connect(m_lookup, &BleDiscoveryService::errorOccurred, this,
            [this](const QString& message) {
                failConnection(tr("BLE scan failed: %1").arg(message));
            });
    m_lookupTimer.start(kNameLookupTimeoutMs);
    m_lookup->start();
}

void BleTransport::onNameLookupTimeout() {
    const QString cached = m_cachedAddress;
    const QStringList seen = m_lookupSeen;
    stopNameLookup();
    qCInfo(lcConnection) << "BLE lookup for" << m_address << "saw"
                         << (seen.isEmpty() ? QStringLiteral("no BTP peripheral")
                                            : seen.join(QStringLiteral("; ")));
    if (!cached.isEmpty()) {
        // Not heard advertising, but it answered to this name before: dial
        // it directly -- a robot that is up but whose advertisements this
        // scan missed still connects, and one that is off just times out.
        qCInfo(lcConnection) << "BLE name" << m_address << "not seen, trying last address"
                             << cached;
        startController(cached);
        return;
    }
    failConnection(seen.isEmpty()
                       ? tr("no BTP robot named \"%1\" found nearby (no BTP robot "
                            "advertising at all)")
                             .arg(m_address)
                       : tr("no BTP robot named \"%1\" found nearby (seen: %2)")
                             .arg(m_address, seen.join(QStringLiteral("; "))));
}

void BleTransport::finishNameLookup() {
    const QStringList matches = m_lookupMatches;
    // Scanning while connecting slows the connection down on several
    // backends (Android in particular), so the scan ends first.
    stopNameLookup();
    if (matches.isEmpty()) {
        return;
    }
    if (matches.size() > 1) {
        failConnection(tr("%1 BLE robots are named \"%2\" (%3) -- give each robot its own "
                          "name, or pick one by address")
                           .arg(matches.size())
                           .arg(m_address, matches.join(QStringLiteral(", "))));
        return;
    }
    qCInfo(lcConnection) << "BLE name" << m_address << "found at" << matches.first();
    nameCache().insert(m_address.toCaseFolded(), matches.first());
    startController(matches.first());
}

void BleTransport::stopNameLookup() {
    m_lookupTimer.stop();
    m_settleTimer.stop();
    m_lookupMatches.clear();
    m_lookupSeen.clear();
    if (m_lookup != nullptr) {
        // deleteLater(), not delete: this can run from inside one of
        // m_lookup's own signals (an error, or the match that ends the scan).
        m_lookup->disconnect(this);
        m_lookup->stop();
        m_lookup->deleteLater();
        m_lookup = nullptr;
    }
}

void BleTransport::startController(const QString& address) {
    const QBluetoothAddress macAddress(address);
    QBluetoothDeviceInfo info = !macAddress.isNull()
        ? QBluetoothDeviceInfo(macAddress, QString(), 0)
        : QBluetoothDeviceInfo(QBluetoothUuid(address), QString(), 0);
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

    qCInfo(lcConnection) << "connecting BLE to" << address;
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
    // RX is a byte stream (fragmentation-and-transports.md section 8.2: the
    // robot concatenates writes and COBS-decodes), so one write may carry
    // the tail of one frame and the start of the next. Filling every write
    // up to the MTU instead of one frame per write is what keeps small
    // frames (a terminal keystroke is ~60 octets) from each paying a full
    // write-with-response round trip.
    const qsizetype capacity = mtuPayloadSize();
    QByteArray chunk;
    chunk.reserve(capacity);
    while (chunk.size() < capacity) {
        if (m_currentFrame.isEmpty()) {
            if (m_pendingFrames.isEmpty()) {
                break;
            }
            m_currentFrame = m_pendingFrames.dequeue();
            m_currentOffset = 0;
        }
        const qsizetype take =
            qMin(capacity - chunk.size(), m_currentFrame.size() - m_currentOffset);
        chunk.append(m_currentFrame.constData() + m_currentOffset, take);
        m_currentOffset += take;
        if (m_currentOffset >= m_currentFrame.size()) {
            m_currentFrame.clear();
            m_currentOffset = 0;
        }
    }
    if (chunk.isEmpty()) {
        return;
    }
    m_writeInFlight = true;
    // RX is "write with response" by contract (T04) -- the next chunk is
    // only sent from onCharacteristicWritten(), which is also what gives a
    // slow/congested link real backpressure instead of queuing writes the
    // controller has no room for.
    m_service->writeCharacteristic(m_rxCharacteristic, chunk, QLowEnergyService::WriteWithResponse);
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
    stopNameLookup();
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
