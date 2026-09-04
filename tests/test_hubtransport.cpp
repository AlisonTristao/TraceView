#include <QSignalSpy>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <cstdint>
#include <vector>

#include "core/deviceconnection.h"
#include "core/hubtransport.h"
#include "devices/device.h"
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

}  // namespace

class TestHubTransport : public QObject {
    Q_OBJECT

private slots:
    void twoChildrenOverOneParentEachClaimOnlyTheirOwnSourceId();
    void aFrameForNoChildIsClaimedByNoChild();
    void writeReachesTheParentWithoutTouchingTheOctets();
    void unconfiguredPeerNeverConnectsAndNeverClaims();
    void closingOneChildLeavesTheParentAndItsSiblingAlone();
    void detachingTheParentDropsTheChild();
};

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

QTEST_MAIN(TestHubTransport)
#include "test_hubtransport.moc"
