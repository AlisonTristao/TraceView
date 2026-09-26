#include <QTcpServer>
#include <QtTest>

#include "backend/backend.h"
#include "core/deviceconnection.h"
#include "core/serialmanager.h"
#include "core/usbhidmanager.h"
#include "devices/device.h"

using traceview::Backend;
using traceview::ConnectionPhase;
using traceview::CommType;
using traceview::DeviceConnection;
using traceview::LineTerminator;
using traceview::TransportType;

namespace {

class TestDeviceConnection : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnectedWithABackend();
    void connectToWithEmptyPortNameStaysDisconnected();
    void connectToWithInvalidPortNameFailsWithoutCrashing();
    void disconnectFromStopsRetryingAfterAFailedAttempt();
    void setLineTerminatorForwardsToSerialManager();
    void usbHidTransportBuildsUsbHidManagerNotSerialManager();
    void setLineTerminatorIsNoopForUsbHidTransport();
    void exposesAsyncConnectionPhases();
    void disconnectInvalidatesAnActiveAttempt();
    void availabilitySuspendsAndResumesWithoutReplayingIntentAsCommands();
    void tcpTransportBuildsNeitherSerialNorUsbHidManager();
    void connectToTcpWithEmptyHostStaysDisconnected();
    void connectToTcpWithZeroPortStaysDisconnected();
    void connectToTcpReachesNegotiatingBtpAgainstALocalServer();
    void connectToTcpSessionRecoversWhenHelloResultNeverArrives();
#ifdef TRACEVIEW_ENABLE_BLE
    void bleTransportBuildsNeitherSerialNorUsbHidManager();
    void connectToBleWithEmptyAddressStaysDisconnected();
#endif
};

void TestDeviceConnection::startsDisconnectedWithABackend() {
    DeviceConnection connection(CommType::Btp);
    QVERIFY(!connection.isConnected());
    QCOMPARE(connection.transportType(), TransportType::Serial);
    QVERIFY(connection.serialTransport() != nullptr);
    QVERIFY(connection.usbHidManager() == nullptr);
    // Only CommType::Btp exists today -- the ctor must always build a
    // concrete Backend for it (see the switch in deviceconnection.cpp).
    QVERIFY(connection.backend() != nullptr);
}

void TestDeviceConnection::connectToWithEmptyPortNameStaysDisconnected() {
    DeviceConnection connection(CommType::Btp);
    QSignalSpy stateSpy(&connection, &DeviceConnection::connectionStateChanged);

    // Empty portName means "not configured" -- must never attempt open().
    connection.connectTo(QString(), 9600);

    QVERIFY(!connection.isConnected());
    QCOMPARE(stateSpy.count(), 0);
}

void TestDeviceConnection::connectToWithInvalidPortNameFailsWithoutCrashing() {
    DeviceConnection connection(CommType::Btp);

    connection.connectTo("__traceview_no_such_port__", 9600);

    // The immediate attempt made from within connectTo() fails synchronously
    // (same as SerialManager::open() on a bad name, see test_serialmanager),
    // so this doesn't need to wait out the retry timer to observe the result.
    QVERIFY(!connection.isConnected());
}

void TestDeviceConnection::disconnectFromStopsRetryingAfterAFailedAttempt() {
    DeviceConnection connection(CommType::Btp);
    connection.connectTo("__traceview_no_such_port__", 9600);
    QVERIFY(!connection.isConnected());

    // Must not crash or reassert intent once explicitly disconnected -- the
    // retry timer firing afterward (if it weren't stopped) would otherwise
    // keep calling open() on a target the caller asked to walk away from.
    connection.disconnectFrom();
    QVERIFY(!connection.isConnected());
}

void TestDeviceConnection::setLineTerminatorForwardsToSerialManager() {
    DeviceConnection connection(CommType::Btp);
    connection.setLineTerminator(int(LineTerminator::CrLf));
    QCOMPARE(connection.serialTransport()->lineTerminator(), LineTerminator::CrLf);
}

void TestDeviceConnection::usbHidTransportBuildsUsbHidManagerNotSerialManager() {
    DeviceConnection connection(CommType::Btp, TransportType::UsbHid);
    QCOMPARE(connection.transportType(), TransportType::UsbHid);
    QVERIFY(connection.serialTransport() == nullptr);
    QVERIFY(connection.usbHidManager() != nullptr);
    QVERIFY(connection.backend() != nullptr);
    QVERIFY(!connection.isConnected());
}

void TestDeviceConnection::setLineTerminatorIsNoopForUsbHidTransport() {
    // No SerialManager to forward to (raw-text control commands don't exist
    // over USB HID, see device.h's Device::lineTerminator comment) -- must
    // not crash.
    DeviceConnection connection(CommType::Btp, TransportType::UsbHid);
    connection.setLineTerminator(int(LineTerminator::CrLf));
    QVERIFY(connection.serialTransport() == nullptr);
}

void TestDeviceConnection::exposesAsyncConnectionPhases() {
    DeviceConnection connection(CommType::Btp);
    QSignalSpy phaseSpy(&connection, &DeviceConnection::connectionPhaseChanged);

    QCOMPARE(connection.connectionPhase(), ConnectionPhase::Disconnected);
    connection.connectTo("__traceview_no_such_port__", 9600);

    QVERIFY(phaseSpy.count() >= 2);
    QCOMPARE(phaseSpy.at(0).at(0).value<ConnectionPhase>(), ConnectionPhase::Connecting);
    QCOMPARE(phaseSpy.at(1).at(0).value<ConnectionPhase>(),
             ConnectionPhase::PreparingTransport);
    QCOMPARE(connection.connectionPhase(), ConnectionPhase::Disconnected);
}

void TestDeviceConnection::disconnectInvalidatesAnActiveAttempt() {
    DeviceConnection connection(CommType::Btp);
    QSignalSpy phaseSpy(&connection, &DeviceConnection::connectionPhaseChanged);

    connection.connectTo("__traceview_no_such_port__", 9600);
    connection.disconnectFrom();

    QCOMPARE(connection.connectionPhase(), ConnectionPhase::Disconnected);
    QVERIFY(!connection.wantsConnection());
    const int phasesAfterDisconnect = phaseSpy.count();
    QTest::qWait(50);
    QCOMPARE(phaseSpy.count(), phasesAfterDisconnect);
}

void TestDeviceConnection::availabilitySuspendsAndResumesWithoutReplayingIntentAsCommands() {
    DeviceConnection connection(CommType::Btp);
    QSignalSpy availabilitySpy(&connection, &DeviceConnection::availabilityChanged);

    connection.connectTo("__traceview_no_such_port__", 9600);
    connection.suspend();

    QVERIFY(!connection.isAvailable());
    QVERIFY(connection.wantsConnection());
    QVERIFY(!connection.isConnected());
    QCOMPARE(connection.connectionPhase(), ConnectionPhase::Disconnected);

    connection.resume();
    QVERIFY(connection.isAvailable());
    QCOMPARE(connection.wantsConnection(), true);
    QCOMPARE(availabilitySpy.count(), 2);
    QCOMPARE(availabilitySpy.at(0).at(0).toBool(), false);
    QCOMPARE(availabilitySpy.at(1).at(0).toBool(), true);
}

void TestDeviceConnection::tcpTransportBuildsNeitherSerialNorUsbHidManager() {
    // Regression: the ctor's switch on TransportType used to have no case
    // for Tcp at all, leaving the base Transport* null while the rest of
    // the class (session-start mode, the write-rejection lambda, ...)
    // already treated Tcp as fully supported -- the first connectTo-style
    // call then dereferenced a null transport. This just needs to survive
    // construction and report the right shape.
    DeviceConnection connection(CommType::Btp, TransportType::Tcp);
    QCOMPARE(connection.transportType(), TransportType::Tcp);
    QVERIFY(connection.serialTransport() == nullptr);
    QVERIFY(connection.usbHidManager() == nullptr);
    QVERIFY(connection.backend() != nullptr);
    QVERIFY(!connection.isConnected());
}

void TestDeviceConnection::connectToTcpWithEmptyHostStaysDisconnected() {
    DeviceConnection connection(CommType::Btp, TransportType::Tcp);
    QSignalSpy stateSpy(&connection, &DeviceConnection::connectionStateChanged);

    // Empty host means "not configured" -- must never attempt open(), same
    // contract as connectTo() with an empty target.
    connection.connectToTcp(QString(), 44300, QByteArray());

    QVERIFY(!connection.isConnected());
    QVERIFY(!connection.wantsConnection());
    QCOMPARE(stateSpy.count(), 0);
}

void TestDeviceConnection::connectToTcpWithZeroPortStaysDisconnected() {
    DeviceConnection connection(CommType::Btp, TransportType::Tcp);

    connection.connectToTcp(QStringLiteral("127.0.0.1"), 0, QByteArray());

    QVERIFY(!connection.isConnected());
    QVERIFY(!connection.wantsConnection());
}

void TestDeviceConnection::connectToTcpReachesNegotiatingBtpAgainstALocalServer() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    DeviceConnection connection(CommType::Btp, TransportType::Tcp);
    QSignalSpy phaseSpy(&connection, &DeviceConnection::connectionPhaseChanged);

    connection.connectToTcp(QStringLiteral("127.0.0.1"), server.serverPort(), QByteArray());

    // No BTP responder on the other end, so this never reaches Ready, but
    // the transport connecting must promote the phase past PreparingTransport
    // instead of the connectionStateChanged(true) getting silently dropped --
    // which is exactly what happened while TcpTransport ran its own
    // independent reconnect loop underneath DeviceConnection's phase gate.
    QTRY_COMPARE_WITH_TIMEOUT(connection.connectionPhase(), ConnectionPhase::NegotiatingBtp, 2000);
}

// T24 "ausência de resposta pela sessão BTP": a server that accepts the TCP
// connection (so the transport itself is healthy) but never answers the
// HELLO that SessionStartMode::DirectBtp sends the instant the socket
// connects. Detecting that is BtpBackend's job, not TcpTransport's --
// BtpBackend::kHelloTimeoutMs (3000ms, btpbackend.h) drives m_node's own
// watchdog; after kMaxHelloAttempts (3) unanswered HELLOs -- ~9 s -- it
// emits sessionRecoveryNeeded(), which
// DeviceConnection's ctor already wires to close the transport while still
// in NegotiatingBtp (see the sessionRecoveryNeeded lambda in
// deviceconnection.cpp). This is the one new test in this file that
// deliberately waits out a real timeout rather than a synthetic one --
// kHelloTimeoutMs has no test-injection hook (unlike TcpTransport's own
// connect/reconnect delays), so shortening it would mean touching
// btpbackend.h's session-watchdog constant for every transport, not just
// TCP, which is out of scope here.
void TestDeviceConnection::connectToTcpSessionRecoversWhenHelloResultNeverArrives() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    DeviceConnection connection(CommType::Btp, TransportType::Tcp);

    connection.connectToTcp(QStringLiteral("127.0.0.1"), server.serverPort(), QByteArray());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(connection.connectionPhase(), ConnectionPhase::NegotiatingBtp, 2000);

    // Silence from here on -- no HELLO_RESULT ever comes back. The session
    // watchdog must notice on its own and fall back to Disconnected (from
    // which the retry timer takes over) instead of sitting in
    // NegotiatingBtp forever.
    QTRY_COMPARE_WITH_TIMEOUT(connection.connectionPhase(), ConnectionPhase::Disconnected, 12000);
    // Recovery, not disconnectFrom(): the caller's intent to be connected
    // must survive so the retry timer actually retries.
    QVERIFY(connection.wantsConnection());

    peer->deleteLater();
}

#ifdef TRACEVIEW_ENABLE_BLE
void TestDeviceConnection::bleTransportBuildsNeitherSerialNorUsbHidManager() {
    // Same regression shape as tcpTransportBuildsNeitherSerialNorUsbHidManager
    // above, for Ble's own ctor case (deviceconnection.cpp).
    DeviceConnection connection(CommType::Btp, TransportType::Ble);
    QCOMPARE(connection.transportType(), TransportType::Ble);
    QVERIFY(connection.serialTransport() == nullptr);
    QVERIFY(connection.usbHidManager() == nullptr);
    QVERIFY(connection.backend() != nullptr);
    QVERIFY(!connection.isConnected());
}

void TestDeviceConnection::connectToBleWithEmptyAddressStaysDisconnected() {
    DeviceConnection connection(CommType::Btp, TransportType::Ble);
    QSignalSpy stateSpy(&connection, &DeviceConnection::connectionStateChanged);

    // Empty address means "not configured" -- must never attempt
    // BleTransport::open() (which would need real Bluetooth hardware/a real
    // peripheral, neither available in CI -- see test_bletransport.cpp's own
    // comment), same contract as connectTo()/connectToTcp() with nothing
    // configured.
    connection.connectToBle(QString(), QByteArray());

    QVERIFY(!connection.isConnected());
    QVERIFY(!connection.wantsConnection());
    QCOMPARE(stateSpy.count(), 0);
}
#endif

}  // namespace

QTEST_MAIN(TestDeviceConnection)
#include "test_deviceconnection.moc"
