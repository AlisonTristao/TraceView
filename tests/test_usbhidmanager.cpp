#include <QtTest>

#include "core/usbhidmanager.h"

using traceview::UsbHidManager;

namespace {

class TestUsbHidManager : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnected();
    void closeWithoutOpenIsNoop();
    void writeWhileDisconnectedReturnsFalse();
    void openWithEmptyPathFailsWithoutCrashing();
    void openWithInvalidPathFailsAndEmitsError();
    void availableDevicesDoesNotCrash();
};

void TestUsbHidManager::startsDisconnected() {
    UsbHidManager manager;
    QVERIFY(!manager.isConnected());
    QVERIFY(manager.path().isEmpty());
}

void TestUsbHidManager::closeWithoutOpenIsNoop() {
    UsbHidManager manager;
    QSignalSpy stateSpy(&manager, &UsbHidManager::connectionStateChanged);

    manager.close();

    QVERIFY(!manager.isConnected());
    QCOMPARE(stateSpy.count(), 0);
}

void TestUsbHidManager::writeWhileDisconnectedReturnsFalse() {
    UsbHidManager manager;
    QVERIFY(!manager.write(QByteArrayLiteral("hello")));
}

void TestUsbHidManager::openWithEmptyPathFailsWithoutCrashing() {
    UsbHidManager manager;
    QVERIFY(!manager.open(QString()));
    QVERIFY(!manager.isConnected());
}

void TestUsbHidManager::openWithInvalidPathFailsAndEmitsError() {
    UsbHidManager manager;
    QSignalSpy errorSpy(&manager, &UsbHidManager::errorOccurred);
    QSignalSpy stateSpy(&manager, &UsbHidManager::connectionStateChanged);

    const bool opened = manager.open("__traceview_no_such_hid_device__");

    QVERIFY(!opened);
    QVERIFY(!manager.isConnected());
    QCOMPARE(stateSpy.count(), 0);
    QCOMPARE(errorSpy.count(), 1);
}

void TestUsbHidManager::availableDevicesDoesNotCrash() {
    // Environment-dependent (no real HID devices on a CI box), just verify
    // the call is safe and returns without asserting on content -- same
    // contract test_serialmanager.cpp's availablePortsDoesNotCrash() has for
    // QSerialPortInfo::availablePorts().
    (void)UsbHidManager::availableDevices();
    QVERIFY(true);
}

}  // namespace

QTEST_MAIN(TestUsbHidManager)
#include "test_usbhidmanager.moc"
