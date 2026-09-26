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
    void addressesAreDialedDirectlyAndEverythingElseIsAName();
    void nameMatchIsCaseInsensitiveAndNeverEmpty();
    void closeDuringNameLookupDoesNotCrash();
    void nameCacheTakesOnlyAddressesAndKeepsTheFirst();
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

void TestBleTransport::addressesAreDialedDirectlyAndEverythingElseIsAName() {
    // MACs (either case) and the per-host UUIDs macOS/iOS hand out.
    QVERIFY(BleTransport::isPlatformAddress(QStringLiteral("AA:BB:CC:DD:EE:FF")));
    QVERIFY(BleTransport::isPlatformAddress(QStringLiteral(" aa:bb:cc:dd:ee:01 ")));
    QVERIFY(BleTransport::isPlatformAddress(
        QStringLiteral("{547a1aae-676e-4b68-8e20-bace26cd0726}")));
    QVERIFY(BleTransport::isPlatformAddress(
        QStringLiteral("547a1aae-676e-4b68-8e20-bace26cd0726")));

    // Anything else is looked up by name, like a TCP hostname.
    QVERIFY(!BleTransport::isPlatformAddress(QStringLiteral("BallyRobot")));
    QVERIFY(!BleTransport::isPlatformAddress(QStringLiteral("ballyrobot-2")));
    QVERIFY(!BleTransport::isPlatformAddress(QStringLiteral("Robo da Bancada")));
}

void TestBleTransport::nameMatchIsCaseInsensitiveAndNeverEmpty() {
    QVERIFY(BleTransport::bleNameMatches(QStringLiteral("BallyRobot"), QStringLiteral("ballyrobot")));
    QVERIFY(BleTransport::bleNameMatches(QStringLiteral(" BallyRobot "), QStringLiteral("BALLYROBOT")));
    QVERIFY(!BleTransport::bleNameMatches(QStringLiteral("BallyRobot2"), QStringLiteral("BallyRobot")));
    // A peripheral whose name has not arrived yet is not whatever was asked for.
    QVERIFY(!BleTransport::bleNameMatches(QString(), QString()));
    QVERIFY(!BleTransport::bleNameMatches(QString(), QStringLiteral("BallyRobot")));
}

void TestBleTransport::nameCacheTakesOnlyAddressesAndKeepsTheFirst() {
    QCOMPARE(BleTransport::cachedAddressForName(QStringLiteral("CacheBot")), QString());
    BleTransport::rememberAddressForName(QStringLiteral("CacheBot"), QStringLiteral("CacheBot"));
    QCOMPARE(BleTransport::cachedAddressForName(QStringLiteral("CacheBot")), QString());
    BleTransport::rememberAddressForName(QStringLiteral(" CacheBot "),
                                         QStringLiteral("14:C1:9F:44:24:86"));
    QCOMPARE(BleTransport::cachedAddressForName(QStringLiteral("cachebot")),
             QStringLiteral("14:C1:9F:44:24:86"));
    // A hint never replaces what is already known.
    BleTransport::rememberAddressForName(QStringLiteral("CacheBot"),
                                         QStringLiteral("AA:BB:CC:DD:EE:FF"));
    QCOMPARE(BleTransport::cachedAddressForName(QStringLiteral("CacheBot")),
             QStringLiteral("14:C1:9F:44:24:86"));
}

void TestBleTransport::closeDuringNameLookupDoesNotCrash() {
    // A name starts a scan instead of a controller; closing or re-opening in
    // the middle of it must tear the scan down cleanly, and a stale lookup
    // must not report anything afterwards.
    BleTransport transport;
    QVERIFY(transport.open(QStringLiteral("BallyRobot")));
    QVERIFY(transport.open(QStringLiteral("OtherRobot")));
    transport.close();
    QVERIFY(!transport.isConnected());
    QTest::qWait(50);
    QCOMPARE(transport.address(), QStringLiteral("OtherRobot"));
}

QTEST_MAIN(TestBleTransport)
#include "test_bletransport.moc"
