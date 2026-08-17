#include <QtTest>

#include "core/serialmanager.h"

using traceview::SerialManager;

namespace {

class TestSerialManager : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnected();
    void closeWithoutOpenIsNoop();
    void writeWhileDisconnectedReturnsFalse();
    void openWithInvalidPortNameFailsAndEmitsError();
    void availablePortsDoesNotCrash();
};

void TestSerialManager::startsDisconnected() {
    SerialManager manager;
    QVERIFY(!manager.isConnected());
    QVERIFY(manager.portName().isEmpty());
}

void TestSerialManager::closeWithoutOpenIsNoop() {
    SerialManager manager;
    QSignalSpy stateSpy(&manager, &SerialManager::connectionStateChanged);

    manager.close();

    QVERIFY(!manager.isConnected());
    QCOMPARE(stateSpy.count(), 0);
}

void TestSerialManager::writeWhileDisconnectedReturnsFalse() {
    SerialManager manager;
    QVERIFY(!manager.write(QByteArrayLiteral("hello\n")));
}

void TestSerialManager::openWithInvalidPortNameFailsAndEmitsError() {
    SerialManager manager;
    QSignalSpy errorSpy(&manager, &SerialManager::errorOccurred);
    QSignalSpy stateSpy(&manager, &SerialManager::connectionStateChanged);

    const bool opened = manager.open("__traceview_no_such_port__", 9600);

    QVERIFY(!opened);
    QVERIFY(!manager.isConnected());
    QCOMPARE(stateSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
}

void TestSerialManager::availablePortsDoesNotCrash() {
    SerialManager manager;
    // Environment-dependent (no real ports on a CI box), just verify the
    // call is safe and returns without asserting on content.
    (void)manager.availablePorts();
    QVERIFY(true);
}

} // namespace

QTEST_MAIN(TestSerialManager)
#include "test_serialmanager.moc"
