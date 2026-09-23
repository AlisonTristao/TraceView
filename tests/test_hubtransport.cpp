#include <QSignalSpy>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <cstdint>
#include <vector>

#include "core/deviceconnection.h"
#include "core/hubtransport.h"
#include "devices/device.h"
#include "preferences/appsettings.h"
#include "protocol/btpbackend.h"

using namespace traceview;

// Topico 26: a Device that connects THROUGH another Device instead of through
// a port. This is what makes the dongle a hub -- one cable, one serial
// connection, and a separate Device per robot behind its radio, each talking
// end to end with its own robot.
//
// The acceptance criterion of the topico is deliberately hardware-free: two
// HubTransports over one parent, each receiving only the frames whose
// source_id is its own. That is exactly what this file establishes, plus the
// two properties that keep it honest -- that nothing is re-encoded on the way
// out, and that a saved project addresses a robot by source_id rather than by
// the dongle's volatile channel index.

namespace {

// One frame, encoded under the ESP-NOW profile because that is what a child
// device produces: its octets are going to end up on a radio, so they are
// built to fit a radio datagram from the start and the hub never has to cut
// them up again.
QByteArray espNowFrame(quint32 sourceId, quint32 sequence, const QByteArray& payload) {
    btp::Header header{};
    header.type = btp::MessageType::Telemetry;
    header.flags = 0;
    header.source_id = sourceId;
    header.boot_id = 0x0BADB007U;
    header.sequence = sequence;
    header.timestamp_us = 1234567U;
    header.object_id = 0x0301U;
    header.fragment_index = 0;
    header.fragment_count = 1;

    const btp::Frame frame{
        header,
        {payload.isEmpty() ? nullptr : reinterpret_cast<const std::uint8_t*>(payload.constData()),
         std::size_t(payload.size())}};

    std::vector<std::uint8_t> encoded(btp::kEspNowMaxFrameSize);
    std::size_t written = 0;
    const btp::Error error =
        btp::encode(frame, btp::kEspNowTransport, encoded.data(), encoded.size(), &written);
    Q_ASSERT(error == btp::Error::Ok);
    Q_UNUSED(error);
    return QByteArray(reinterpret_cast<const char*>(encoded.data()), int(written));
}

// Stands in for the parent's physical link (the dongle's serial port), so a
// parent can be "connected" without hardware. Swapped in through the friend
// access DeviceConnection grants this fixture.
class SimulatedTransport : public traceview::Transport {
public:
    using Transport::Transport;
    bool connected = false;
    bool isConnected() const override {
        return connected;
    }
    bool write(const QByteArray&) override {
        return connected;
    }
    void close() override {
        connected = false;
    }
};

}  // namespace

namespace traceview {

class TestHubTransport : public QObject {
    Q_OBJECT

private slots:
    void twoChildrenOverOneParentEachClaimOnlyTheirOwnSourceId();
    void aFrameForNoChildIsClaimedByNoChild();
    void writeReachesTheParentWithoutTouchingTheOctets();
    void unconfiguredPeerNeverConnectsAndNeverClaims();
    void closingOneChildLeavesTheParentAndItsSiblingAlone();
    void detachingTheParentDropsTheChild();
    void reattachingDuringParentStateChangeNotifiesChildren();
    void childConfiguredUnderAReadyParentReportsConnected();
    void repeatedConnectViaIsANoOp();
    void childToggledOffAndOnUnderALiveParentComesBack();
    void childWaitsForTheParentSessionNotJustItsPort();
    void parentReconnectWithChildrenReattachedMidTransition();
    void reapplyingTheSameTargetKeepsThePhase();
    void sessionLossAfterReadyRecyclesTheTransport();
    void anUnreportedLinkChangeIsReconciledOnTheNextTick();

private:
    // A Serial DeviceConnection whose transport is a SimulatedTransport and
    // whose real port is out of the picture, so nothing here ever opens one.
    static SimulatedTransport* simulateLink(DeviceConnection& parent);
    static void bringUp(DeviceConnection& parent, SimulatedTransport* link, ConnectionPhase phase);
    static void bringDown(DeviceConnection& parent, SimulatedTransport* link);
};

SimulatedTransport* TestHubTransport::simulateLink(DeviceConnection& parent) {
    auto* link = new SimulatedTransport(&parent);
    parent.m_transport = link;
    parent.m_serialTransport = nullptr;
    parent.m_target = QStringLiteral("SIM");
    parent.m_baudRate = 115200;
    parent.m_shouldBeConnected = true;
    return link;
}

void TestHubTransport::bringUp(DeviceConnection& parent, SimulatedTransport* link,
                               ConnectionPhase phase) {
    link->connected = true;
    parent.reportConnected(true);  // what the transport lambda does on open
    parent.setConnectionPhase(phase);
}

void TestHubTransport::bringDown(DeviceConnection& parent, SimulatedTransport* link) {
    link->connected = false;
    parent.reportConnected(false);
}

// The bug this block is about: a child configured (or loaded, or toggled back
// on) while the dongle is already up used to be told "connected" in the one
// phase its own gate threw that away in -- and since nothing ever re-announced
// it, the card stayed red over a working link.
void TestHubTransport::childConfiguredUnderAReadyParentReportsConnected() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    bringUp(parent, link, ConnectionPhase::Ready);

    DeviceConnection child(CommType::Btp, TransportType::HubChannel);
    QSignalSpy states(&child, &DeviceConnection::connectionStateChanged);
    child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray());

    QCOMPARE(states.size(), 1);
    QCOMPARE(states.at(0).at(0).toBool(), true);
    QCOMPARE(child.connectionPhase(), ConnectionPhase::Ready);
}

// MainWindow::reattachHubChildren() runs on every device update, including
// from inside the parent's own signals. Same arguments must change nothing.
void TestHubTransport::repeatedConnectViaIsANoOp() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    bringUp(parent, link, ConnectionPhase::Ready);

    DeviceConnection child(CommType::Btp, TransportType::HubChannel);
    child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray());
    QSignalSpy states(&child, &DeviceConnection::connectionStateChanged);
    QSignalSpy phases(&child, &DeviceConnection::connectionPhaseChanged);

    for (int i = 0; i < 3; ++i) {
        child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray());
    }
    QCOMPARE(states.size(), 0);
    QCOMPARE(phases.size(), 0);
    QCOMPARE(child.connectionPhase(), ConnectionPhase::Ready);
}

// Clicking a child's status dot twice with the dongle online.
void TestHubTransport::childToggledOffAndOnUnderALiveParentComesBack() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    bringUp(parent, link, ConnectionPhase::Ready);

    DeviceConnection child(CommType::Btp, TransportType::HubChannel);
    child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray());
    QSignalSpy states(&child, &DeviceConnection::connectionStateChanged);

    child.disconnectFrom();
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.takeFirst().at(0).toBool(), false);
    QVERIFY(!child.isConnected());

    child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray());
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.takeFirst().at(0).toBool(), true);
    QCOMPARE(child.connectionPhase(), ConnectionPhase::Ready);
}

// The dongle's port being open is not enough: until its own handshake is
// done it is in console mode and has not re-bound its children.
void TestHubTransport::childWaitsForTheParentSessionNotJustItsPort() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    bringUp(parent, link, ConnectionPhase::NegotiatingBtp);

    DeviceConnection child(CommType::Btp, TransportType::HubChannel);
    QSignalSpy states(&child, &DeviceConnection::connectionStateChanged);
    child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray());
    QCOMPARE(states.size(), 0);
    QVERIFY(!child.isConnected());
    QCOMPARE(child.connectionPhase(), ConnectionPhase::Connecting);

    parent.setConnectionPhase(ConnectionPhase::Ready);
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.takeFirst().at(0).toBool(), true);

    // The parent dropping takes the child down, but the child keeps wanting
    // to be online -- it waits, it is not switched off.
    bringDown(parent, link);
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.takeFirst().at(0).toBool(), false);
    QVERIFY(child.wantsConnection());
    QCOMPARE(child.connectionPhase(), ConnectionPhase::Connecting);
}

// The real app's ordering: MainWindow sees the parent's signals first and
// re-applies every child's target from inside them (DevicesGrid emits
// deviceUpdated for live state too), before HubTransport's own slot runs.
void TestHubTransport::parentReconnectWithChildrenReattachedMidTransition() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    DeviceConnection child(CommType::Btp, TransportType::HubChannel);
    auto reattach = [&] { child.connectVia(&parent, 0x11111111U, 0x0A0A0A0AU, QByteArray()); };
    connect(&parent, &DeviceConnection::connectionStateChanged, &child, reattach);
    connect(&parent, &DeviceConnection::deviceIdentified, &child, reattach);
    reattach();

    QSignalSpy states(&child, &DeviceConnection::connectionStateChanged);
    for (int cycle = 0; cycle < 3; ++cycle) {
        bringUp(parent, link, ConnectionPhase::NegotiatingBtp);
        QVERIFY(states.isEmpty());
        emit parent.backend()->deviceIdentified(QStringLiteral("BTP/1"),
                                                QStringLiteral("0x0D0D0D0D"));
        QCOMPARE(parent.connectionPhase(), ConnectionPhase::Ready);
        QCOMPARE(states.size(), 1);
        QCOMPARE(states.takeFirst().at(0).toBool(), true);

        bringDown(parent, link);
        QCOMPARE(states.size(), 1);
        QCOMPARE(states.takeFirst().at(0).toBool(), false);
    }
}

// Re-applying an unchanged target (every deviceUpdated does) used to knock a
// handshaking parent back to Connecting, where both the Ready promotion and a
// later drop were ignored.
void TestHubTransport::reapplyingTheSameTargetKeepsThePhase() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    bringUp(parent, link, ConnectionPhase::NegotiatingBtp);

    parent.connectTo(QStringLiteral("SIM"), 115200);
    QCOMPARE(parent.connectionPhase(), ConnectionPhase::NegotiatingBtp);

    emit parent.backend()->deviceIdentified(QStringLiteral("BTP/1"), QStringLiteral("0x0D0D0D0D"));
    QCOMPARE(parent.connectionPhase(), ConnectionPhase::Ready);

    parent.connectTo(QStringLiteral("SIM"), 115200);
    QCOMPARE(parent.connectionPhase(), ConnectionPhase::Ready);
    QVERIFY(link->connected);
}

// A session that dies after it was established (the dongle back to BTP/1
// CONSOLE) must recycle the port, not leave it open and mute.
void TestHubTransport::sessionLossAfterReadyRecyclesTheTransport() {
    if (!AppSettings::instance().autoReconnect()) {
        QSKIP("auto-reconnect is disabled in this machine's settings");
    }
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    bringUp(parent, link, ConnectionPhase::Ready);

    emit parent.backend()->sessionRecoveryNeeded();
    QVERIFY(!link->connected);
}

// Safety net: however a transition got lost, the retry tick re-announces the
// link's real state.
void TestHubTransport::anUnreportedLinkChangeIsReconciledOnTheNextTick() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    SimulatedTransport* link = simulateLink(parent);
    link->connected = true;  // up, but nobody was told
    QSignalSpy states(&parent, &DeviceConnection::connectionStateChanged);

    parent.attemptReconnect();
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.at(0).at(0).toBool(), true);
    QCOMPARE(parent.connectionPhase(), ConnectionPhase::NegotiatingBtp);

    link->connected = false;  // and down, again without a word
    parent.attemptReconnect();
    QCOMPARE(states.size(), 2);
    QCOMPARE(states.at(1).at(0).toBool(), false);
}

void TestHubTransport::reattachingDuringParentStateChangeNotifiesChildren() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* link = new SimulatedTransport(&parent);
    parent.m_transport = link;
    HubTransport child(0x0A0A0A0AU);
    // MainWindow handles the parent's signal first and reattaches its children
    // through DevicesGrid::deviceUpdated before their own slots can run.
    connect(&parent, &DeviceConnection::connectionStateChanged, &child,
            [&] { child.attachTo(&parent); });
    child.attachTo(&parent);
    QSignalSpy states(&child, &Transport::connectionStateChanged);

    for (bool connected : {true, false, true}) {
        link->connected = connected;
        // A child rides the parent's session, not just its port: Ready is
        // what connects it.
        parent.m_connectionPhase =
            connected ? ConnectionPhase::Ready : ConnectionPhase::Disconnected;
        emit parent.connectionStateChanged(connected);
        QCOMPARE(child.isConnected(), connected);
        QCOMPARE(states.size(), 1);
        QCOMPARE(states.takeFirst().at(0).toBool(), connected);
        // Repeated configuration must not reset an established session.
        child.attachTo(&parent);
        QVERIFY(states.isEmpty());
    }
}

// The topico's stated acceptance criterion, and the reason the hub needs no
// routing table: every child sees every frame the parent decoded, and the
// entire demux is one comparison against source_id. Telemetry, log, terminal,
// manifest and command result all sort the same way, because what defines a
// channel is which two ends are talking, never what kind of message it is.
void TestHubTransport::twoChildrenOverOneParentEachClaimOnlyTheirOwnSourceId() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* backend = qobject_cast<BtpBackend*>(parent.backend());
    QVERIFY(backend != nullptr);

    const quint32 robotA = 0x0A0A0A0AU;
    const quint32 robotB = 0x0B0B0B0BU;

    HubTransport childA(robotA);
    HubTransport childB(robotB);
    childA.attachTo(&parent);
    childB.attachTo(&parent);

    QSignalSpy spyA(&childA, &Transport::dataReceived);
    QSignalSpy spyB(&childB, &Transport::dataReceived);

    const QByteArray frameA = espNowFrame(robotA, 1, QByteArray(16, '\xA1'));
    const QByteArray frameB = espNowFrame(robotB, 2, QByteArray(24, '\xB2'));

    // Drive the parent's hub signal directly: this is precisely what the
    // parent's BtpSession emits per decoded frame, so no port and no hardware
    // is involved.
    emit backend->hubFrameBytesReceived(robotA, frameA);
    emit backend->hubFrameBytesReceived(robotB, frameB);
    emit backend->hubFrameBytesReceived(robotA, frameA);

    QCOMPARE(spyA.count(), 2);
    QCOMPARE(spyB.count(), 1);
    QCOMPARE(spyA.at(0).at(0).toByteArray(), frameA);
    QCOMPARE(spyB.at(0).at(0).toByteArray(), frameB);
}

// The hub's own telemetry (hub.link, hub.usb, hub.peers) arrives with the
// dongle's source_id, which is no child's. Nobody claiming it is the correct
// outcome and needs no rule of its own -- the parent device consumes it, the
// same way it consumes everything addressed to itself.
void TestHubTransport::aFrameForNoChildIsClaimedByNoChild() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* backend = qobject_cast<BtpBackend*>(parent.backend());
    QVERIFY(backend != nullptr);

    HubTransport child(0x0A0A0A0AU);
    child.attachTo(&parent);
    QSignalSpy spy(&child, &Transport::dataReceived);

    const quint32 dongleItself = 0x0D0D0D0DU;
    emit backend->hubFrameBytesReceived(dongleItself,
                                        espNowFrame(dongleItself, 1, QByteArray(8, '\xDD')));

    QCOMPARE(spy.count(), 0);
}

// The property the whole encryption half of the plan rests on: a child's
// octets cross the hub untouched. The identity triple in that header
// (source_id, boot_id, sequence) is the AEAD nonce of a payload the hub holds
// no key for, so re-encoding here would break a seal two repositories away
// from where the symptom would appear.
void TestHubTransport::writeReachesTheParentWithoutTouchingTheOctets() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* backend = qobject_cast<BtpBackend*>(parent.backend());
    QVERIFY(backend != nullptr);

    const quint32 robot = 0x0A0A0A0AU;
    HubTransport child(robot);
    child.attachTo(&parent);

    // The parent is not on a port, so it is not connected, and a child of a
    // disconnected parent must not pretend otherwise.
    QVERIFY(!parent.isConnected());
    QVERIFY(!child.isConnected());
    QVERIFY(!child.write(espNowFrame(robot, 1, QByteArray(8, '\x11'))));

    // What the parent WOULD put on the wire, checked at the backend's own
    // output: sendChildFrame() forwards to BtpSession::sendRawFrame(), which
    // only wraps. Exercised directly because reaching it through a real
    // serial port is what this suite exists to avoid.
    QSignalSpy written(backend, &Backend::bytesToWrite);
    const QByteArray frame = espNowFrame(robot, 7, QByteArray(32, '\x5A'));
    QVERIFY(backend->sendChildFrame(frame));
    QCOMPARE(written.count(), 1);

    const QByteArray packet = written.at(0).at(0).toByteArray();
    // 0x00 || COBS(frame) || 0x00, and the frame inside is byte-for-byte the
    // one handed in -- CRC included, since it was never recomputed.
    QVERIFY(packet.size() > 2);
    QCOMPARE(packet.at(0), char(0));
    QCOMPARE(packet.at(packet.size() - 1), char(0));

    const QByteArray block = packet.mid(1, packet.size() - 2);
    std::vector<std::uint8_t> decoded(btp::kSerialMaxFrameSize);
    std::size_t decodedBytes = 0;
    QCOMPARE(
        btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(block.constData()),
                         std::size_t(block.size()), decoded.data(), decoded.size(), &decodedBytes),
        btp::CobsError::Ok);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(decoded.data()), int(decodedBytes)), frame);
}

// Peer 0 is "not configured", the value a project file missing the field
// falls back to. It must never connect and never claim anything: a child that
// attached to whatever robot happened to answer is the exact failure that
// storing a real address instead of a display index exists to prevent.
void TestHubTransport::unconfiguredPeerNeverConnectsAndNeverClaims() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* backend = qobject_cast<BtpBackend*>(parent.backend());
    QVERIFY(backend != nullptr);

    HubTransport child(0);
    child.attachTo(&parent);
    QSignalSpy spy(&child, &Transport::dataReceived);

    QVERIFY(!child.isConnected());
    emit backend->hubFrameBytesReceived(0, espNowFrame(0x0A0A0A0AU, 1, QByteArray(4, '\x01')));
    emit backend->hubFrameBytesReceived(0x0A0A0A0AU,
                                        espNowFrame(0x0A0A0A0AU, 2, QByteArray(4, '\x02')));
    QCOMPARE(spy.count(), 0);
    QVERIFY(!child.write(espNowFrame(0x0A0A0A0AU, 3, QByteArray(4, '\x03'))));
}

// Closing a child releases no port -- there is none -- and above all does not
// take the cable down. The parent is shared with the hub device itself and
// with every sibling child, so one child going away must leave both untouched.
void TestHubTransport::closingOneChildLeavesTheParentAndItsSiblingAlone() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* backend = qobject_cast<BtpBackend*>(parent.backend());
    QVERIFY(backend != nullptr);

    const quint32 robotA = 0x0A0A0A0AU;
    const quint32 robotB = 0x0B0B0B0BU;
    HubTransport childA(robotA);
    HubTransport childB(robotB);
    childA.attachTo(&parent);
    childB.attachTo(&parent);

    QSignalSpy spyA(&childA, &Transport::dataReceived);
    QSignalSpy spyB(&childB, &Transport::dataReceived);

    childA.close();

    emit backend->hubFrameBytesReceived(robotA, espNowFrame(robotA, 1, QByteArray(4, '\xA1')));
    emit backend->hubFrameBytesReceived(robotB, espNowFrame(robotB, 2, QByteArray(4, '\xB2')));

    QCOMPARE(spyA.count(), 0);             // closed: claims nothing
    QCOMPARE(spyB.count(), 1);             // sibling entirely unaffected
    QVERIFY(parent.backend() != nullptr);  // and the parent still exists
}

// Attaching to nullptr is how a child is detached, and it must be as complete
// as close(): a stale connection to a former parent would keep delivering
// another device's traffic into this one.
void TestHubTransport::detachingTheParentDropsTheChild() {
    DeviceConnection parent(CommType::Btp, TransportType::Serial);
    auto* backend = qobject_cast<BtpBackend*>(parent.backend());
    QVERIFY(backend != nullptr);

    const quint32 robot = 0x0A0A0A0AU;
    HubTransport child(robot);
    child.attachTo(&parent);
    QSignalSpy spy(&child, &Transport::dataReceived);

    emit backend->hubFrameBytesReceived(robot, espNowFrame(robot, 1, QByteArray(4, '\x11')));
    QCOMPARE(spy.count(), 1);

    child.attachTo(nullptr);
    QVERIFY(!child.isConnected());
    emit backend->hubFrameBytesReceived(robot, espNowFrame(robot, 2, QByteArray(4, '\x22')));
    QCOMPARE(spy.count(), 1);  // still one: nothing arrived after detaching
}

}  // namespace traceview

QTEST_MAIN(traceview::TestHubTransport)
#include "test_hubtransport.moc"
