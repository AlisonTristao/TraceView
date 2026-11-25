#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

class QTcpSocket;
class QTimer;

namespace traceview {

class DeviceConnection;
class MdnsBrowser;
#ifdef TRACEVIEW_ENABLE_BLE
class BleDiscoveryService;
#endif
namespace mdns_detail {
struct ServiceRecord;
}

// One robot (or dongle) seen around this computer, by any link. Robots that
// report a name are merged by it across links: the same robot on USB, Wi-Fi
// and BLE is one entry with three targets.
struct NearbyRobot {
    // Lower-cased name when there is one; otherwise "serial:COM7",
    // "tcp:192.168.0.12" or "ble:<address>".
    QString key;
    QString name;
    QString serialPort;
    bool dongle = false;
    // An IPv4 address when known, else the robot's "<host>.local".
    QString tcpHost;
    quint16 tcpPort = 0;
    QString bleAddress;
    bool foundByDeepScan = false;
    // Last time each link reported it (ms since epoch, 0 = never).
    qint64 serialSeenMs = 0;
    qint64 tcpSeenMs = 0;
    qint64 bleSeenMs = 0;
};

// The "Nearby robots" list of the Home page.
//
// Two very different costs, kept apart on purpose:
//
//  - The passive watch (setActive) touches no robot. It reads the OS's list
//    of serial ports (a robot names its USB port after itself), browses mDNS
//    for "_btp._tcp" (every bally_OS robot on Wi-Fi advertises it) and runs
//    one short BLE scan when Bluetooth permission is already granted. It
//    runs only while the Home page is visible.
//
//  - The deep scan (startDeepScan) is only ever started by a button: it
//    opens every unidentified USB serial port and says HELLO (opening a port
//    can reset an ESP32), and tries TCP port 44300 on every address of the
//    computer's /24 networks, saying HELLO to whatever answers (a robot
//    takes one client at a time, and network monitors may flag a sweep).
class NearbyRobotsService : public QObject {
    Q_OBJECT

public:
    explicit NearbyRobotsService(QObject* parent = nullptr);
    ~NearbyRobotsService() override;

    // Ports and hosts the app itself is using right now: a deep scan leaves
    // them alone (a busy port cannot be opened, and a robot already talking
    // to us would only be disturbed). Asked for at the start of each scan.
    void setBusyTargetsProvider(std::function<QSet<QString>()> provider) {
        m_busyTargets = std::move(provider);
    }
    // Extra TCP ports to try besides 44300 (those the project's devices use).
    void setExtraTcpPortsProvider(std::function<QSet<quint16>()> provider) {
        m_extraTcpPorts = std::move(provider);
    }

    void setActive(bool active);
    bool isActive() const {
        return m_active;
    }

    // One BLE scan now -- may show the OS's Bluetooth permission prompt.
    void scanBluetooth();
    bool bluetoothScanning() const;
    static bool bluetoothAvailable();

    void startDeepScan();
    void cancelDeepScan();
    bool deepScanRunning() const {
        return m_deepScanRunning;
    }

    QVector<NearbyRobot> robots() const;

signals:
    void robotsChanged();
    void bluetoothScanningChanged(bool scanning);
    void deepScanProgress(int done, int total);
    void deepScanFinished(int found);

private:
    struct Probe {
        QPointer<DeviceConnection> connection;
        QString serialPort;
        QString tcpHost;
        quint16 tcpPort = 0;
        bool ready = false;
        qint64 deadlineMs = 0;
    };

    void refreshSerialPorts();
    void onServiceSeen(const mdns_detail::ServiceRecord& record);
    void prune();
    NearbyRobot& upsert(const QString& name, const QString& fallbackKey);
    void notifyChanged();

    void startSerialProbes(const QSet<QString>& busy);
    void startTcpSweep(const QSet<QString>& busy);
    void pumpTcpSweep();
    void startProbe(const Probe& probe);
    void finishProbe(DeviceConnection* connection, const QString& name);
    void onDeepScanTick();
    void maybeFinishDeepScan();

    QHash<QString, NearbyRobot> m_robots;
    std::function<QSet<QString>()> m_busyTargets;
    std::function<QSet<quint16>()> m_extraTcpPorts;
    bool m_active = false;
    QTimer* m_serialTimer = nullptr;
    MdnsBrowser* m_mdns = nullptr;
    qint64 m_lastPassiveBleMs = 0;
#ifdef TRACEVIEW_ENABLE_BLE
    BleDiscoveryService* m_ble = nullptr;
    QTimer* m_bleStopTimer = nullptr;
#endif

    // Deep scan state.
    bool m_deepScanRunning = false;
    int m_deepFound = 0;
    int m_deepTotal = 0;
    int m_deepDone = 0;
    struct Sweep {
        QString host;
        quint16 port = 0;
    };
    QVector<Sweep> m_sweepQueue;
    QHash<QTcpSocket*, Sweep> m_sweepSockets;
    QHash<DeviceConnection*, Probe> m_probes;
    QTimer* m_deepTimer = nullptr;
};

}  // namespace traceview
