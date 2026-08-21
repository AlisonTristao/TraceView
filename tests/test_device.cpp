#include <QtTest>

#include "devices/device.h"

using traceview::CommType;
using traceview::Device;
using traceview::deviceFromJson;
using traceview::deviceToJson;
using traceview::TransportType;

namespace {

class TestDevice : public QObject {
    Q_OBJECT

private slots:
    void roundTripsAllFieldsExceptLiveState();
    void roundTripsUsbHidTransport();
    void fromJsonRejectsMissingId();
    void fromJsonDefaultsMissingOptionalFields();
};

void TestDevice::roundTripsAllFieldsExceptLiveState() {
    Device device;
    device.id = "abc-123";
    device.name = "Bench dongle";
    device.connected = true; // deliberately not expected to round-trip
    device.commType = CommType::Btp;
    device.description = "Left side of the desk";
    device.btpVersion = "BTP/1"; // deliberately not expected to round-trip
    device.btpId = "0xDEADBEEF"; // deliberately not expected to round-trip
    device.transportType = TransportType::Serial;
    device.portName = "COM7";
    device.baudRate = 460800;
    device.lineTerminator = 3;

    bool ok = false;
    const Device roundTripped = deviceFromJson(deviceToJson(device), &ok);

    QVERIFY(ok);
    QCOMPARE(roundTripped.id, device.id);
    QCOMPARE(roundTripped.name, device.name);
    // connected is live transport state, not configuration -- a freshly
    // loaded device always starts disconnected (see device.h).
    QVERIFY(!roundTripped.connected);
    QCOMPARE(roundTripped.commType, device.commType);
    QCOMPARE(roundTripped.description, device.description);
    // btpVersion/btpId are live session state too (what the last
    // HELLO_RESULT reported) -- a freshly loaded device hasn't handshaked
    // yet, so both stay empty until DeviceConnection re-identifies it.
    QVERIFY(roundTripped.btpVersion.isEmpty());
    QVERIFY(roundTripped.btpId.isEmpty());
    QCOMPARE(roundTripped.transportType, device.transportType);
    QCOMPARE(roundTripped.portName, device.portName);
    QCOMPARE(roundTripped.baudRate, device.baudRate);
    QCOMPARE(roundTripped.lineTerminator, device.lineTerminator);
}

void TestDevice::roundTripsUsbHidTransport() {
    Device device;
    device.id = "usb-1";
    device.transportType = TransportType::UsbHid;
    device.usbPath = R"(\\?\hid#vid_303a&pid_1001#7&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})";

    bool ok = false;
    const Device roundTripped = deviceFromJson(deviceToJson(device), &ok);

    QVERIFY(ok);
    QCOMPARE(roundTripped.transportType, TransportType::UsbHid);
    QCOMPARE(roundTripped.usbPath, device.usbPath);
}

void TestDevice::fromJsonRejectsMissingId() {
    bool ok = true;
    deviceFromJson(QJsonObject{{"name", "No id"}}, &ok);
    QVERIFY(!ok);
}

void TestDevice::fromJsonDefaultsMissingOptionalFields() {
    bool ok = false;
    const Device device = deviceFromJson(QJsonObject{{"id", "abc"}}, &ok);

    QVERIFY(ok);
    QVERIFY(device.name.isEmpty());
    // A save from before TransportType existed has no "transportType" key
    // at all -- must default to Serial, the only transport that existed
    // then, so an older project loads exactly as it did before.
    QCOMPARE(device.transportType, TransportType::Serial);
    QVERIFY(device.portName.isEmpty());
    QCOMPARE(device.baudRate, 921600);
    QCOMPARE(device.lineTerminator, 1);
    QVERIFY(device.usbPath.isEmpty());
    QVERIFY(!device.connected);
}

} // namespace

QTEST_MAIN(TestDevice)
#include "test_device.moc"
