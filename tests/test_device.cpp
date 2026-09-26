#include <QJsonArray>
#include <QtTest>

#include "devices/device.h"

using traceview::CommType;
using traceview::Device;
using traceview::DeviceLink;
using traceview::DeviceLinkCycler;
using traceview::DeviceLinkState;
using traceview::deviceLinkAt;
using traceview::deviceLinkCount;
using traceview::deviceLinksSignature;
using traceview::deviceLinkSummary;
using traceview::effectiveDevice;
using traceview::serialPortLabel;
using traceview::deviceLinkUsable;
using traceview::HubPeer;
using traceview::LinkDirectory;
using traceview::mdnsHostFromName;
using traceview::resolveLinkByName;
using traceview::SerialPortOption;
using traceview::deviceFromJson;
using traceview::deviceLinkState;
using traceview::deviceToJson;
using traceview::TransportType;

namespace {

class TestDevice : public QObject {
    Q_OBJECT

private slots:
    void roundTripsAllFieldsExceptLiveState();
    void roundTripsUsbHidTransport();
    void keepsTransportOrdinalsAndDedicatedConfiguration();
    void roundTripsDirectTransportConfiguration();
    void fromJsonRejectsMissingId();
    void fromJsonDefaultsMissingOptionalFields();
    void roundTripsHubChannelByPeerSourceIdNotChannelIndex();
    void hubChannelPasswordIsOmittedUnlessCachingWasOptedInto();
    void projectWithoutHubFieldsLoadsAsUnconfiguredRatherThanGuessing();
    void linkStateSeparatesPortOpenFromSessionLive();

    // Several ways to connect (Device::extraLinks)
    void extraLinksRoundTripAndActiveLinkIsNotSaved();
    void aSingleLinkDeviceSavesExactlyAsBefore();
    void effectiveDeviceUsesTheActiveLink();
    void linkStateIsJudgedByTheLinkInUse();
    void linksSignatureChangesOnlyWhenALinkDoes();
    void cyclerStaysWhileLiveOrUnwanted();
    void cyclerMovesOnAfterTheAttemptWindowAndWraps();
    void cyclerSkipsUnusableLinks();
    void cyclerMovesQuicklyPastAnUnresolvedLink();
    void cyclerMovesOnSoonAfterAFailure();

    void serialPortLabelShowsTheRobotNameWhenReported();

    // One name for every link (Device::robotName, DeviceLink::autoTarget)
    void mdnsHostFromNameMakesAValidLabel();
    void robotNameAndAutoTargetRoundTripAndAreOmittedWhenUnset();
    void autoLinkIsUsableOnlyWithARobotName();
    void autoTcpAndBleDeriveFromTheName();
    void autoSerialPicksTheOnePortReportingTheName();
    void autoHubFindsTheRobotByNameAndRefusesToGuess();
    void manualLinksAreNotResolved();
};

void TestDevice::linkStateSeparatesPortOpenFromSessionLive() {
    Device d;
    d.transportType = TransportType::Serial;

    d.connected = false;
    d.btpVersion.clear();
    QCOMPARE(deviceLinkState(d), DeviceLinkState::Offline);

    // Port open, handshake not done: the "connected and mute" case that used
    // to be indistinguishable from a working device.
    d.connected = true;
    QCOMPARE(deviceLinkState(d), DeviceLinkState::TransportOnly);

    d.btpVersion = "1";
    QCOMPARE(deviceLinkState(d), DeviceLinkState::Live);

    // A hub child never handshakes. Until reconcile has a presence verdict,
    // "connected" alone is not "live": a green dot on the strength of the cable
    // to the dongle is exactly the false-online the peer* fields exist to stop.
    Device child;
    child.transportType = TransportType::HubChannel;
    child.connected = true;
    child.btpVersion.clear();
    QCOMPARE(deviceLinkState(child), DeviceLinkState::PeerStale);  // presence not known yet

    // Reconcile has a verdict and the robot's frames are arriving.
    child.peerPresenceKnown = true;
    child.peerOnline = true;
    QCOMPARE(deviceLinkState(child), DeviceLinkState::Live);

    // The robot's data has stopped (or the dongle's hub.peers fallback reads
    // offline): the cable to the dongle is fine, the robot is not -- amber, its
    // own state, never a hard offline while the dongle is still relaying.
    child.peerOnline = false;
    QCOMPARE(deviceLinkState(child), DeviceLinkState::PeerStale);

    // A dropped hub link (the dongle itself) is Offline regardless of the peer
    // fields.
    child.peerOnline = true;
    child.connected = false;
    QCOMPARE(deviceLinkState(child), DeviceLinkState::Offline);
}

void TestDevice::roundTripsAllFieldsExceptLiveState() {
    Device device;
    device.id = "abc-123";
    device.name = "Bench dongle";
    device.connected = true;  // deliberately not expected to round-trip
    device.commType = CommType::Btp;
    device.description = "Left side of the desk";
    device.btpVersion = "BTP/1";  // deliberately not expected to round-trip
    device.btpId = "0xDEADBEEF";  // deliberately not expected to round-trip
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
    device.usbPath =
        R"(\\?\hid#vid_303a&pid_1001#7&1a2b3c4d&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})";

    bool ok = false;
    const Device roundTripped = deviceFromJson(deviceToJson(device), &ok);

    QVERIFY(ok);
    QCOMPARE(roundTripped.transportType, TransportType::UsbHid);
    QCOMPARE(roundTripped.usbPath, device.usbPath);
}

void TestDevice::keepsTransportOrdinalsAndDedicatedConfiguration() {
    QCOMPARE(int(TransportType::Serial), 0);
    QCOMPARE(int(TransportType::UsbHid), 1);
    QCOMPARE(int(TransportType::HubChannel), 2);
    QCOMPARE(int(TransportType::Tcp), 3);
    QCOMPARE(int(TransportType::Ble), 4);

    Device tcp;
    tcp.transportType = TransportType::Tcp;
    tcp.tcpHost = "robot.local";
    tcp.tcpPort = 44300;
    QVERIFY(tcp.portName.isEmpty());
    QCOMPARE(tcp.baudRate, traceview::kDefaultSerialBaudRate);

    Device ble;
    ble.transportType = TransportType::Ble;
    ble.bleAddress = "AA:BB:CC:DD:EE:FF";
    ble.blePeerUuid = "robot-uuid";
    QVERIFY(ble.tcpHost.isEmpty());
    QCOMPARE(ble.tcpPort, quint16(44300));
}

void TestDevice::roundTripsDirectTransportConfiguration() {
    Device device;
    device.id = "direct-1";
    device.transportType = TransportType::Tcp;
    device.tcpHost = "192.0.2.10";
    device.tcpPort = 44301;
    device.bleAddress = "AA:BB:CC:DD:EE:FF";
    device.blePeerUuid = "robot-uuid";

    bool ok = false;
    const Device loaded = deviceFromJson(deviceToJson(device), &ok);

    QVERIFY(ok);
    QCOMPARE(loaded.transportType, TransportType::Tcp);
    QCOMPARE(loaded.tcpHost, device.tcpHost);
    QCOMPARE(loaded.tcpPort, device.tcpPort);
    QCOMPARE(loaded.bleAddress, device.bleAddress);
    QCOMPARE(loaded.blePeerUuid, device.blePeerUuid);

    const Device invalidPort = deviceFromJson(
        QJsonObject{{"id", "invalid-port"}, {"tcpPort", 70000}}, &ok);
    QVERIFY(ok);
    QCOMPARE(invalidPort.tcpPort, quint16(44300));
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
    QCOMPARE(device.baudRate, traceview::kDefaultSerialBaudRate);
    QCOMPARE(device.lineTerminator, 1);
    QVERIFY(device.usbPath.isEmpty());
    QVERIFY(device.tcpHost.isEmpty());
    QCOMPARE(device.tcpPort, quint16(44300));
    QVERIFY(device.bleAddress.isEmpty());
    QVERIFY(device.blePeerUuid.isEmpty());
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
    QCOMPARE(json.value("peerPassword").toString(), QStringLiteral("correct horse battery staple"));
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

}  // namespace

namespace {

Device threeLinkDevice() {
    Device device;
    device.id = QStringLiteral("robot-a");
    device.name = QStringLiteral("Robot A");
    device.transportType = TransportType::Tcp;
    device.tcpHost = QStringLiteral("192.168.0.50");

    DeviceLink ble;
    ble.transportType = TransportType::Ble;
    ble.bleAddress = QStringLiteral("14:C1:00:00:00:01");
    DeviceLink hub;
    hub.transportType = TransportType::HubChannel;
    hub.parentDeviceId = QStringLiteral("dongle-1");
    hub.peerSourceId = 0xA1B2C3D4u;
    hub.enabled = false;
    device.extraLinks = {ble, hub};
    return device;
}

}  // namespace

void TestDevice::extraLinksRoundTripAndActiveLinkIsNotSaved() {
    Device device = threeLinkDevice();
    device.activeLink = 2;
    bool ok = false;
    const Device loaded = deviceFromJson(deviceToJson(device), &ok);
    QVERIFY(ok);
    QCOMPARE(loaded.extraLinks.size(), 2);
    QCOMPARE(int(loaded.extraLinks.at(0).transportType), int(TransportType::Ble));
    QCOMPARE(loaded.extraLinks.at(0).bleAddress, QStringLiteral("14:C1:00:00:00:01"));
    QVERIFY(loaded.extraLinks.at(0).enabled);
    QCOMPARE(int(loaded.extraLinks.at(1).transportType), int(TransportType::HubChannel));
    QCOMPARE(loaded.extraLinks.at(1).parentDeviceId, QStringLiteral("dongle-1"));
    QCOMPARE(loaded.extraLinks.at(1).peerSourceId, 0xA1B2C3D4u);  // full uint32 range
    QVERIFY(!loaded.extraLinks.at(1).enabled);
    // Live state: a loaded project always starts on its main link.
    QCOMPARE(loaded.activeLink, 0);
}

void TestDevice::aSingleLinkDeviceSavesExactlyAsBefore() {
    // No extraLinks key at all, so a project an older build wrote and one
    // this build writes for the same single-link device are identical.
    Device device;
    device.id = QStringLiteral("d");
    QVERIFY(!deviceToJson(device).contains(QStringLiteral("extraLinks")));
}

void TestDevice::effectiveDeviceUsesTheActiveLink() {
    Device device = threeLinkDevice();
    QCOMPARE(int(effectiveDevice(device).transportType), int(TransportType::Tcp));

    device.activeLink = 1;
    const Device viaBle = effectiveDevice(device);
    QCOMPARE(int(viaBle.transportType), int(TransportType::Ble));
    QCOMPARE(viaBle.bleAddress, QStringLiteral("14:C1:00:00:00:01"));
    QCOMPARE(viaBle.name, device.name);  // only the transport fields change

    device.activeLink = 7;  // stale index after an edit removed links
    QCOMPARE(int(effectiveDevice(device).transportType), int(TransportType::Tcp));

    QCOMPARE(deviceLinkCount(device), 3);
    QCOMPARE(deviceLinkAt(device, 0).tcpHost, QStringLiteral("192.168.0.50"));
    QCOMPARE(deviceLinkSummary(deviceLinkAt(device, 2)), QStringLiteral("Hub 0xA1B2C3D4"));
}

void TestDevice::linkStateIsJudgedByTheLinkInUse() {
    // Main link TCP, currently on the hub link: connected alone is not "live"
    // for a hub child -- the robot's presence decides.
    Device device = threeLinkDevice();
    device.extraLinks[1].enabled = true;
    device.activeLink = 2;
    device.connected = true;
    device.btpVersion = QStringLiteral("1");  // would read Live for TCP
    QCOMPARE(int(deviceLinkState(device)), int(DeviceLinkState::PeerStale));
    device.peerPresenceKnown = true;
    device.peerOnline = true;
    QCOMPARE(int(deviceLinkState(device)), int(DeviceLinkState::Live));
}

void TestDevice::linksSignatureChangesOnlyWhenALinkDoes() {
    Device device = threeLinkDevice();
    const QByteArray before = deviceLinksSignature(device);

    Device live = device;
    live.connected = true;
    live.activeLink = 1;
    live.btpVersion = QStringLiteral("1");
    live.name = QStringLiteral("renamed");
    QCOMPARE(deviceLinksSignature(live), before);

    Device edited = device;
    edited.extraLinks[0].bleAddress = QStringLiteral("14:C1:00:00:00:02");
    QVERIFY(deviceLinksSignature(edited) != before);
    Device primaryEdited = device;
    primaryEdited.tcpPort = 1234;
    QVERIFY(deviceLinksSignature(primaryEdited) != before);
}

void TestDevice::cyclerStaysWhileLiveOrUnwanted() {
    DeviceLinkCycler cycler;
    const QVector<bool> usable{true, true};
    const qint64 late = DeviceLinkCycler::kAttemptMs * 5;
    QCOMPARE(cycler.tick(usable, true, false, 0), -1);  // first tick starts the clock
    QCOMPARE(cycler.tick(usable, true, true, late), -1);  // live: stay
    QCOMPARE(cycler.tick(usable, false, false, late * 2), -1);  // user disconnected it
    QCOMPARE(cycler.active(), 0);
}

void TestDevice::cyclerMovesOnAfterTheAttemptWindowAndWraps() {
    DeviceLinkCycler cycler;
    const QVector<bool> usable{true, true, true};
    const qint64 w = DeviceLinkCycler::kAttemptMs;
    QCOMPARE(cycler.tick(usable, true, false, 0), -1);
    QCOMPARE(cycler.tick(usable, true, false, w - 1), -1);  // not yet
    QCOMPARE(cycler.tick(usable, true, false, w), 1);
    QCOMPARE(cycler.tick(usable, true, false, w + 1), -1);  // fresh window for link 1
    QCOMPARE(cycler.tick(usable, true, false, 2 * w), 2);
    QCOMPARE(cycler.tick(usable, true, false, 3 * w), 0);  // wraps to the main link
    QCOMPARE(cycler.active(), 0);

    // Once one comes up it is kept, and a later drop gets a full window
    // before moving on again.
    QCOMPARE(cycler.tick(usable, true, true, 5 * w), -1);
    QCOMPARE(cycler.tick(usable, true, false, 5 * w + 1), -1);
    QCOMPARE(cycler.tick(usable, true, false, 6 * w), 1);

    // An outside decision (edit / Connect) starts over.
    cycler.restart(0, 7 * w);
    QCOMPARE(cycler.active(), 0);
    QCOMPARE(cycler.tick(usable, true, false, 7 * w + 1), -1);
}

void TestDevice::cyclerMovesQuicklyPastAnUnresolvedLink() {
    DeviceLinkCycler cycler;
    const QVector<bool> usable{true, true};
    const qint64 u = DeviceLinkCycler::kUnresolvedAttemptMs;
    QCOMPARE(cycler.tick(usable, true, false, 0, true), -1);
    QCOMPARE(cycler.tick(usable, true, false, u - 1, true), -1);
    QCOMPARE(cycler.tick(usable, true, false, u, true), 1);
    // A resolved link gets the full window again.
    QCOMPARE(cycler.tick(usable, true, false, 2 * u, false), -1);
}

void TestDevice::cyclerMovesOnSoonAfterAFailure() {
    DeviceLinkCycler cycler;
    const QVector<bool> usable{true, true, true};
    const qint64 f = DeviceLinkCycler::kFailedAttemptMs;
    QCOMPARE(cycler.tick(usable, true, false, 0), -1);
    cycler.markFailed();
    QCOMPARE(cycler.tick(usable, true, false, f - 1), -1);
    QCOMPARE(cycler.tick(usable, true, false, f), 1);
    // The failure belonged to link 0: link 1 gets the full window.
    QCOMPARE(cycler.tick(usable, true, false, 2 * f), -1);
    QCOMPARE(cycler.tick(usable, true, false, f + DeviceLinkCycler::kAttemptMs - 1), -1);
    // A session coming up clears it too.
    cycler.markFailed();
    QCOMPARE(cycler.tick(usable, true, true, 3 * f), -1);
    QCOMPARE(cycler.tick(usable, true, false, 3 * f + 1), -1);
    QCOMPARE(cycler.tick(usable, true, false, 5 * f), -1);
}

void TestDevice::cyclerSkipsUnusableLinks() {
    DeviceLinkCycler cycler;
    const qint64 w = DeviceLinkCycler::kAttemptMs;
    QCOMPARE(cycler.tick({true, false, true}, true, false, 0), -1);
    QCOMPARE(cycler.tick({true, false, true}, true, false, w), 2);
    // Only one usable link left: keep retrying it, never "switch" to itself.
    QCOMPARE(cycler.tick({false, false, true}, true, false, 2 * w), -1);
    QCOMPARE(cycler.active(), 2);
}

void TestDevice::serialPortLabelShowsTheRobotNameWhenReported() {
    const QString dash = QStringLiteral(" ") + QChar(0x2014) + QStringLiteral(" ");
    QCOMPARE(serialPortLabel("COM5", "Robo2"), QStringLiteral("COM5") + dash + "Robo2");
    QCOMPARE(serialPortLabel("ttyACM0", "  BallyRobot "),
             QStringLiteral("ttyACM0") + dash + "BallyRobot");
    // Nothing useful reported: the bare port, as before.
    QCOMPARE(serialPortLabel("COM5", QString()), QStringLiteral("COM5"));
    QCOMPARE(serialPortLabel("COM5", "   "), QStringLiteral("COM5"));
    QCOMPARE(serialPortLabel("COM5", "N/A"), QStringLiteral("COM5"));
    QCOMPARE(serialPortLabel("COM5", "COM5"), QStringLiteral("COM5"));
}

void TestDevice::mdnsHostFromNameMakesAValidLabel() {
    QCOMPARE(mdnsHostFromName("BallyRobot"), QStringLiteral("ballyrobot"));
    QCOMPARE(mdnsHostFromName("  Robo 2 "), QStringLiteral("robo-2"));
    QCOMPARE(mdnsHostFromName("robo__x--y"), QStringLiteral("robo-x-y"));
    QCOMPARE(mdnsHostFromName("-edge-"), QStringLiteral("edge"));
    QCOMPARE(mdnsHostFromName("!!!"), QString());
    QCOMPARE(mdnsHostFromName(QString(80, QLatin1Char('a'))).size(), 63);
    QCOMPARE(mdnsHostFromName(QString(62, QLatin1Char('a')) + " b"),
             QString(62, QLatin1Char('a')));
}

void TestDevice::robotNameAndAutoTargetRoundTripAndAreOmittedWhenUnset() {
    Device plain;
    plain.id = "d";
    const QJsonObject plainJson = deviceToJson(plain);
    QVERIFY(!plainJson.contains("robotName"));
    QVERIFY(!plainJson.contains("autoTarget"));

    Device named = plain;
    named.robotName = "Robo2";
    named.autoTarget = true;
    named.transportType = TransportType::Tcp;
    DeviceLink ble;
    ble.transportType = TransportType::Ble;
    ble.autoTarget = true;
    DeviceLink manual;
    manual.transportType = TransportType::Serial;
    manual.portName = "COM5";
    named.extraLinks = {ble, manual};

    const QJsonObject json = deviceToJson(named);
    QVERIFY(!json.value("extraLinks").toArray().at(1).toObject().contains("auto"));
    bool ok = false;
    const Device back = deviceFromJson(json, &ok);
    QVERIFY(ok);
    QCOMPARE(back.robotName, QStringLiteral("Robo2"));
    QVERIFY(back.autoTarget);
    QVERIFY(deviceLinkAt(back, 0).autoTarget);
    QVERIFY(back.extraLinks.at(0).autoTarget);
    QVERIFY(!back.extraLinks.at(1).autoTarget);
}

void TestDevice::autoLinkIsUsableOnlyWithARobotName() {
    Device device;
    DeviceLink link;
    link.transportType = TransportType::Ble;
    link.autoTarget = true;
    QVERIFY(!deviceLinkUsable(device, link));
    device.robotName = "Robo2";
    QVERIFY(deviceLinkUsable(device, link));
    link.enabled = false;
    QVERIFY(!deviceLinkUsable(device, link));
}

void TestDevice::autoTcpAndBleDeriveFromTheName() {
    DeviceLink tcp;
    tcp.transportType = TransportType::Tcp;
    tcp.autoTarget = true;
    tcp.tcpHost = "stale.example";
    QCOMPARE(resolveLinkByName("Robo 2", tcp, {}).tcpHost, QStringLiteral("robo-2.local"));
    QCOMPARE(resolveLinkByName("", tcp, {}).tcpHost, QString());

    DeviceLink ble;
    ble.transportType = TransportType::Ble;
    ble.autoTarget = true;
    QCOMPARE(resolveLinkByName(" Robo2 ", ble, {}).bleAddress, QStringLiteral("Robo2"));
}

void TestDevice::autoSerialPicksTheOnePortReportingTheName() {
    DeviceLink serial;
    serial.transportType = TransportType::Serial;
    serial.autoTarget = true;
    LinkDirectory directory;
    directory.ports = {SerialPortOption{"COM3", "COM3", QString()},
                       SerialPortOption{"COM18", "COM18", "robo2"},
                       SerialPortOption{"COM20", "COM20", "OtherBot"}};
    QCOMPARE(resolveLinkByName("Robo2", serial, directory).portName, QStringLiteral("COM18"));
    QCOMPARE(resolveLinkByName("Nobody", serial, directory).portName, QString());
    // No port reports the name: the port the link was last set to, if present.
    DeviceLink remembered = serial;
    remembered.portName = QStringLiteral("COM18");
    QCOMPARE(resolveLinkByName("Nobody", remembered, directory).portName, QStringLiteral("COM18"));
    remembered.portName = QStringLiteral("COM99");
    QCOMPARE(resolveLinkByName("Nobody", remembered, directory).portName, QString());
    // Two ports claim the name: neither is picked.
    directory.ports.append(SerialPortOption{"COM21", "COM21", "Robo2"});
    QCOMPARE(resolveLinkByName("Robo2", serial, directory).portName, QString());
}

void TestDevice::autoHubFindsTheRobotByNameAndRefusesToGuess() {
    DeviceLink hub;
    hub.transportType = TransportType::HubChannel;
    hub.autoTarget = true;
    HubPeer robo2;
    robo2.sourceId = 0xA1B2C3D4;
    robo2.name = "Robo2";
    HubPeer other;
    other.sourceId = 0x11111111;
    other.name = "Other";
    LinkDirectory directory;
    directory.hubPeers = {{"dongleA", other}, {"dongleA", robo2}, {"dongleB", robo2}};

    DeviceLink found = resolveLinkByName("robo2", hub, directory);
    QCOMPARE(found.parentDeviceId, QStringLiteral("dongleA"));
    QCOMPARE(found.peerSourceId, 0xA1B2C3D4u);

    // A chosen hub narrows the search.
    hub.parentDeviceId = "dongleB";
    found = resolveLinkByName("Robo2", hub, directory);
    QCOMPARE(found.parentDeviceId, QStringLiteral("dongleB"));
    QCOMPARE(found.peerSourceId, 0xA1B2C3D4u);

    // Two different robots with the same name: no robot id at all.
    hub.parentDeviceId.clear();
    HubPeer twin = robo2;
    twin.sourceId = 0x22222222;
    directory.hubPeers.append({"dongleB", twin});
    QCOMPARE(resolveLinkByName("Robo2", hub, directory).peerSourceId, 0u);
}

void TestDevice::manualLinksAreNotResolved() {
    DeviceLink tcp;
    tcp.transportType = TransportType::Tcp;
    tcp.tcpHost = "192.168.4.1";
    QCOMPARE(resolveLinkByName("Robo2", tcp, {}).tcpHost, QStringLiteral("192.168.4.1"));
}

QTEST_MAIN(TestDevice)
#include "test_device.moc"
