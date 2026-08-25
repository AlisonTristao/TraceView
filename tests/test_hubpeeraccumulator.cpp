#include <QtTest>

#include <QHash>
#include <QString>

#include "devices/hubpeeraccumulator.h"

using traceview::HubPeer;
using traceview::HubPeerAccumulator;

namespace {

// The dongle's own field ids for hub.peers (bally_dongle's
// DonglePublisher.cpp, kPeersFields). Deliberately NOT declared as the
// canonical numbers anywhere in TraceView: telemetry.md section 1 makes them
// local to a source's namespace, so the accumulator resolves by name and
// these are only what one particular dongle build happens to announce.
constexpr quint16 kChannelId = 1;
constexpr quint16 kSourceIdId = 2;
constexpr quint16 kBootIdId = 3;
constexpr quint16 kMacId = 4;
constexpr quint16 kLastSeenId = 5;
constexpr quint16 kOnlineId = 6;

QHash<quint16, QString> dongleSchema() {
    return {{kChannelId, "channel"}, {kSourceIdId, "source_id"}, {kBootIdId, "boot_id"},
            {kMacId, "mac"},          {kLastSeenId, "last_seen_ms"}, {kOnlineId, "online"}};
}

// Feeds one whole column, the way TelemetryFieldRouter does: ascending
// element index, starting at 0.
void feedColumn(HubPeerAccumulator& accumulator, quint16 fieldId, const QVector<double>& elements) {
    for (int i = 0; i < elements.size(); ++i) {
        accumulator.append(fieldId, quint16(i), elements.at(i));
    }
}

// One complete two-peer sample, the shape a real dongle publishes.
void feedTwoPeerSample(HubPeerAccumulator& accumulator) {
    feedColumn(accumulator, kChannelId, {0, 1});
    feedColumn(accumulator, kSourceIdId, {double(0x0A0A0A0Au), double(0x0B0B0B0Bu)});
    feedColumn(accumulator, kBootIdId, {7, 9});
    feedColumn(accumulator, kMacId, {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
    feedColumn(accumulator, kLastSeenId, {0, 12000});
    feedColumn(accumulator, kOnlineId, {1, 0});
}

}  // namespace

class TestHubPeerAccumulator : public QObject {
    Q_OBJECT

private slots:
    void unresolvedAccumulatorReportsNoPeers();
    void resolveRejectsSchemaMissingAField();
    void resolveIgnoresFieldIdNumbersAndUsesNamesOnly();
    void fullSampleDecodesEveryPeerField();
    void shorterSampleShrinksThePeerList();
    void partialSampleIsBoundedByItsShortestPerPeerColumn();
    void outOfOrderElementDropsItsColumnRatherThanMisalign();
    void clearSamplesKeepsTheResolvedSchema();
};

void TestHubPeerAccumulator::unresolvedAccumulatorReportsNoPeers() {
    HubPeerAccumulator accumulator;
    QVERIFY(!accumulator.isResolved());
    // Samples before a manifest has arrived are dropped, not buffered
    // against a schema that might never match them.
    accumulator.append(kSourceIdId, 0, 0x0A0A0A0Au);
    QVERIFY(accumulator.peers().isEmpty());
}

void TestHubPeerAccumulator::resolveRejectsSchemaMissingAField() {
    HubPeerAccumulator accumulator;
    QHash<quint16, QString> schema = dongleSchema();
    schema.remove(kSourceIdId);

    QVERIFY(!accumulator.resolve(schema));
    QVERIFY(!accumulator.isResolved());
    // A peer row with no source_id must never reach Device::peerSourceId,
    // so a partial schema leaves this decoding nothing at all rather than
    // producing rows with a zeroed address.
    feedColumn(accumulator, kChannelId, {0, 1});
    QVERIFY(accumulator.peers().isEmpty());
}

void TestHubPeerAccumulator::resolveIgnoresFieldIdNumbersAndUsesNamesOnly() {
    // Same six names, completely different numbers -- a renumbering the
    // dongle is free to do (telemetry.md section 1). Decoding must be
    // unaffected.
    HubPeerAccumulator accumulator;
    QVERIFY(accumulator.resolve({{70, "channel"},
                                  {71, "source_id"},
                                  {72, "boot_id"},
                                  {73, "mac"},
                                  {74, "last_seen_ms"},
                                  {75, "online"}}));

    feedColumn(accumulator, 70, {4});
    feedColumn(accumulator, 71, {double(0x12345678u)});
    feedColumn(accumulator, 72, {1});
    feedColumn(accumulator, 73, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66});
    feedColumn(accumulator, 74, {500});
    feedColumn(accumulator, 75, {1});

    const QVector<HubPeer> peers = accumulator.peers();
    QCOMPARE(peers.size(), 1);
    QCOMPARE(peers.at(0).channel, quint8(4));
    QCOMPARE(peers.at(0).sourceId, 0x12345678u);
    QCOMPARE(peers.at(0).mac, QStringLiteral("11:22:33:44:55:66"));
}

void TestHubPeerAccumulator::fullSampleDecodesEveryPeerField() {
    HubPeerAccumulator accumulator;
    QVERIFY(accumulator.resolve(dongleSchema()));
    feedTwoPeerSample(accumulator);

    const QVector<HubPeer> peers = accumulator.peers();
    QCOMPARE(peers.size(), 2);

    QCOMPARE(peers.at(0).channel, quint8(0));
    QCOMPARE(peers.at(0).sourceId, 0x0A0A0A0Au);
    QCOMPARE(peers.at(0).lastSeenAgeMs, 0u);
    QCOMPARE(peers.at(0).online, true);
    // Six octets per peer out of one flat array, uppercase and colon-joined
    // to match how a MAC is shown everywhere else in the app.
    QCOMPARE(peers.at(0).mac, QStringLiteral("AA:BB:CC:DD:EE:FF"));

    QCOMPARE(peers.at(1).channel, quint8(1));
    QCOMPARE(peers.at(1).sourceId, 0x0B0B0B0Bu);
    QCOMPARE(peers.at(1).lastSeenAgeMs, 12000u);
    QCOMPARE(peers.at(1).online, false);
    QCOMPARE(peers.at(1).mac, QStringLiteral("01:02:03:04:05:06"));
}

void TestHubPeerAccumulator::shorterSampleShrinksThePeerList() {
    HubPeerAccumulator accumulator;
    QVERIFY(accumulator.resolve(dongleSchema()));
    feedTwoPeerSample(accumulator);
    QCOMPARE(accumulator.peers().size(), 2);

    // A peer drops off the dongle's table: every array comes back one
    // element shorter. Writing by index alone would leave the departed
    // peer's values behind forever, so element 0 has to restart the column.
    feedColumn(accumulator, kChannelId, {0});
    feedColumn(accumulator, kSourceIdId, {double(0x0A0A0A0Au)});
    feedColumn(accumulator, kBootIdId, {7});
    feedColumn(accumulator, kMacId, {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF});
    feedColumn(accumulator, kLastSeenId, {0});
    feedColumn(accumulator, kOnlineId, {1});

    const QVector<HubPeer> peers = accumulator.peers();
    QCOMPARE(peers.size(), 1);
    QCOMPARE(peers.at(0).sourceId, 0x0A0A0A0Au);
}

void TestHubPeerAccumulator::partialSampleIsBoundedByItsShortestPerPeerColumn() {
    HubPeerAccumulator accumulator;
    QVERIFY(accumulator.resolve(dongleSchema()));

    // Read mid-update: channel already carries two entries, online only
    // one. The row count follows the shortest per-peer column so nothing
    // reads past the end of any of them.
    feedColumn(accumulator, kChannelId, {0, 1});
    feedColumn(accumulator, kSourceIdId, {double(0x0A0A0A0Au), double(0x0B0B0B0Bu)});
    feedColumn(accumulator, kBootIdId, {7, 9});
    feedColumn(accumulator, kMacId, {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF});
    feedColumn(accumulator, kLastSeenId, {0, 12000});
    feedColumn(accumulator, kOnlineId, {1});

    const QVector<HubPeer> peers = accumulator.peers();
    QCOMPARE(peers.size(), 1);
    QCOMPARE(peers.at(0).sourceId, 0x0A0A0A0Au);
}

void TestHubPeerAccumulator::outOfOrderElementDropsItsColumnRatherThanMisalign() {
    HubPeerAccumulator accumulator;
    QVERIFY(accumulator.resolve(dongleSchema()));
    feedTwoPeerSample(accumulator);
    QCOMPARE(accumulator.peers().size(), 2);

    // Element 1 with no element 0 before it: a gap. Storing it would put a
    // peer's address against the wrong row, so the column is dropped
    // instead, which the shortest-column rule then reports as no peers.
    accumulator.append(kSourceIdId, 1, 0x0C0C0C0Cu);
    QVERIFY(accumulator.peers().isEmpty());
}

void TestHubPeerAccumulator::clearSamplesKeepsTheResolvedSchema() {
    HubPeerAccumulator accumulator;
    QVERIFY(accumulator.resolve(dongleSchema()));
    feedTwoPeerSample(accumulator);
    QCOMPARE(accumulator.peers().size(), 2);

    // What a session drop calls for: the accumulated sample is stale, but
    // the schema it was decoded against survives, so a reconnect doesn't
    // have to wait for a second manifest exchange before decoding again.
    accumulator.clearSamples();
    QVERIFY(accumulator.isResolved());
    QVERIFY(accumulator.peers().isEmpty());

    feedTwoPeerSample(accumulator);
    QCOMPARE(accumulator.peers().size(), 2);
}

QTEST_MAIN(TestHubPeerAccumulator)
#include "test_hubpeeraccumulator.moc"
