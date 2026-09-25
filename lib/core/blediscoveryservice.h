#pragma once

#include <QBluetoothUuid>
#include <QObject>
#include <QString>

class QBluetoothDeviceDiscoveryAgent;
class QBluetoothDeviceInfo;
class QTimer;

namespace traceview {

// Scans for nearby BLE peripherals and reports only the ones advertising the
// BTP GATT service (TAREFAS_TCP_BLE_ANDROID.txt T04/T27) -- a bare "every BLE
// device nearby" list would be mostly noise (phones, headphones, unrelated
// beacons) and would let a user pick something that can never speak BTP at
// all. Filtering happens here, once, rather than in DeviceConfigDialog, so
// every future consumer of discovery gets the same rule.
//
// Deliberately owns no protocol state and does no GATT work itself -- see
// BleTransport for connecting to a picked result. This class only answers
// "what is out there," the same separation TcpTransport/TcpBtpServer keep
// from BtpSession.
class BleDiscoveryService : public QObject {
    Q_OBJECT

public:
    // The BTP v1 BLE service UUID (BTP/docs/fragmentation-and-transports.md
    // section 8, TAREFAS_TCP_BLE_ANDROID.txt T04's note). Every compatible
    // peripheral advertises this; nothing else this service reports should.
    static QBluetoothUuid btpServiceUuid();

    // One compatible peripheral found during a scan. `address` is a platform
    // BLE address (a MAC on most backends, an opaque per-pairing UUID on
    // some -- see QBluetoothDeviceInfo::deviceUuid()) -- a discovery hint
    // only, never an identity (see Device::bleAddress's own comment).
    struct DiscoveredDevice {
        QString address;
        QString name;
    };

    explicit BleDiscoveryService(QObject* parent = nullptr);
    ~BleDiscoveryService() override;

    // Starts (or restarts) a scan. Safe to call while already scanning --
    // the previous agent is torn down first, which is also how cancellation
    // is made to actually discard obsolete results (see stop()).
    void start();

    // Cancels a scan in progress. Destroys the underlying discovery agent
    // rather than merely asking it to stop, so any result already queued
    // internally by the platform backend cannot reach deviceDiscovered()
    // after this call returns -- a Qt object with no signal connections left
    // cannot emit anything, regardless of what the backend does with it
    // afterwards. A restart via start() begins a fresh scan from scratch.
    void stop();

    // True from start() on, including while the Bluetooth permission prompt
    // is still up (see blepermission.h), until stop() or a denied permission.
    bool isScanning() const {
        return m_agent != nullptr || m_awaitingPermission;
    }

signals:
    // One per compatible peripheral. The same physical device may be
    // reported more than once per scan (most platform backends re-emit on
    // every advertisement seen); callers that want a de-duplicated list
    // key on `address` themselves, since a discovery service has no
    // business deciding what its caller wants displayed.
    void deviceDiscovered(const traceview::BleDiscoveryService::DiscoveredDevice& device);
    void finished();
    void errorOccurred(const QString& message);

private:
    void startAgent();
    void onDeviceDiscovered(const QBluetoothDeviceInfo& info);
    void onAgentFinished();
    void onAgentError();
    void sweepDiscoveredDevices();

    QBluetoothDeviceDiscoveryAgent* m_agent = nullptr;
    // Re-checks m_agent->discoveredDevices() while a scan runs -- see
    // startAgent()'s comment for why deviceDiscovered() alone misses robots.
    QTimer* m_sweepTimer = nullptr;
    bool m_awaitingPermission = false;
    // Bumped by every start()/stop(), so a permission answer arriving after
    // either one is recognized as stale and dropped.
    quint64 m_generation = 0;
};

}  // namespace traceview
