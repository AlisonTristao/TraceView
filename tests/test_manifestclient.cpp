#include <QSignalSpy>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/manifestclient.h"
#include "protocol/protocolrouter.h"
#include "protocol/telemetrycatalog.h"

using namespace traceview;

// ManifestClient is what turns a device's announced manifest into the
// catalog everything downstream decodes against, and it was the last piece
// of pure protocol logic in this repository with no test at all.
//
// Two halves are worth pinning, for different reasons.
//
// The REQUEST side is about what goes on the wire and when. Its rules are
// small but each exists to prevent a specific, quiet failure: a redundant
// full enumeration on every reconnect, an "ask this one robot" silently
// becoming "enumerate everything" because zero is the wildcard, and an
// unknown-schema sample stream flooding the link with duplicate requests
// while a reply is already in flight.
//
// The PARSE side is a bounds-checked walk over an attacker-shaped byte
// buffer. Every early return in it is a case where a malformed response
// must leave the catalog untouched rather than half-applied, and none of
// them is observable except by feeding it the bytes.

namespace {

constexpr quint16 kControlManifestRequest = 0x0003;
constexpr quint16 kControlManifestData = 0x0004;

constexpr quint8 kStatusSuccess = 0x00;
constexpr quint8 kStatusNotFound = 0x01;
constexpr quint8 kFlagNotModified = 0x01;

// Matches ManifestClient's own kUnknownSchemaRequestCooldownMs.
constexpr int kCooldownMs = 3000;

constexpr quint32 kRobot = 0x0A0A0A0Au;
constexpr quint32 kRobotBoot = 0x00BEEF01u;

// --------------------------------------------------------------- builders

void appendLe16(QByteArray& out, quint16 value) {
    out.append(char(value));
    out.append(char(value >> 8));
}

void appendLe32(QByteArray& out, quint32 value) {
    out.append(char(value));
    out.append(char(value >> 8));
    out.append(char(value >> 16));
    out.append(char(value >> 24));
}

void appendF64(QByteArray& out, double value) {
    char bytes[8];
    std::memcpy(bytes, &value, sizeof(bytes));  // host is little-endian
    out.append(bytes, 8);
}

void appendUtf8(QByteArray& out, const QString& text) {
    const QByteArray bytes = text.toUtf8();
    appendLe16(out, quint16(bytes.size()));
    out.append(bytes);
}

quint32 readLe32(const QByteArray& data, int offset) {
    return quint32(quint8(data.at(offset))) | (quint32(quint8(data.at(offset + 1))) << 8) |
           (quint32(quint8(data.at(offset + 2))) << 16) |
           (quint32(quint8(data.at(offset + 3))) << 24);
}

// One field record: its own size prefix, then the fixed header, then the
// three length-prefixed strings (commands.md section 3.3).
QByteArray fieldRecord(quint16 fieldId, quint8 type, const QString& name, const QString& unit) {
    QByteArray body;
    appendLe16(body, fieldId);
    appendLe16(body, fieldId);  // order
    body.append(char(type));
    body.append(char(0));  // flags
    appendLe16(body, 1);   // elementCount: scalar
    appendLe16(body, 0);   // maxElementCount
    appendF64(body, 1.0);  // scale
    appendF64(body, 0.0);  // offset
    appendLe16(body, 0);   // enumCount
    appendUtf8(body, name);
    appendUtf8(body, unit);
    appendUtf8(body, QString());  // description

    QByteArray record;
    appendLe32(record, quint32(body.size()));
    record.append(body);
    return record;
}

QByteArray topicRecord(quint16 topicId, quint16 schemaVersion, const QString& name,
                       const QVector<QByteArray>& fields) {
    QByteArray body;
    appendLe16(body, topicId);
    appendLe16(body, schemaVersion);
    body.append(char(0x05));  // encoding: PACKED_LE
    body.append(char(0));     // flags
    appendLe16(body, quint16(fields.size()));
    appendLe32(body, 1000);  // maxRateMillihz
    appendUtf8(body, name);
    appendUtf8(body, QString());  // description
    for (const QByteArray& field : fields) {
        body.append(field);
    }

    QByteArray record;
    appendLe32(record, quint32(body.size()));
    record.append(body);
    return record;
}

struct ManifestOptions {
    quint8 status = kStatusSuccess;
    quint8 flags = 0;
    quint16 formatVersion = 1;
    quint32 configRevision = 7;
    quint32 describedSourceId = kRobot;
    quint32 describedBootId = kRobotBoot;
    QVector<QByteArray> topics;
    QVector<QByteArray> actions;
};

QByteArray manifestDataPayload(const ManifestOptions& options) {
    QByteArray payload;
    payload.append(12, char(0));  // request-reference; correlation isn't used
    payload.append(char(options.status));
    payload.append(char(options.flags));
    appendLe16(payload, 0);  // errorCode
    appendLe16(payload, options.formatVersion);
    appendLe16(payload, 0);  // reserved
    appendLe32(payload, options.configRevision);
    payload.append(16, char(0));  // source_uuid; not modelled by TelemetryCatalog
    appendLe32(payload, options.describedSourceId);
    appendLe32(payload, options.describedBootId);
    payload.append(char(0));  // role
    payload.append(char(0));  // source flags
    appendLe16(payload, 0);   // catalogIndex
    appendLe16(payload, 1);   // catalogCount
    appendLe16(payload, quint16(options.topics.size()));
    appendLe16(payload, quint16(options.actions.size()));
    appendUtf8(payload, QStringLiteral("robot1"));
    for (const QByteArray& topic : options.topics) {
        payload.append(topic);
    }
    for (const QByteArray& action : options.actions) {
        payload.append(action);
    }
    return payload;
}

BtpFrame manifestDataFrame(const ManifestOptions& options) {
    BtpFrame frame;
    frame.type = btp::MessageType::Control;  // ProtocolRouter dispatches on this
    frame.objectId = kControlManifestData;
    frame.payload = manifestDataPayload(options);
    return frame;
}

// The two-topic response a robot actually sends: one scalar float32 field
// each, the shape bally_OS's ManifestResponder announces.
ManifestOptions twoTopicManifest() {
    ManifestOptions options;
    options.topics = {
        topicRecord(0x0001, 1, QStringLiteral("protocol.test"),
                    {fieldRecord(1, 0x09, QStringLiteral("value"), QStringLiteral("1"))}),
        topicRecord(0x0002, 3, QStringLiteral("robot.state"),
                    {fieldRecord(1, 0x09, QStringLiteral("velocity"), QStringLiteral("m/s")),
                     fieldRecord(2, 0x03, QStringLiteral("ticks"), QStringLiteral("1"))}),
    };
    return options;
}

// --------------------------------------------------------------- fixture

bool decodeWritten(const QByteArray& written, btp::DecodedFrame* out,
                   std::vector<std::uint8_t>* storage) {
    if (written.size() < 3 || written.at(0) != char(0) ||
        written.at(written.size() - 1) != char(0)) {
        return false;
    }
    const QByteArray block = written.mid(1, written.size() - 2);
    storage->assign(btp::kSerialMaxFrameSize, 0);
    std::size_t decoded = 0;
    if (btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(block.constData()),
                         std::size_t(block.size()), storage->data(), storage->size(),
                         &decoded) != btp::CobsError::Ok) {
        return false;
    }
    storage->resize(decoded);
    return btp::decode(storage->data(), storage->size(), btp::TransportProfile::Serial, out) ==
           btp::Error::Ok;
}

struct Fixture {
    BtpSession session{btp::TransportProfile::Serial};
    ProtocolRouter router;
    TelemetryCatalog catalog;
    ManifestClient client{&session, &router, &catalog};
    QSignalSpy written{&session, &BtpSession::bytesToWrite};

    // MANIFEST_REQUEST's payload is target_source_id, target_boot_id,
    // known_revision -- three LE u32s (commands.md section 3.1).
    bool request(int index, quint32* targetSourceId, quint32* knownRevision) const {
        btp::DecodedFrame frame{};
        std::vector<std::uint8_t> storage;
        if (index >= written.count() ||
            !decodeWritten(written.at(index).at(0).toByteArray(), &frame, &storage)) {
            return false;
        }
        if (frame.header.object_id != kControlManifestRequest || frame.payload.size < 12) {
            return false;
        }
        const QByteArray payload(reinterpret_cast<const char*>(frame.payload.data),
                                 int(frame.payload.size));
        *targetSourceId = readLe32(payload, 0);
        *knownRevision = readLe32(payload, 8);
        return true;
    }
};

}  // namespace

class TestManifestClient : public QObject {
    Q_OBJECT

private slots:
    // Request side
    void sessionEstablishedEnumeratesEverything();
    void reconnectingToAnUnchangedCatalogAsksNothing();
    void aChangedCatalogRevisionReEnumerates();
    void requestCatalogForAsksOneSourceOnly();
    void requestCatalogForZeroIsRefusedRatherThanWidened();
    void unknownSchemaIsRateLimitedPerSource();
    void unknownSchemaCarriesTheRevisionAlreadyCached();
    void aFailedResponseClearsTheCooldownSoARetryIsPossible();

    // Parse side
    void aSuccessfulResponseRegistersEverySchemaAndTheBootId();
    void notModifiedRecordsTheBootIdWithoutTouchingSchemas();
    void aNonSuccessStatusAppliesNothing();
    void anUnsupportedFormatVersionIsRejected();
    void aTruncatedPayloadIsRejected();
    void aTopicRecordRunningPastItsSizeIsRejected();
    void trailingActionRecordsAreSkippedNotMisparsed();
    void aFrameOfAnotherControlObjectIsIgnored();
};

// ============================================================ request side

void TestManifestClient::sessionEstablishedEnumeratesEverything() {
    Fixture fixture;
    fixture.client.onSessionEstablished(42);

    QCOMPARE(fixture.written.count(), 1);
    quint32 target = 0xFFFFFFFFu;
    quint32 revision = 0xFFFFFFFFu;
    QVERIFY(fixture.request(0, &target, &revision));
    // Zero is the enumeration wildcard: ask about everything the other end
    // knows of, which is what makes a hub answer for all its peers.
    QCOMPARE(target, 0u);
    QCOMPARE(revision, 0u);
}

void TestManifestClient::reconnectingToAnUnchangedCatalogAsksNothing() {
    // A full enumeration is the most expensive thing this client can do, and
    // an unplug/replug is routine. HELLO_RESULT's config_revision is what
    // makes it skippable -- the catalog is still in memory and still valid.
    Fixture fixture;
    fixture.client.onSessionEstablished(42);
    QCOMPARE(fixture.written.count(), 1);

    fixture.client.onSessionEstablished(42);
    fixture.client.onSessionEstablished(42);
    QCOMPARE(fixture.written.count(), 1);
}

void TestManifestClient::aChangedCatalogRevisionReEnumerates() {
    Fixture fixture;
    fixture.client.onSessionEstablished(42);
    fixture.client.onSessionEstablished(43);
    QCOMPARE(fixture.written.count(), 2);

    // ...and the new revision becomes the one that now counts as unchanged,
    // rather than the check always comparing against the first ever seen.
    fixture.client.onSessionEstablished(43);
    QCOMPARE(fixture.written.count(), 2);
}

void TestManifestClient::requestCatalogForAsksOneSourceOnly() {
    // The difference between talking to a hub and talking through one: a
    // child device wants its own robot's catalog, not everything the hub
    // has ever heard of.
    Fixture fixture;
    fixture.client.requestCatalogFor(kRobot);

    QCOMPARE(fixture.written.count(), 1);
    quint32 target = 0;
    quint32 revision = 0xFFFFFFFFu;
    QVERIFY(fixture.request(0, &target, &revision));
    QCOMPARE(target, kRobot);
    QCOMPARE(revision, 0u);
}

void TestManifestClient::requestCatalogForZeroIsRefusedRatherThanWidened() {
    // Zero is the wildcard on the wire, so passing it through would turn
    // "ask this robot" into "enumerate everything" -- which a robot cannot
    // answer, leaving a child with no catalog and no error explaining why.
    Fixture fixture;
    fixture.client.requestCatalogFor(0);
    QCOMPARE(fixture.written.count(), 0);
}

void TestManifestClient::unknownSchemaIsRateLimitedPerSource() {
    // A sample stream carrying an unknown schema_version arrives at the
    // telemetry rate. Without a cooldown, every one of them would put
    // another MANIFEST_REQUEST on the link while the first reply is still
    // in flight.
    Fixture fixture;
    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written.count(), 1);

    for (int i = 0; i < 20; ++i) {
        fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    }
    QCOMPARE(fixture.written.count(), 1);

    // Per source, not global: another robot's unknown schema is a separate
    // question and must not be silenced by the first one's cooldown.
    fixture.client.onUnknownSchema(0x0B0B0B0Bu, 0x0001, 9);
    QCOMPARE(fixture.written.count(), 2);
}

void TestManifestClient::unknownSchemaCarriesTheRevisionAlreadyCached() {
    // Sending the cached revision is what lets the far end answer
    // NOT_MODIFIED -- i.e. "your cache is current, this sample really does
    // use a schema_version nobody published", rather than this client
    // guessing that its cache is merely stale.
    Fixture fixture;
    ManifestOptions options = twoTopicManifest();
    options.configRevision = 99;
    fixture.router.onFrameReceived(manifestDataFrame(options));

    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written.count(), 1);

    quint32 target = 0;
    quint32 revision = 0;
    QVERIFY(fixture.request(0, &target, &revision));
    QCOMPARE(target, kRobot);
    QCOMPARE(revision, 99u);
}

void TestManifestClient::aFailedResponseClearsTheCooldownSoARetryIsPossible() {
    // NOT_FOUND usually means "that source isn't here yet". Leaving the
    // cooldown set would make the retry that matters -- once it does show
    // up -- wait out a window started by an attempt that answered nothing.
    Fixture fixture;
    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written.count(), 1);

    ManifestOptions failure;
    failure.status = kStatusNotFound;
    fixture.router.onFrameReceived(manifestDataFrame(failure));

    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written.count(), 2);
}

// ============================================================== parse side

void TestManifestClient::aSuccessfulResponseRegistersEverySchemaAndTheBootId() {
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    fixture.router.onFrameReceived(manifestDataFrame(twoTopicManifest()));

    QCOMPARE(updated.count(), 1);

    // SUBSCRIBE addresses a (source, boot) pair and this response is the
    // only carrier of the boot half.
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), kRobotBoot);

    const TelemetryTopicSchema* test = fixture.catalog.lookup(kRobot, 0x0001, 1);
    QVERIFY(test != nullptr);
    QCOMPARE(test->name, QStringLiteral("protocol.test"));
    QCOMPARE(int(test->encoding), int(TelemetryEncoding::PackedLe));
    QCOMPARE(test->fields.size(), 1);

    const TelemetryTopicSchema* state = fixture.catalog.lookup(kRobot, 0x0002, 3);
    QVERIFY(state != nullptr);
    QCOMPARE(state->name, QStringLiteral("robot.state"));
    QCOMPARE(state->fields.size(), 2);

    // Fields are addressed by fieldId, never by order or name
    // (telemetry.md section 8), and their declared types are what the
    // PACKED_LE decoder reads widths from -- a field parsed into the wrong
    // slot would misdecode every later field of the same sample.
    const TelemetryFieldSchema* velocity = state->fieldById(1);
    QVERIFY(velocity != nullptr);
    QCOMPARE(velocity->name, QStringLiteral("velocity"));
    QCOMPARE(velocity->unit, QStringLiteral("m/s"));
    QCOMPARE(int(velocity->type), int(TelemetryFieldType::Float32));

    const TelemetryFieldSchema* ticks = state->fieldById(2);
    QVERIFY(ticks != nullptr);
    QCOMPARE(ticks->name, QStringLiteral("ticks"));
    QCOMPARE(int(ticks->type), int(TelemetryFieldType::UInt32));

    // A schema_version this response did not describe must not resolve.
    QVERIFY(fixture.catalog.lookup(kRobot, 0x0002, 1) == nullptr);
}

void TestManifestClient::notModifiedRecordsTheBootIdWithoutTouchingSchemas() {
    Fixture fixture;
    fixture.router.onFrameReceived(manifestDataFrame(twoTopicManifest()));
    QVERIFY(fixture.catalog.lookup(kRobot, 0x0001, 1) != nullptr);

    // A reboot with an unchanged catalog: a new boot_id, and that alone has
    // to reach the catalog and fire the signal, because a SUBSCRIBE held
    // back for lack of a boot_id is waiting on exactly this.
    //
    // The response deliberately still CARRIES topic records -- describing a
    // single topic that would overwrite 0x0002's schema if it were applied.
    // A well-behaved sender omits them under NOT_MODIFIED, so a test using
    // an empty list would pass whether or not the flag is honoured at all:
    // the "apply every topic" path would simply have nothing to apply.
    // Sending them is what makes "NOT_MODIFIED means don't read the topic
    // records" an actual assertion.
    //
    // Two layers enforce that independently -- parseManifestData() skips
    // the topic records entirely, and onControlFrameReceived() returns
    // before the register loop -- so removing either one alone still
    // passes. That redundancy is deliberate on the implementation's part;
    // noted here so a future reader doesn't take a surviving mutation of
    // one of them as evidence this case is untested.
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);
    ManifestOptions notModified;
    notModified.flags = kFlagNotModified;
    notModified.describedBootId = 0x00BEEF02u;
    notModified.topics = {
        topicRecord(0x0002, 3, QStringLiteral("overwritten"),
                    {fieldRecord(1, 0x01, QStringLiteral("bogus"), QStringLiteral("1"))})};
    fixture.router.onFrameReceived(manifestDataFrame(notModified));

    QCOMPARE(updated.count(), 1);
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), 0x00BEEF02u);

    // Both original schemas intact, and 0x0002 still the one from the real
    // response rather than the decoy above.
    QCOMPARE(fixture.catalog.allSchemas().size(), 2);
    QVERIFY(fixture.catalog.lookup(kRobot, 0x0001, 1) != nullptr);
    const TelemetryTopicSchema* state = fixture.catalog.lookup(kRobot, 0x0002, 3);
    QVERIFY(state != nullptr);
    QCOMPARE(state->name, QStringLiteral("robot.state"));
    QCOMPARE(state->fields.size(), 2);
}

void TestManifestClient::aNonSuccessStatusAppliesNothing() {
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    ManifestOptions failure = twoTopicManifest();
    failure.status = kStatusNotFound;
    fixture.router.onFrameReceived(manifestDataFrame(failure));

    // Not even the boot_id: a response that failed describes nothing, and
    // caching a boot from it would address later SUBSCRIBEs at a source the
    // far end just said it does not have.
    QCOMPARE(updated.count(), 0);
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), 0u);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

void TestManifestClient::anUnsupportedFormatVersionIsRejected() {
    // Only manifest_format_version 1 is defined. A later one may reorder or
    // resize anything below this point, so parsing on would be reading a
    // layout nobody promised.
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    ManifestOptions future = twoTopicManifest();
    future.formatVersion = 2;
    fixture.router.onFrameReceived(manifestDataFrame(future));

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

void TestManifestClient::aTruncatedPayloadIsRejected() {
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    const QByteArray full = manifestDataPayload(twoTopicManifest());
    // Every truncation point, not just one: each field of the header is a
    // separate bounds check, and a cursor that advanced past the end on any
    // of them would read whatever follows the buffer.
    for (int length = 0; length < full.size(); ++length) {
        BtpFrame frame;
        frame.type = btp::MessageType::Control;
        frame.objectId = kControlManifestData;
        frame.payload = full.left(length);
        fixture.router.onFrameReceived(frame);
    }

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), 0u);

    // The untruncated one still parses, so the loop above was rejecting
    // truncation rather than the payload being unparseable all along.
    fixture.router.onFrameReceived(manifestDataFrame(twoTopicManifest()));
    QCOMPARE(updated.count(), 1);
    QCOMPARE(fixture.catalog.allSchemas().size(), 2);
}

void TestManifestClient::aTopicRecordRunningPastItsSizeIsRejected() {
    // record_size is the framing that makes unknown trailing bytes
    // skippable. A record whose declared size does not cover the fields it
    // then claims is inconsistent, and applying its first topic before
    // discovering that would leave the catalog half-updated.
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    QByteArray badTopic =
        topicRecord(0x0001, 1, QStringLiteral("protocol.test"),
                    {fieldRecord(1, 0x09, QStringLiteral("value"), QStringLiteral("1"))});
    // Shrink the declared record_size so the field record it contains now
    // runs past the record's own end.
    const quint32 realSize = readLe32(badTopic, 0);
    QByteArray shrunk;
    appendLe32(shrunk, realSize - 8);
    shrunk.append(badTopic.mid(4));

    ManifestOptions options;
    options.topics = {shrunk};
    fixture.router.onFrameReceived(manifestDataFrame(options));

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

void TestManifestClient::trailingActionRecordsAreSkippedNotMisparsed() {
    // Action records are not modelled yet, and are stepped over by the same
    // record_size framing. If that skip were wrong the topics would still
    // parse -- the damage would be silent, so the check is that a manifest
    // carrying them is applied exactly like one without.
    Fixture fixture;
    QByteArray action;
    appendLe32(action, 6);
    action.append(6, char(0xAB));

    ManifestOptions options = twoTopicManifest();
    options.actions = {action, action};
    fixture.router.onFrameReceived(manifestDataFrame(options));

    QCOMPARE(fixture.catalog.allSchemas().size(), 2);
    QVERIFY(fixture.catalog.lookup(kRobot, 0x0002, 3) != nullptr);
}

void TestManifestClient::aFrameOfAnotherControlObjectIsIgnored() {
    // CONTROL carries HELLO, SUBSCRIBE and more besides MANIFEST_DATA, and
    // they all reach this slot.
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    BtpFrame frame = manifestDataFrame(twoTopicManifest());
    frame.objectId = 0x0005;  // not MANIFEST_DATA
    fixture.router.onFrameReceived(frame);

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

QTEST_MAIN(TestManifestClient)
#include "test_manifestclient.moc"
