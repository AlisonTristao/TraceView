#include <QtTest>

#include "backend/backend.h"
#include "core/deviceconnection.h"
#include "core/serialmanager.h"
#include "devices/device.h"

using traceview::Backend;
using traceview::CommType;
using traceview::DeviceConnection;
using traceview::LineTerminator;

namespace {

class TestDeviceConnection : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnectedWithABackend();
    void connectToWithEmptyPortNameStaysDisconnected();
    void connectToWithInvalidPortNameFailsWithoutCrashing();
    void disconnectFromStopsRetryingAfterAFailedAttempt();
    void setLineTerminatorForwardsToSerialManager();
};

void TestDeviceConnection::startsDisconnectedWithABackend() {
    DeviceConnection connection(CommType::Btp);
    QVERIFY(!connection.isConnected());
    QVERIFY(connection.serialManager() != nullptr);
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
    QCOMPARE(connection.serialManager()->lineTerminator(), LineTerminator::CrLf);
}

} // namespace

QTEST_MAIN(TestDeviceConnection)
#include "test_deviceconnection.moc"
