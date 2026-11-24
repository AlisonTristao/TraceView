#include "core/nearbyrobots.h"

#include <QDateTime>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

#include "core/deviceconnection.h"
#include "ota/mdnsbrowser.h"

#ifdef TRACEVIEW_ENABLE_SERIAL
#ifdef Q_OS_ANDROID
#include "core/androidusbserialtransport.h"
#else
#include <QSerialPortInfo>

#include "core/serialmanager.h"
#endif
#include "core/serialtransport.h"
#endif

#ifdef TRACEVIEW_ENABLE_BLE
#include "core/blediscoveryservice.h"
#include "core/blepermission.h"
#endif

namespace traceview {

namespace {
constexpr int kSerialRefreshMs = 2000;
constexpr quint16 kDefaultBtpPort = 44300;
// A link not heard from for this long drops off the list. mDNS is asked
// every 30 s (MdnsBrowser), so a Wi-Fi robot survives one lost answer.
constexpr qint64 kTcpExpiryMs = 75000;
constexpr qint64 kBleExpiryMs = 120000;
constexpr int kBleScanMs = 8000;
// The passive BLE scan runs at most this often (each time the Home page
// opens, not more).
constexpr qint64 kPassiveBleIntervalMs = 60000;

// Deep scan.
constexpr int kSweepParallel = 48;
constexpr int kSweepConnectTimeoutMs = 700;
constexpr int kProbeTimeoutMs = 8000;
// After the BTP session is up, how long to wait for the robot's name.
constexpr int kProbeNameGraceMs = 3000;
constexpr int kDeepTickMs = 250;

// "USB JTAG/serial debug unit" and similar: an ESP32 without bally_OS's
// USB stack, not a robot that named itself.
bool genericProductName(const QString& product) {
    const QString lower = product.toLower();
    return lower.isEmpty() || lower.contains(QLatin1String("jtag")) ||
           lower.contains(QLatin1String("serial debug")) || lower == QLatin1String("espressif") ||
           lower == QLatin1String("tinyusb device");
}

qint64 nowMs() {
    return QDateTime::currentMSecsSinceEpoch();
}

QString nameFromInfo(const QVector<DeviceInfoRecord>& info) {
    for (const DeviceInfoRecord& record : info) {
        if (record.key == QLatin1String("name") && !record.value.trimmed().isEmpty()) {
            return record.value.trimmed();
        }
    }
    return QString();
}
}  // namespace

NearbyRobotsService::NearbyRobotsService(QObject* parent) : QObject(parent) {
    m_serialTimer = new QTimer(this);
    m_serialTimer->setInterval(kSerialRefreshMs);
    connect(m_serialTimer, &QTimer::timeout, this, [this] {
        refreshSerialPorts();
        prune();
    });

    m_mdns = new MdnsBrowser(QStringLiteral("_btp._tcp"), this);
    connect(m_mdns, &MdnsBrowser::serviceSeen, this, &NearbyRobotsService::onServiceSeen);

#ifdef TRACEVIEW_ENABLE_BLE
    m_ble = new BleDiscoveryService(this);
    connect(m_ble, &BleDiscoveryService::deviceDiscovered, this,
            [this](const BleDiscoveryService::DiscoveredDevice& device) {
                NearbyRobot& robot = upsert(device.name, QStringLiteral("ble:") + device.address);
                robot.bleAddress = device.address;
                robot.bleSeenMs = nowMs();
                notifyChanged();
            });
    connect(m_ble, &BleDiscoveryService::finished, this,
            [this] { emit bluetoothScanningChanged(false); });
    connect(m_ble, &BleDiscoveryService::errorOccurred, this, [this](const QString&) {
        m_ble->stop();
        emit bluetoothScanningChanged(false);
    });
    m_bleStopTimer = new QTimer(this);
    m_bleStopTimer->setSingleShot(true);
    m_bleStopTimer->setInterval(kBleScanMs);
    connect(m_bleStopTimer, &QTimer::timeout, this, [this] {
        m_ble->stop();
        emit bluetoothScanningChanged(false);
    });
#endif

    m_deepTimer = new QTimer(this);
    m_deepTimer->setInterval(kDeepTickMs);
    connect(m_deepTimer, &QTimer::timeout, this, &NearbyRobotsService::onDeepScanTick);
}

NearbyRobotsService::~NearbyRobotsService() {
    cancelDeepScan();
}

void NearbyRobotsService::setActive(bool active) {
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (!active) {
        m_serialTimer->stop();
        m_mdns->stop();
#ifdef TRACEVIEW_ENABLE_BLE
        m_bleStopTimer->stop();
        if (m_ble->isScanning()) {
            m_ble->stop();
            emit bluetoothScanningChanged(false);
        }
#endif
        return;
    }
    refreshSerialPorts();
    m_serialTimer->start();
    m_mdns->start();
#ifdef TRACEVIEW_ENABLE_BLE
    // Never the permission prompt on its own: only when already allowed.
    if (bluetoothPermissionGranted() && nowMs() - m_lastPassiveBleMs > kPassiveBleIntervalMs) {
        m_lastPassiveBleMs = nowMs();
        scanBluetooth();
    }
#endif
}

bool NearbyRobotsService::bluetoothAvailable() {
#ifdef TRACEVIEW_ENABLE_BLE
    return true;
#else
    return false;
#endif
}

bool NearbyRobotsService::bluetoothScanning() const {
#ifdef TRACEVIEW_ENABLE_BLE
    return m_ble->isScanning();
#else
    return false;
#endif
}

void NearbyRobotsService::scanBluetooth() {
#ifdef TRACEVIEW_ENABLE_BLE
    m_ble->start();
    m_bleStopTimer->start();
    emit bluetoothScanningChanged(true);
#endif
}

QVector<NearbyRobot> NearbyRobotsService::robots() const {
    QVector<NearbyRobot> out = m_robots.values().toVector();
    std::sort(out.begin(), out.end(), [](const NearbyRobot& a, const NearbyRobot& b) {
        // Named robots first, then by name/key.
        if (a.name.isEmpty() != b.name.isEmpty()) {
            return !a.name.isEmpty();
        }
        return QString::localeAwareCompare(a.name.isEmpty() ? a.key : a.name,
                                           b.name.isEmpty() ? b.key : b.name) < 0;
    });
    return out;
}

NearbyRobot& NearbyRobotsService::upsert(const QString& name, const QString& fallbackKey) {
    const QString trimmed = name.trimmed();
    const QString key = trimmed.isEmpty() ? fallbackKey : trimmed.toLower();
    auto it = m_robots.find(key);
    if (it == m_robots.end()) {
        NearbyRobot robot;
        robot.key = key;
        robot.name = trimmed;
        it = m_robots.insert(key, robot);
    }
    // A name learned now retires the unnamed entry the same target had.
    if (!trimmed.isEmpty() && fallbackKey != key) {
        m_robots.remove(fallbackKey);
        it = m_robots.find(key);
    }
    return it.value();
}

void NearbyRobotsService::notifyChanged() {
    emit robotsChanged();
}

void NearbyRobotsService::refreshSerialPorts() {
#ifdef TRACEVIEW_ENABLE_SERIAL
    const qint64 now = nowMs();
    QSet<QString> present;
#ifdef Q_OS_ANDROID
    for (const SerialPortOption& port : AndroidUsbSerialTransport::availablePorts()) {
        present.insert(port.name);
        if (genericProductName(port.productName)) {
            continue;
        }
        NearbyRobot& robot = upsert(port.productName, QStringLiteral("serial:") + port.name);
        robot.serialPort = port.name;
        robot.serialSeenMs = now;
    }
#else
    const QHash<QString, QString> products = SerialManager::portProductNames();
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        if (SerialManager::isDuplicateDialInPort(info.portName())) {
            continue;
        }
        present.insert(info.portName());
        const bool espressif = info.hasVendorIdentifier() &&
                               info.vendorIdentifier() == SerialTransport::kDongleVid;
        const bool dongle = espressif && info.hasProductIdentifier() &&
                            info.productIdentifier() == SerialTransport::kDonglePid;
        const QString product = products.value(info.portName());
        if (dongle) {
            NearbyRobot& robot =
                upsert(QString(), QStringLiteral("serial:") + info.portName());
            robot.dongle = true;
            robot.serialPort = info.portName();
            robot.serialSeenMs = now;
            continue;
        }
        if (!espressif || genericProductName(product)) {
            continue;
        }
        NearbyRobot& robot = upsert(product, QStringLiteral("serial:") + info.portName());
        robot.serialPort = info.portName();
        robot.serialSeenMs = now;
    }
#endif
    // A robot unplugged loses its port (and the entry, with nothing else).
    for (NearbyRobot& robot : m_robots) {
        if (!robot.serialPort.isEmpty() && !present.contains(robot.serialPort)) {
            robot.serialPort.clear();
            robot.serialSeenMs = 0;
        }
    }
    notifyChanged();
#endif
}

void NearbyRobotsService::onServiceSeen(const mdns_detail::ServiceRecord& record) {
    QString host = record.address.isNull() ? record.host : record.address.toString();
    if (host.isEmpty()) {
        return;
    }
    // The instance label is the robot's name too (lower-cased by the
    // parser): the TXT "name" keeps its case, so it wins when present.
    QString name = record.txtName;
    if (name.isEmpty()) {
        name = record.instance.section(QLatin1Char('.'), 0, 0);
    }
    NearbyRobot& robot = upsert(name, QStringLiteral("tcp:") + host);
    robot.tcpHost = host;
    robot.tcpPort = record.port;
    robot.tcpSeenMs = nowMs();
    notifyChanged();
}

void NearbyRobotsService::prune() {
    const qint64 now = nowMs();
    bool changed = false;
    for (auto it = m_robots.begin(); it != m_robots.end();) {
        NearbyRobot& robot = it.value();
        // Deep-scan finds are not re-confirmed by anything passive: they
        // stay until the next scan or until the app closes.
        if (!robot.foundByDeepScan) {
            if (robot.tcpSeenMs > 0 && now - robot.tcpSeenMs > kTcpExpiryMs) {
                robot.tcpHost.clear();
                robot.tcpPort = 0;
                robot.tcpSeenMs = 0;
                changed = true;
            }
            if (robot.bleSeenMs > 0 && now - robot.bleSeenMs > kBleExpiryMs) {
                robot.bleAddress.clear();
                robot.bleSeenMs = 0;
                changed = true;
            }
        }
        if (robot.serialPort.isEmpty() && robot.tcpHost.isEmpty() && robot.bleAddress.isEmpty()) {
            it = m_robots.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        notifyChanged();
    }
}

// --- Deep scan --------------------------------------------------------------

void NearbyRobotsService::startDeepScan() {
    if (m_deepScanRunning) {
        return;
    }
    m_deepScanRunning = true;
    m_deepFound = 0;
    m_deepDone = 0;
    m_deepTotal = 0;
    const QSet<QString> busy = m_busyTargets ? m_busyTargets() : QSet<QString>();
    // A new scan re-checks everything an older one found.
    for (NearbyRobot& robot : m_robots) {
        robot.foundByDeepScan = false;
    }
    refreshSerialPorts();
    startSerialProbes(busy);
    startTcpSweep(busy);
    m_deepTimer->start();
    emit deepScanProgress(m_deepDone, m_deepTotal);
    maybeFinishDeepScan();
}

void NearbyRobotsService::cancelDeepScan() {
    m_sweepQueue.clear();
    const auto sockets = m_sweepSockets.keys();
    m_sweepSockets.clear();
    for (QTcpSocket* socket : sockets) {
        socket->abort();
        socket->deleteLater();
    }
    const auto probes = m_probes;
    m_probes.clear();
    for (const Probe& probe : probes) {
        if (probe.connection) {
            probe.connection->disconnectFrom();
            probe.connection->deleteLater();
        }
    }
    if (m_deepTimer != nullptr) {
        m_deepTimer->stop();
    }
    if (m_deepScanRunning) {
        m_deepScanRunning = false;
        emit deepScanFinished(m_deepFound);
    }
}

void NearbyRobotsService::startSerialProbes(const QSet<QString>& busy) {
#ifdef TRACEVIEW_ENABLE_SERIAL
    QSet<QString> identified;
    for (const NearbyRobot& robot : std::as_const(m_robots)) {
        if (!robot.serialPort.isEmpty() && (!robot.name.isEmpty() || robot.dongle)) {
            identified.insert(robot.serialPort);
        }
    }
    QStringList candidates;
#ifdef Q_OS_ANDROID
    for (const SerialPortOption& port : AndroidUsbSerialTransport::availablePorts()) {
        candidates.append(port.name);
    }
#else
    for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts()) {
        // USB ports only: COM1 and Bluetooth's virtual ports are never a
        // robot, and some of them stall when opened.
        if (!info.hasVendorIdentifier() ||
            SerialManager::isDuplicateDialInPort(info.portName())) {
            continue;
        }
        candidates.append(info.portName());
    }
#endif
    for (const QString& port : std::as_const(candidates)) {
        if (identified.contains(port) || busy.contains(port)) {
            continue;
        }
        Probe probe;
        probe.serialPort = port;
        ++m_deepTotal;
        startProbe(probe);
    }
#else
    Q_UNUSED(busy);
#endif
}

void NearbyRobotsService::startTcpSweep(const QSet<QString>& busy) {
    QSet<quint16> ports{kDefaultBtpPort};
    if (m_extraTcpPorts) {
        ports.unite(m_extraTcpPorts());
    }
    QSet<QString> known = busy;
    for (const NearbyRobot& robot : std::as_const(m_robots)) {
        if (!robot.tcpHost.isEmpty()) {
            known.insert(robot.tcpHost);
        }
    }
    QSet<quint32> networks;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLinkLocal()) {
                continue;
            }
            // Only the /24 around this computer's own address, even on a
            // wider network: 254 addresses per interface keeps it short.
            const quint32 own = ip.toIPv4Address();
            const quint32 base = own & 0xFFFFFF00u;
            if (networks.contains(base)) {
                continue;
            }
            networks.insert(base);
            for (quint32 host = 1; host < 255; ++host) {
                const quint32 address = base | host;
                if (address == own) {
                    continue;
                }
                const QString text = QHostAddress(address).toString();
                if (known.contains(text)) {
                    continue;
                }
                for (quint16 port : std::as_const(ports)) {
                    m_sweepQueue.append({text, port});
                }
            }
        }
    }
    m_deepTotal += m_sweepQueue.size();
    pumpTcpSweep();
}

void NearbyRobotsService::pumpTcpSweep() {
    while (m_sweepSockets.size() < kSweepParallel && !m_sweepQueue.isEmpty()) {
        const Sweep sweep = m_sweepQueue.takeFirst();
        auto* socket = new QTcpSocket(this);
        m_sweepSockets.insert(socket, sweep);
        const auto done = [this, socket](bool open) {
            const auto it = m_sweepSockets.find(socket);
            if (it == m_sweepSockets.end()) {
                return;  // already settled (or the scan was cancelled)
            }
            const Sweep target = it.value();
            m_sweepSockets.erase(it);
            socket->abort();
            socket->deleteLater();
            ++m_deepDone;
            if (open) {
                // Something listens there: ask it who it is. Counted as one
                // more step so the progress bar does not hit 100% early.
                Probe probe;
                probe.tcpHost = target.host;
                probe.tcpPort = target.port;
                ++m_deepTotal;
                startProbe(probe);
            }
            emit deepScanProgress(m_deepDone, m_deepTotal);
            QMetaObject::invokeMethod(this, &NearbyRobotsService::pumpTcpSweep,
                                      Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, &NearbyRobotsService::maybeFinishDeepScan,
                                      Qt::QueuedConnection);
        };
        connect(socket, &QTcpSocket::connected, this, [done] { done(true); });
        connect(socket, &QTcpSocket::errorOccurred, this, [done] { done(false); });
        QTimer::singleShot(kSweepConnectTimeoutMs, socket, [done] { done(false); });
        socket->connectToHost(sweep.host, sweep.port);
    }
}

void NearbyRobotsService::startProbe(const Probe& request) {
    Probe probe = request;
    const bool tcp = !probe.tcpHost.isEmpty();
    auto* connection =
        new DeviceConnection(CommType::Btp, tcp ? TransportType::Tcp : TransportType::Serial, this);
    probe.connection = connection;
    probe.deadlineMs = nowMs() + kProbeTimeoutMs;
    m_probes.insert(connection, probe);
    connect(connection, &DeviceConnection::connectionPhaseChanged, this,
            [this, connection](ConnectionPhase phase) {
                const auto it = m_probes.find(connection);
                if (it == m_probes.end() || phase != ConnectionPhase::Ready || it->ready) {
                    return;
                }
                it->ready = true;
                it->deadlineMs = std::min(it->deadlineMs, nowMs() + kProbeNameGraceMs);
            });
    connect(connection, &DeviceConnection::deviceInfoReported, this,
            [this, connection](const QVector<DeviceInfoRecord>& info) {
                const QString name = nameFromInfo(info);
                if (!name.isEmpty()) {
                    // Queued: the connection must not be deleted inside its
                    // own signal.
                    QMetaObject::invokeMethod(
                        this, [this, connection, name] { finishProbe(connection, name); },
                        Qt::QueuedConnection);
                }
            });
    if (tcp) {
        connection->connectToTcp(probe.tcpHost, probe.tcpPort, QByteArray());
    } else {
        connection->connectTo(probe.serialPort, kDefaultSerialBaudRate);
    }
}

void NearbyRobotsService::finishProbe(DeviceConnection* connection, const QString& name) {
    const auto it = m_probes.find(connection);
    if (it == m_probes.end()) {
        return;
    }
    const Probe probe = it.value();
    m_probes.erase(it);
    connection->disconnectFrom();
    connection->deleteLater();
    ++m_deepDone;

    // A session that came up counts even without a name; one that never did
    // was not a robot (or was busy) and is left out.
    if (probe.ready || !name.isEmpty()) {
        const bool tcp = !probe.tcpHost.isEmpty();
        const QString fallback = tcp ? QStringLiteral("tcp:") + probe.tcpHost
                                     : QStringLiteral("serial:") + probe.serialPort;
        NearbyRobot& robot = upsert(name, fallback);
        robot.foundByDeepScan = true;
        if (tcp) {
            robot.tcpHost = probe.tcpHost;
            robot.tcpPort = probe.tcpPort;
            robot.tcpSeenMs = nowMs();
        } else {
            robot.serialPort = probe.serialPort;
            robot.serialSeenMs = nowMs();
        }
        ++m_deepFound;
        notifyChanged();
    }
    emit deepScanProgress(m_deepDone, m_deepTotal);
    maybeFinishDeepScan();
}

void NearbyRobotsService::onDeepScanTick() {
    const qint64 now = nowMs();
    QVector<DeviceConnection*> expired;
    for (auto it = m_probes.cbegin(); it != m_probes.cend(); ++it) {
        if (now >= it->deadlineMs) {
            expired.append(it.key());
        }
    }
    for (DeviceConnection* connection : expired) {
        finishProbe(connection, QString());
    }
}

void NearbyRobotsService::maybeFinishDeepScan() {
    if (!m_deepScanRunning || !m_sweepQueue.isEmpty() || !m_sweepSockets.isEmpty() ||
        !m_probes.isEmpty()) {
        return;
    }
    m_deepTimer->stop();
    m_deepScanRunning = false;
    emit deepScanFinished(m_deepFound);
}

}  // namespace traceview
