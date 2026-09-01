#include <QtTest>

#include "protocol/statusreport.h"

using traceview::parseStatusPayload;
using traceview::StatusReport;
using traceview::StatusTopicRecord;

namespace {

void appendLe(QByteArray& out, quint64 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// The fixed 92-octet block of commands.md section 5, identical in
// v1 and v2 (same fields, same offsets) -- only `status_version` differs.
QByteArray buildStatusV1Block(quint16 statusVersion) {
    QByteArray payload;
    appendLe(payload, statusVersion, 2);
    appendLe(payload, 0x0001, 2);                 // flags: DEGRADED
    appendLe(payload, 0x1122334455667788ULL, 8);  // uptime_us
    appendLe(payload, 1001, 8);                   // frames_rx
    appendLe(payload, 1002, 8);                   // frames_tx
    appendLe(payload, 1003, 8);                   // frames_dropped
    appendLe(payload, 1004, 8);                   // crc_errors
    appendLe(payload, 1005, 8);                   // decode_errors
    appendLe(payload, 1006, 8);                   // reassembly_completed
    appendLe(payload, 1007, 8);                   // reassembly_timeouts
    appendLe(payload, 1008, 8);                   // reassembly_rejected
    appendLe(payload, 1009, 8);                   // command_duplicates
    appendLe(payload, 1010, 8);                   // telemetry_dropped
    return payload;
}

QByteArray buildTopicRecord(quint32 sourceId, quint16 topicId, quint16 subscriberCount,
                            quint32 effectiveRateMillihz, quint64 bytesTotal,
                            quint64 samplesDropped) {
    QByteArray record;
    appendLe(record, sourceId, 4);
    appendLe(record, topicId, 2);
    appendLe(record, subscriberCount, 2);
    appendLe(record, effectiveRateMillihz, 4);
    appendLe(record, bytesTotal, 8);
    appendLe(record, samplesDropped, 8);
    return record;
}

class TestStatusReport : public QObject {
    Q_OBJECT

private slots:
    void parsesVersion1AndStopsAt92Octets();
    void rejectsTrailingBytesOnVersion1();
    void parsesVersion2TopicRecords();
    void rejectsTruncatedVersion2List();
    void rejectsShortOrUnknownVersion();
};

void TestStatusReport::parsesVersion1AndStopsAt92Octets() {
    const QByteArray payload = buildStatusV1Block(1);
    QCOMPARE(payload.size(), 92);

    StatusReport report;
    QVERIFY(parseStatusPayload(payload, &report));
    QCOMPARE(report.statusVersion, quint16(1));
    QVERIFY(report.degraded());
    QCOMPARE(report.uptimeUs, quint64(0x1122334455667788ULL));
    QCOMPARE(report.framesRx, quint64(1001));
    QCOMPARE(report.telemetryDropped, quint64(1010));
    QVERIFY(report.topics.isEmpty());
}

void TestStatusReport::rejectsTrailingBytesOnVersion1() {
    // A v1 STATUS payload is exactly 92 octets. Anything after that is a
    // malformed message, not "a v1 body with an ignorable tail": btp::messages
    // rejects it (the `status_v1_trailing_byte` invalid conformance vector),
    // so this v2-aware reader does too. The bytes here would even decode as a
    // well-formed topic_status list under a v2 header -- still rejected.
    QByteArray payload = buildStatusV1Block(1);
    appendLe(payload, 1, 2);  // would-be topic_status_count
    payload.append(buildTopicRecord(0x9F442484, 0x0001, 2, 50000, 4096, 7));

    StatusReport report;
    QVERIFY(!parseStatusPayload(payload, &report));
}

void TestStatusReport::parsesVersion2TopicRecords() {
    QByteArray payload = buildStatusV1Block(2);
    appendLe(payload, 2, 2);  // topic_status_count
    payload.append(buildTopicRecord(0x9F442484, 0x0001, 2, 48300, 0x0102030405060708ULL, 12));
    // Same topic_id from a different source: the pair (source_id, topic_id)
    // is what identifies a topic, never topic_id alone (section 5.1).
    payload.append(buildTopicRecord(0xAABBCCDD, 0x0001, 0, 0, 0, 0));
    QCOMPARE(payload.size(), 92 + 2 + 2 * 28);  // section 5.1's 28-octet stride

    StatusReport report;
    QVERIFY(parseStatusPayload(payload, &report));
    QCOMPARE(report.statusVersion, quint16(2));
    // The v1 block is at the same offsets in a v2 message.
    QCOMPARE(report.uptimeUs, quint64(0x1122334455667788ULL));
    QCOMPARE(report.telemetryDropped, quint64(1010));

    QCOMPARE(report.topics.size(), 2);
    QCOMPARE(report.topics[0].sourceId, quint32(0x9F442484));
    QCOMPARE(report.topics[0].topicId, quint16(0x0001));
    QCOMPARE(report.topics[0].subscriberCount, quint16(2));
    QCOMPARE(report.topics[0].effectiveRateMillihz, quint32(48300));
    QCOMPARE(report.topics[0].bytesTotal, quint64(0x0102030405060708ULL));
    QCOMPARE(report.topics[0].samplesDroppedTotal, quint64(12));
    QCOMPARE(report.topics[1].sourceId, quint32(0xAABBCCDD));
    QCOMPARE(report.topics[1].topicId, quint16(0x0001));
    // Zero effective rate is meaningful: the topic exists but is not being
    // published right now.
    QCOMPARE(report.topics[1].effectiveRateMillihz, quint32(0));
}

void TestStatusReport::rejectsTruncatedVersion2List() {
    QByteArray payload = buildStatusV1Block(2);
    appendLe(payload, 2, 2);  // claims two records...
    payload.append(
        buildTopicRecord(0x9F442484, 0x0001, 1, 50000, 10, 0));  // ...but only one follows

    StatusReport report;
    QVERIFY(!parseStatusPayload(payload, &report));

    // A zero count is valid (no topic has an active subscription).
    QByteArray empty = buildStatusV1Block(2);
    appendLe(empty, 0, 2);
    QVERIFY(parseStatusPayload(empty, &report));
    QCOMPARE(report.statusVersion, quint16(2));
    QVERIFY(report.topics.isEmpty());

    // status_version=2 with no topic_status_count at all is malformed.
    StatusReport untouched;
    QVERIFY(!parseStatusPayload(buildStatusV1Block(2), &untouched));
}

void TestStatusReport::rejectsShortOrUnknownVersion() {
    StatusReport report;
    QVERIFY(!parseStatusPayload(buildStatusV1Block(1).left(91), &report));
    QVERIFY(!parseStatusPayload(QByteArray(), &report));
    // Never guess at a version this client does not know.
    QVERIFY(!parseStatusPayload(buildStatusV1Block(3), &report));
    QVERIFY(!parseStatusPayload(buildStatusV1Block(0), &report));
}

}  // namespace

QTEST_MAIN(TestStatusReport)
#include "test_statusreport.moc"
