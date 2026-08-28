#include <QtTest>

#include "core/serialmanager.h"

using traceview::SerialManager;

namespace {

class TestSerialManager : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnected();
    void closeWithoutOpenIsNoop();
    void drainWritesWithoutOpenFails();
    void writeWhileDisconnectedReturnsFalse();
    void openWithInvalidPortNameFailsAndEmitsError();
    void openAt1200BaudIsFoldedUpWithAWarning();
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

void TestSerialManager::drainWritesWithoutOpenFails() {
    SerialManager manager;
    QVERIFY(!manager.drainWrites(10));
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

void TestSerialManager::openAt1200BaudIsFoldedUpWithAWarning() {
    // 1200 baud is the ESP32-S3 CDC's reset-to-bootloader shortcut, never a
    // working data rate -- open() must substitute a real rate and say so
    // rather than bootloader-loop the dongle. The port name is invalid so the
    // open itself still fails; what matters is the extra warning carrying
    // "1200" that fires before Qt's own open-failure error.
    SerialManager manager;
    QSignalSpy errorSpy(&manager, &SerialManager::errorOccurred);

    const bool opened = manager.open("__traceview_no_such_port__", 1200);

    QVERIFY(!opened);
    QVERIFY(errorSpy.count() >= 1);
    bool warnedAbout1200 = false;
    for (const QList<QVariant>& call : errorSpy) {
        if (call.at(0).toString().contains("1200")) {
            warnedAbout1200 = true;
        }
    }
    QVERIFY(warnedAbout1200);
}

void TestSerialManager::availablePortsDoesNotCrash() {
    SerialManager manager;
    // Environment-dependent (no real ports on a CI box), just verify the
    // call is safe and returns without asserting on content.
    (void)manager.availablePorts();
    QVERIFY(true);
}

}  // namespace

QTEST_MAIN(TestSerialManager)
#include "test_serialmanager.moc"
