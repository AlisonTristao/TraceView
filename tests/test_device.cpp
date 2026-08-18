#include <QtTest>

#include "devices/device.h"

using traceview::CommType;
using traceview::Device;
using traceview::deviceFromJson;
using traceview::deviceToJson;

namespace {

class TestDevice : public QObject {
    Q_OBJECT

private slots:
    void roundTripsAllFieldsExceptConnected();
    void fromJsonRejectsMissingId();
    void fromJsonDefaultsMissingOptionalFields();
};

void TestDevice::roundTripsAllFieldsExceptConnected() {
    Device device;
    device.id = "abc-123";
    device.name = "Bench dongle";
    device.connected = true; // deliberately not expected to round-trip
    device.commType = CommType::Btp;
    device.description = "Left side of the desk";
    device.btpVersion = "1";
    device.chipType = "esp32";
    device.btpId = "deadbeef";
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
    QCOMPARE(roundTripped.btpVersion, device.btpVersion);
    QCOMPARE(roundTripped.chipType, device.chipType);
    QCOMPARE(roundTripped.btpId, device.btpId);
    QCOMPARE(roundTripped.portName, device.portName);
    QCOMPARE(roundTripped.baudRate, device.baudRate);
    QCOMPARE(roundTripped.lineTerminator, device.lineTerminator);
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
    QVERIFY(device.portName.isEmpty());
    QCOMPARE(device.baudRate, 921600);
    QCOMPARE(device.lineTerminator, 1);
    QVERIFY(!device.connected);
}

} // namespace

QTEST_MAIN(TestDevice)
#include "test_device.moc"
