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
    void roundTripsHubChannelByPeerSourceIdNotChannelIndex();
    void hubChannelPasswordIsOmittedUnlessCachingWasOptedInto();
    void projectWithoutHubFieldsLoadsAsUnconfiguredRatherThanGuessing();
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

// The persistence half of the hub topico, and the one that decides whether a
// saved project still means what it meant when it was saved.
//
// The dongle publishes a "channel" number per peer, and it is the short,
// friendly thing the UI shows -- so it is exactly what someone would reach
// for when persisting a selection. It must not be. That number is a display
// index assigned in the order the dongle first heard each peer, and it is not
// stable across a dongle reboot: bring the robots up in a different order and
// channel 1 now names a different one. A project saved with the index reopens
// pointing at the wrong robot, plotting real data from the wrong machine,
// raising no error anywhere.
//
// So this asserts two things at once: that peerSourceId survives, and that
// nothing resembling a channel index is written at all.
void TestDevice::roundTripsHubChannelByPeerSourceIdNotChannelIndex() {
    Device device;
    device.id = "child-1";
    device.name = "Robot A";
    device.transportType = TransportType::HubChannel;
    device.parentDeviceId = "dongle-0";
    // Deliberately in the top half of the uint32 range: a source_id that a
    // signed round trip would wrap into a negative number.
    device.peerSourceId = 0xC0FFEE01u;

    const QJsonObject json = deviceToJson(device);
    QCOMPARE(json.value("transportType").toInt(), int(TransportType::HubChannel));
    QCOMPARE(json.value("parentDeviceId").toString(), QStringLiteral("dongle-0"));
    QVERIFY(!json.contains("channel"));
    QVERIFY(!json.contains("peerChannel"));
    QVERIFY(!json.contains("channelIndex"));

    bool ok = false;
    const Device loaded = deviceFromJson(json, &ok);
    QVERIFY(ok);
    QCOMPARE(loaded.transportType, TransportType::HubChannel);
    QCOMPARE(loaded.parentDeviceId, QStringLiteral("dongle-0"));
    // The whole point: the address survives intact, including its top bit.
    QCOMPARE(loaded.peerSourceId, 0xC0FFEE01u);
}

// A .tvproj is a file people mail to each other and commit, so it must not
// become a secrets file by accident. Caching a password is per device and
// explicit; without it the key is absent from the JSON entirely -- not
// present and empty, which would still say something about the device.
void TestDevice::hubChannelPasswordIsOmittedUnlessCachingWasOptedInto() {
    Device device;
    device.id = "child-1";
    device.transportType = TransportType::HubChannel;
    device.peerSourceId = 0x0A0A0A0Au;
    device.peerPassword = "correct horse battery staple";
    device.cachePeerPassword = false;

    QJsonObject json = deviceToJson(device);
    QVERIFY(!json.contains("peerPassword"));

    bool ok = false;
    Device loaded = deviceFromJson(json, &ok);
    QVERIFY(ok);
    QVERIFY(loaded.peerPassword.isEmpty());
    QVERIFY(!loaded.cachePeerPassword);

    // Opted in: it is written, and it comes back.
    device.cachePeerPassword = true;
    json = deviceToJson(device);
    QCOMPARE(json.value("peerPassword").toString(),
             QStringLiteral("correct horse battery staple"));
    loaded = deviceFromJson(json, &ok);
    QVERIFY(ok);
    QVERIFY(loaded.cachePeerPassword);
    QCOMPARE(loaded.peerPassword, QStringLiteral("correct horse battery staple"));

    // And a stray password in a project that did NOT opt in is ignored rather
    // than honored -- the flag is what decides, not the key's presence.
    json["cachePeerPassword"] = false;
    loaded = deviceFromJson(json, &ok);
    QVERIFY(ok);
    QVERIFY(loaded.peerPassword.isEmpty());
}

// A project written before hub channels existed has none of these fields. The
// safe direction for every one of them is "not configured", because the
// alternative -- a child that attaches to whatever robot happens to answer --
// is the exact failure that storing a real address exists to prevent.
void TestDevice::projectWithoutHubFieldsLoadsAsUnconfiguredRatherThanGuessing() {
    QJsonObject json;
    json["id"] = "old-device";
    json["name"] = "From an older save";

    bool ok = false;
    const Device loaded = deviceFromJson(json, &ok);
    QVERIFY(ok);
    QCOMPARE(loaded.transportType, TransportType::Serial);
    QVERIFY(loaded.parentDeviceId.isEmpty());
    QCOMPARE(loaded.peerSourceId, 0u);
    QVERIFY(!loaded.cachePeerPassword);
    QVERIFY(loaded.peerPassword.isEmpty());
}

} // namespace

QTEST_MAIN(TestDevice)
#include "test_device.moc"
