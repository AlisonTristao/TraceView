#include <QtTest>

#include "core/blediscoveryservice.h"
#include "core/bletransport.h"

using traceview::BleDiscoveryService;
using traceview::BleTransport;

// T27/T28-T30: what's testable without real BLE hardware or a peripheral
// simulator, mirroring test_tcptransport.cpp's own "can't reach real
// hardware in CI" coverage -- construction, own-input validation and clean
// teardown. A real scan or GATT round trip needs either T36's simulated
// layer or T37's real hardware, neither of which this suite has access to.
class TestBleTransport : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnectedAndRejectsEmptyAddress();
    void closeWithNothingOpenDoesNotCrash();
    void discoveryServiceStartStopLifecycle();
    void discoveryServiceFiltersByBtpUuid();
};

void TestBleTransport::startsDisconnectedAndRejectsEmptyAddress() {
    BleTransport transport;
    QVERIFY(!transport.isConnected());
    QCOMPARE(transport.pendingFrameCount(), 0);
    QVERIFY(!transport.open(QString()));
    QVERIFY(!transport.open(QStringLiteral("   ")));
    QVERIFY(!transport.write("not connected"));
}

void TestBleTransport::closeWithNothingOpenDoesNotCrash() {
    BleTransport transport;
    transport.close();
    transport.close();
    QVERIFY(!transport.isConnected());
}

void TestBleTransport::discoveryServiceStartStopLifecycle() {
    BleDiscoveryService discovery;
    QVERIFY(!discovery.isScanning());

    discovery.start();
    QVERIFY(discovery.isScanning());

    // Restarting while already scanning tears the previous agent down and
    // begins a fresh one -- see BleDiscoveryService::start()'s own comment.
    // Must not crash or leave two agents running.
    discovery.start();
    QVERIFY(discovery.isScanning());

    discovery.stop();
    QVERIFY(!discovery.isScanning());

    // stop() while already stopped is a no-op, not an error.
    discovery.stop();
    QVERIFY(!discovery.isScanning());
}

void TestBleTransport::discoveryServiceFiltersByBtpUuid() {
    // The one thing every compatible peripheral advertises
    // (fragmentation-and-transports.md section 8) -- fixed and documented,
    // so a change here is a deliberate protocol change, not an accident.
    QCOMPARE(BleDiscoveryService::btpServiceUuid(),
             QBluetoothUuid(QStringLiteral("547a1aae-676e-4b68-8e20-bace26cd0726")));
}

QTEST_MAIN(TestBleTransport)
#include "test_bletransport.moc"
