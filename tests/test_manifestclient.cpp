#include <QHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <btp/node.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

#include "protocol/manifestclient.h"
#include "protocol/manifeststore.h"
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
// Since BTP 2.48.0 both halves run over a real btp::Node: requests are what
// the node puts on the wire, answers are MANIFEST_DATA frames the node
// receives and hands over through on_manifest() -- together with the manifest
// cache, whose NOT_MODIFIED path is pinned here too.
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
constexpr quint32 kClientSourceId = 0x00C11E47u;
constexpr quint32 kClientBootId = 0x00000B07u;

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

// kFieldHasRange (BTP messages.hpp), 0x04: the flags bit that puts min_value/
// max_value on the wire at all (format >= 3 only) -- a field that doesn't set
// it carries no range bytes, same as fieldRecord() above.
constexpr quint8 kFieldHasRange = 0x04;

QByteArray fieldRecordWithRange(quint16 fieldId, quint8 type, const QString& name,
                                const QString& unit, double minValue, double maxValue) {
    QByteArray body;
    appendLe16(body, fieldId);
    appendLe16(body, fieldId);  // order
    body.append(char(type));
    body.append(char(kFieldHasRange));  // flags
    appendLe16(body, 1);   // elementCount: scalar
    appendLe16(body, 0);   // maxElementCount
    appendF64(body, 1.0);  // scale
    appendF64(body, 0.0);  // offset
    appendF64(body, minValue);
    appendF64(body, maxValue);
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

struct InfoTriple {
    QString key;
    QString label;
    QString value;
};

struct ManifestOptions {
    quint8 status = kStatusSuccess;
    quint8 flags = 0;
    quint16 formatVersion = 1;
    quint32 configRevision = 7;
    quint32 describedSourceId = kRobot;
    quint32 describedBootId = kRobotBoot;
    QVector<QByteArray> topics;
    QVector<QByteArray> actions;
    // Written after source_name when formatVersion >= 2 (commands.md 3.12).
    QVector<InfoTriple> sourceInfo;
    // The request reference (12 bytes) this answer echoes; zero = unsolicited.
    QByteArray requestRef;
};

QByteArray manifestDataPayload(const ManifestOptions& options) {
    QByteArray payload;
    if (options.requestRef.size() == 12) {
        payload.append(options.requestRef);
    } else {
        payload.append(12, char(0));  // request-reference: unsolicited
    }
    payload.append(char(options.status));
    payload.append(char(options.flags));
    appendLe16(payload, 0);  // errorCode
    appendLe16(payload, options.formatVersion);
    appendLe16(payload, 0);  // reserved
    appendLe32(payload, options.configRevision);
    payload.append(16, char(0));  // source_uuid; not modelled by TelemetryCatalog
    appendLe32(payload, options.describedSourceId);
    appendLe32(payload, options.describedBootId);
    // source_role: a SUCCESS descriptor names a real role (btp::messages rejects
    // a reserved one); a non-SUCCESS response describes no source and carries 0,
    // exactly as bally_OS / the dongle emit it.
    payload.append(char(options.status == kStatusSuccess ? 0x01 : 0x00));
    payload.append(char(0));  // source flags
    appendLe16(payload, 0);   // catalogIndex
    appendLe16(payload, 1);   // catalogCount
    appendLe16(payload, quint16(options.topics.size()));
    appendLe16(payload, quint16(options.actions.size()));
    appendUtf8(payload, QStringLiteral("robot1"));
    if (options.formatVersion >= 2) {
        appendLe16(payload, quint16(options.sourceInfo.size()));  // info_count
        for (const InfoTriple& entry : options.sourceInfo) {
            appendUtf8(payload, entry.key);
            appendUtf8(payload, entry.label);
            appendUtf8(payload, entry.value);
        }
    }
    for (const QByteArray& topic : options.topics) {
        payload.append(topic);
    }
    for (const QByteArray& action : options.actions) {
        payload.append(action);
    }
    return payload;
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

// The NodeConfig a BtpBackend would be, reduced to what ManifestClient needs:
// frames it sends are captured, and an in-memory manifest cache stands in for
// ManifestStore (on by default -- every backend runs with it on unless the
// user turns it off in Settings).
struct CaptureConfig : btp::NodeConfig {
    std::vector<std::vector<std::uint8_t>> frames;
    QHash<quint32, QByteArray> cache;
    bool cacheOn = true;
    int stores = 0;

    CaptureConfig() {
        source_id = kClientSourceId;
        boot_id = kClientBootId;
        transport = btp::kSerialTransport;
    }
    bool send(const std::uint8_t* frame, std::size_t size) override {
        frames.emplace_back(frame, frame + size);
        return true;
    }
    bool has_manifest_cache() const noexcept override {
        return cacheOn;
    }
    bool manifest_load(std::uint32_t sourceId, btp::ByteView* out) override {
        const auto it = cache.constFind(sourceId);
        if (it == cache.constEnd()) {
            return false;
        }
        *out = btp::ByteView{reinterpret_cast<const std::uint8_t*>(it->constData()),
                             std::size_t(it->size())};
        return true;
    }
    void manifest_store(std::uint32_t sourceId, const btp::ManifestHeader&,
                        btp::ByteView payload) override {
        ++stores;
        cache.insert(sourceId,
                     QByteArray(reinterpret_cast<const char*>(payload.data), int(payload.size)));
    }
    void manifest_evict(std::uint32_t sourceId) override {
        cache.remove(sourceId);
    }
};

struct Fixture {
    CaptureConfig cfg;
    btp::StaticNode<4, 700, 2048, 1024> node{cfg};
    TelemetryCatalog catalog;
    ManifestClient client{node, &catalog};
    quint32 robotSequence = 1;

    Fixture() {
        node.begin();
    }

    int written() const {
        return int(cfg.frames.size());
    }

    bool decodeSent(int index, btp::DecodedFrame* out) const {
        if (index >= written()) {
            return false;
        }
        const auto& bytes = cfg.frames.at(std::size_t(index));
        return btp::decode(bytes.data(), bytes.size(), btp::kSerialTransport, out) ==
               btp::Error::Ok;
    }

    // MANIFEST_REQUEST's payload is target_source_id, target_boot_id,
    // known_revision -- three LE u32s (commands.md section 3.1).
    bool request(int index, quint32* targetSourceId, quint32* knownRevision) const {
        btp::DecodedFrame frame{};
        if (!decodeSent(index, &frame) || frame.header.object_id != kControlManifestRequest ||
            frame.payload.size < 12) {
            return false;
        }
        const QByteArray payload(reinterpret_cast<const char*>(frame.payload.data),
                                 int(frame.payload.size));
        *targetSourceId = readLe32(payload, 0);
        *knownRevision = readLe32(payload, 8);
        return true;
    }

    // The request reference an answer to sent frame `index` echoes back
    // (commands.md section 3.2) -- what lets the node tie a rejection, which
    // names no source, to the target it was asked about.
    QByteArray replyTo(int index) const {
        btp::DecodedFrame frame{};
        QByteArray ref;
        if (decodeSent(index, &frame)) {
            appendLe32(ref, frame.header.source_id);
            appendLe32(ref, frame.header.boot_id);
            appendLe32(ref, frame.header.sequence);
        }
        return ref;
    }

    // Puts `payload` on the wire as the robot's CONTROL frame and hands it to
    // the node -- the same path a real MANIFEST_DATA takes.
    btp::NodeRx deliverPayload(const QByteArray& payload,
                               quint16 objectId = kControlManifestData) {
        btp::Frame frame{};
        frame.header.type = btp::MessageType::Control;
        frame.header.source_id = kRobot;
        frame.header.boot_id = kRobotBoot;
        frame.header.sequence = robotSequence++;
        frame.header.object_id = objectId;
        frame.header.fragment_count = 1;
        frame.payload = btp::ByteView{reinterpret_cast<const std::uint8_t*>(payload.constData()),
                                      std::size_t(payload.size())};
        std::vector<std::uint8_t> wire(btp::kSerialMaxFrameSize);
        std::size_t size = 0;
        if (btp::encode(frame, btp::kSerialTransport, wire.data(), wire.size(), &size) !=
            btp::Error::Ok) {
            return btp::NodeRx::DroppedFrame;
        }
        btp::ReceivedMessage out{};
        return node.receive(wire.data(), size, 0U, &out);
    }

    btp::NodeRx deliver(const ManifestOptions& options) {
        return deliverPayload(manifestDataPayload(options));
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
    void requestsGoOutAsTheNodesOwnIdentity();

    // Cache
    void notModifiedAfterARestartServesTopicsFromTheCache();
    void primeFromCacheRegistersSchemasButNoBoot();
    void primeFromCacheDoesNothingWithTheCacheOff();
    void readCachedReturnsTopicsAndSourceInfoWithoutRegistering();
    void cachedSourceNamedFindsTheRobotByItsReportedName();

    // Parse side
    void aSuccessfulResponseRegistersEverySchemaAndTheBootId();
    void notModifiedRecordsTheBootIdWithoutTouchingSchemas();
    void aNonSuccessStatusAppliesNothing();
    void anUnsupportedFormatVersionIsRejected();
    void aFormatThreeManifestAppliesRangePerField();
    void sourceInfoIsParsedAndReportedOnFullAndNotModified();
    void aTruncatedPayloadIsRejected();
    void aTopicRecordRunningPastItsSizeIsRejected();
    void trailingActionRecordsAreSkippedNotMisparsed();
    void aFrameOfAnotherControlObjectIsIgnored();
};

// ============================================================ request side

void TestManifestClient::sessionEstablishedEnumeratesEverything() {
    Fixture fixture;
    fixture.client.onSessionEstablished(42);

    QCOMPARE(fixture.written(), 1);
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
    QCOMPARE(fixture.written(), 1);

    fixture.client.onSessionEstablished(42);
    fixture.client.onSessionEstablished(42);
    QCOMPARE(fixture.written(), 1);
}

void TestManifestClient::aChangedCatalogRevisionReEnumerates() {
    Fixture fixture;
    fixture.client.onSessionEstablished(42);
    fixture.client.onSessionEstablished(43);
    QCOMPARE(fixture.written(), 2);

    // ...and the new revision becomes the one that now counts as unchanged,
    // rather than the check always comparing against the first ever seen.
    fixture.client.onSessionEstablished(43);
    QCOMPARE(fixture.written(), 2);
}

void TestManifestClient::requestCatalogForAsksOneSourceOnly() {
    // The difference between talking to a hub and talking through one: a
    // child device wants its own robot's catalog, not everything the hub
    // has ever heard of.
    Fixture fixture;
    fixture.client.requestCatalogFor(kRobot);

    QCOMPARE(fixture.written(), 1);
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
    QCOMPARE(fixture.written(), 0);
}

void TestManifestClient::unknownSchemaIsRateLimitedPerSource() {
    // A sample stream carrying an unknown schema_version arrives at the
    // telemetry rate. Without a cooldown, every one of them would put
    // another MANIFEST_REQUEST on the link while the first reply is still
    // in flight.
    Fixture fixture;
    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written(), 1);

    for (int i = 0; i < 20; ++i) {
        fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    }
    QCOMPARE(fixture.written(), 1);

    // Per source, not global: another robot's unknown schema is a separate
    // question and must not be silenced by the first one's cooldown.
    fixture.client.onUnknownSchema(0x0B0B0B0Bu, 0x0001, 9);
    QCOMPARE(fixture.written(), 2);
}

void TestManifestClient::unknownSchemaCarriesTheRevisionAlreadyCached() {
    // Sending the cached revision is what lets the far end answer
    // NOT_MODIFIED -- i.e. "your cache is current, this sample really does
    // use a schema_version nobody published", rather than this client
    // guessing that its cache is merely stale.
    Fixture fixture;
    ManifestOptions options = twoTopicManifest();
    options.configRevision = 99;
    fixture.deliver((options));

    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written(), 1);

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
    QCOMPARE(fixture.written(), 1);

    ManifestOptions failure;
    failure.status = kStatusNotFound;
    failure.requestRef = fixture.replyTo(0);
    QCOMPARE(int(fixture.deliver(failure)), int(btp::NodeRx::ManifestRejected));
    fixture.client.onManifestRejected(fixture.node.manifest_outcome());

    fixture.client.onUnknownSchema(kRobot, 0x0001, 9);
    QCOMPARE(fixture.written(), 2);
}

// Sealing and identity are the node's (its NodeConfig's) since BTP 2.48.0 --
// the same (source_id, boot_id) and sequence space as every other thing the
// backend sends, which is what keeps AEAD nonces unique on a keyed link.
void TestManifestClient::requestsGoOutAsTheNodesOwnIdentity() {
    Fixture fixture;
    fixture.client.requestCatalogFor(kRobot);
    fixture.client.requestFullCatalog();

    btp::DecodedFrame first{};
    btp::DecodedFrame second{};
    QVERIFY(fixture.decodeSent(0, &first));
    QVERIFY(fixture.decodeSent(1, &second));
    QCOMPARE(first.header.source_id, kClientSourceId);
    QCOMPARE(first.header.boot_id, kClientBootId);
    QCOMPARE(second.header.sequence, first.header.sequence + 1);
}

// ================================================================== cache

void TestManifestClient::notModifiedAfterARestartServesTopicsFromTheCache() {
    // The point of the cache: a second run asks "still revision 7?", the
    // robot says NOT_MODIFIED, and the catalog -- empty, this is a fresh
    // process -- is filled from what the first run stored.
    QHash<quint32, QByteArray> disk;
    {
        Fixture first;
        first.client.requestCatalogFor(kRobot);
        ManifestOptions full = twoTopicManifest();
        full.requestRef = first.replyTo(0);
        QCOMPARE(int(first.deliver(full)), int(btp::NodeRx::ManifestHandled));
        QCOMPARE(first.cfg.stores, 1);
        disk = first.cfg.cache;
    }

    Fixture second;
    second.cfg.cache = disk;
    QSignalSpy described(&second.client, &ManifestClient::sourceDescribed);
    second.client.requestCatalogFor(kRobot);
    quint32 target = 0;
    quint32 revision = 0;
    QVERIFY(second.request(0, &target, &revision));
    QCOMPARE(target, kRobot);
    QCOMPARE(revision, 7u);  // twoTopicManifest()'s revision, from the cache

    ManifestOptions unchanged;
    unchanged.flags = kFlagNotModified;
    unchanged.describedBootId = 0x00BEEF02u;  // rebooted, same catalog
    unchanged.requestRef = second.replyTo(0);
    QCOMPARE(int(second.deliver(unchanged)), int(btp::NodeRx::ManifestHandled));

    QCOMPARE(second.catalog.allSchemas().size(), 2);
    QVERIFY(second.catalog.lookup(kRobot, 0x0002, 3) != nullptr);
    QCOMPARE(second.catalog.sourceBootId(kRobot), 0x00BEEF02u);
    QCOMPARE(described.count(), 1);
    QCOMPARE(second.cfg.stores, 0);  // nothing new to store
}

void TestManifestClient::primeFromCacheRegistersSchemasButNoBoot() {
    // A hub child's topics can be listed before its robot (or even the
    // dongle) is reachable -- but a SUBSCRIBE still has to wait for a live
    // boot_id, so none is invented from the cache.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ManifestStore& store = ManifestStore::instance();
    store.setDirectory(dir.path());
    store.setEnabled(true);
    store.store(kRobot, manifestDataPayload(twoTopicManifest()));

    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);
    QVERIFY(fixture.client.primeFromCache(kRobot));
    QCOMPARE(updated.count(), 1);
    QCOMPARE(fixture.catalog.allSchemas().size(), 2);
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), 0u);
    QCOMPARE(fixture.written(), 0);  // nothing asked on the wire

    // A source with nothing stored is a clean no.
    QVERIFY(!fixture.client.primeFromCache(0x0B0B0B0Bu));
    store.clear();
    store.setDirectory(QString());
}

void TestManifestClient::primeFromCacheDoesNothingWithTheCacheOff() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ManifestStore& store = ManifestStore::instance();
    store.setDirectory(dir.path());
    store.store(kRobot, manifestDataPayload(twoTopicManifest()));
    store.setEnabled(false);

    Fixture fixture;
    QVERIFY(!fixture.client.primeFromCache(kRobot));
    QVERIFY(fixture.catalog.allSchemas().isEmpty());

    store.setEnabled(true);
    store.clear();
    store.setDirectory(QString());
}

void TestManifestClient::readCachedReturnsTopicsAndSourceInfoWithoutRegistering() {
    // What a device's settings dialog shows while it is offline: the cached
    // manifest, parsed, touching no catalog.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ManifestStore& store = ManifestStore::instance();
    store.setDirectory(dir.path());
    store.setEnabled(true);
    ManifestOptions options = twoTopicManifest();
    options.formatVersion = 2;
    options.sourceInfo = {
        {QStringLiteral("fw_version"), QStringLiteral("Firmware"), QStringLiteral("1dd9fc5")}};
    store.store(kRobot, manifestDataPayload(options));

    QVector<TelemetryTopicSchema> topics;
    QVector<DeviceInfoRecord> info;
    QVERIFY(ManifestClient::readCached(kRobot, &topics, &info));
    QCOMPARE(topics.size(), 2);
    QCOMPARE(topics.at(1).name, QStringLiteral("robot.state"));
    QCOMPARE(info.size(), 1);
    QCOMPARE(info.at(0).value, QStringLiteral("1dd9fc5"));

    QVERIFY(!ManifestClient::readCached(0x0B0B0B0Bu, &topics, &info));
    QVERIFY(!ManifestClient::readCached(0, &topics, &info));
    store.setEnabled(false);
    QVERIFY(!ManifestClient::readCached(kRobot, &topics, &info));

    store.setEnabled(true);
    store.clear();
    store.setDirectory(QString());
}

void TestManifestClient::cachedSourceNamedFindsTheRobotByItsReportedName() {
    // A direct link learns its robot's source_id only from HELLO_RESULT, so
    // offline the cache entry is found by the name its source_info reports.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ManifestStore& store = ManifestStore::instance();
    store.setDirectory(dir.path());
    store.setEnabled(true);
    const auto named = [](quint32 sourceId, const QString& name) {
        ManifestOptions options = twoTopicManifest();
        options.formatVersion = 2;
        options.describedSourceId = sourceId;
        options.sourceInfo = {{QStringLiteral("name"), QStringLiteral("Name"), name}};
        return manifestDataPayload(options);
    };
    store.store(kRobot, named(kRobot, QStringLiteral("bally")));
    store.store(0x0B0B0B0Bu, named(0x0B0B0B0Bu, QStringLiteral("other")));

    QCOMPARE(store.sourceIds().size(), 2);
    QCOMPARE(ManifestClient::cachedSourceNamed(QStringLiteral(" Bally ")), kRobot);
    QCOMPARE(ManifestClient::cachedSourceNamed(QStringLiteral("other")), 0x0B0B0B0Bu);
    QCOMPARE(ManifestClient::cachedSourceNamed(QStringLiteral("nobody")), 0u);
    QCOMPARE(ManifestClient::cachedSourceNamed(QString()), 0u);

    // Two robots with one name: neither is picked.
    store.store(0x0C0C0C0Cu, named(0x0C0C0C0Cu, QStringLiteral("bally")));
    QCOMPARE(ManifestClient::cachedSourceNamed(QStringLiteral("bally")), 0u);

    store.clear();
    store.setDirectory(QString());
}

// ============================================================== parse side

void TestManifestClient::aSuccessfulResponseRegistersEverySchemaAndTheBootId() {
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    fixture.deliver((twoTopicManifest()));

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
    fixture.deliver((twoTopicManifest()));
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
    // (With the cache on, as here, the node pairs this NOT_MODIFIED with the
    // stored copy of the first response, so the topics re-registered are the
    // real ones; with it off, parseTopics() reads no topic records from a
    // NOT_MODIFIED at all. Either way the decoy never lands.)
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);
    ManifestOptions notModified;
    notModified.flags = kFlagNotModified;
    notModified.describedBootId = 0x00BEEF02u;
    notModified.topics = {
        topicRecord(0x0002, 3, QStringLiteral("overwritten"),
                    {fieldRecord(1, 0x01, QStringLiteral("bogus"), QStringLiteral("1"))})};
    fixture.deliver((notModified));

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
    fixture.deliver((failure));

    // Not even the boot_id: a response that failed describes nothing, and
    // caching a boot from it would address later SUBSCRIBEs at a source the
    // far end just said it does not have.
    QCOMPARE(updated.count(), 0);
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), 0u);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

void TestManifestClient::anUnsupportedFormatVersionIsRejected() {
    // Formats 1, 2 and 3 are defined (commands.md 3.12; format 3 added the
    // per-field min_value/max_value range, BTP v2.45.0). A later one may
    // reorder or resize anything below the fixed prefix, so parsing on would
    // be reading a layout nobody promised.
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    ManifestOptions future = twoTopicManifest();
    future.formatVersion = 4;
    fixture.deliver((future));

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

void TestManifestClient::aFormatThreeManifestAppliesRangePerField() {
    // v2.44.0 wrote min_value/max_value for EVERY field once format 3 was
    // used, even ones with nothing to declare; v2.45.0 made it opt-in via
    // kFieldHasRange. A reader that doesn't honor the flag per field
    // desyncs the byte offset the moment it meets a field without it,
    // corrupting every field record after -- this is what actually broke
    // the catalog end-to-end (bally_OS's current_a/current_b/pwm_left/
    // pwm_right are ranged, the rest of robot.sensors/robot.flags are not).
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    ManifestOptions ranged;
    ranged.formatVersion = 3;
    ranged.topics = {
        topicRecord(0x0002, 3, QStringLiteral("robot.state"),
                    {fieldRecordWithRange(1, 0x09, QStringLiteral("current_a"),
                                          QStringLiteral("A"), 0.0, 4095.0),
                     fieldRecord(2, 0x03, QStringLiteral("ticks"), QStringLiteral("1"))}),
    };
    fixture.deliver((ranged));

    QCOMPARE(updated.count(), 1);
    const TelemetryTopicSchema* state = fixture.catalog.lookup(kRobot, 0x0002, 3);
    QVERIFY(state != nullptr);
    QCOMPARE(state->fields.size(), 2);

    const TelemetryFieldSchema* currentA = state->fieldById(1);
    QVERIFY(currentA != nullptr);
    QCOMPARE(currentA->minValue, 0.0);
    QCOMPARE(currentA->maxValue, 4095.0);

    // The field right after the ranged one is where a fixed-width reader
    // would land wrong if it always consumed 16 bytes of range regardless
    // of the flag -- ticks must still parse as itself, with no range.
    const TelemetryFieldSchema* ticks = state->fieldById(2);
    QVERIFY(ticks != nullptr);
    QCOMPARE(ticks->name, QStringLiteral("ticks"));
    QVERIFY(qIsNaN(ticks->minValue));
    QVERIFY(qIsNaN(ticks->maxValue));
}

void TestManifestClient::sourceInfoIsParsedAndReportedOnFullAndNotModified() {
    // A format-2 response carries the source_info block between source_name
    // and the topic records (commands.md 3.12). sourceInfoReported fires with
    // the key/label/value triples, in order; it rides a NOT_MODIFIED response
    // too, since source_info is not gated by config_revision. The topic
    // records that follow the block still parse.
    Fixture fixture;
    QSignalSpy info(&fixture.client, &ManifestClient::sourceInfoReported);

    ManifestOptions full = twoTopicManifest();
    full.formatVersion = 2;
    full.sourceInfo = {
        {QStringLiteral("fw_version"), QStringLiteral("Firmware"), QStringLiteral("1dd9fc5")},
        {QStringLiteral("chip"), QString(), QStringLiteral("ESP32-S3")},
    };
    fixture.deliver((full));

    QCOMPARE(info.count(), 1);
    QCOMPARE(info.at(0).at(0).toUInt(), kRobot);
    const auto records = qvariant_cast<QVector<DeviceInfoRecord>>(info.at(0).at(1));
    QCOMPARE(records.size(), 2);
    QCOMPARE(records.at(0).key, QStringLiteral("fw_version"));
    QCOMPARE(records.at(0).label, QStringLiteral("Firmware"));
    QCOMPARE(records.at(0).value, QStringLiteral("1dd9fc5"));
    QCOMPARE(records.at(1).key, QStringLiteral("chip"));
    QVERIFY(records.at(1).label.isEmpty());
    QCOMPARE(records.at(1).value, QStringLiteral("ESP32-S3"));
    // The two topic records after the block still landed.
    QCOMPARE(fixture.catalog.allSchemas().size(), 2);

    // NOT_MODIFIED carries source_info too, with no topic records.
    ManifestOptions unchanged;
    unchanged.formatVersion = 2;
    unchanged.flags = kFlagNotModified;
    unchanged.sourceInfo = {
        {QStringLiteral("fw_version"), QStringLiteral("Firmware"), QStringLiteral("1dd9fc5")},
    };
    fixture.deliver((unchanged));
    QCOMPARE(info.count(), 2);
    const auto again = qvariant_cast<QVector<DeviceInfoRecord>>(info.at(1).at(1));
    QCOMPARE(again.size(), 1);
    QCOMPARE(again.at(0).value, QStringLiteral("1dd9fc5"));
}

void TestManifestClient::aTruncatedPayloadIsRejected() {
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    const QByteArray full = manifestDataPayload(twoTopicManifest());
    // Every truncation point, not just one: each field of the header is a
    // separate bounds check, and a cursor that advanced past the end on any
    // of them would read whatever follows the buffer.
    for (int length = 0; length < full.size(); ++length) {
        fixture.deliverPayload(full.left(length));
    }

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
    QCOMPARE(fixture.catalog.sourceBootId(kRobot), 0u);

    // The untruncated one still parses, so the loop above was rejecting
    // truncation rather than the payload being unparseable all along.
    fixture.deliver((twoTopicManifest()));
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
    fixture.deliver((options));

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
    fixture.deliver((options));

    QCOMPARE(fixture.catalog.allSchemas().size(), 2);
    QVERIFY(fixture.catalog.lookup(kRobot, 0x0002, 3) != nullptr);
}

void TestManifestClient::aFrameOfAnotherControlObjectIsIgnored() {
    // CONTROL carries HELLO, SUBSCRIBE and more besides MANIFEST_DATA, and
    // they all reach this slot.
    Fixture fixture;
    QSignalSpy updated(&fixture.client, &ManifestClient::catalogUpdated);

    // not MANIFEST_DATA
    fixture.deliverPayload(manifestDataPayload(twoTopicManifest()), 0x0005);

    QCOMPARE(updated.count(), 0);
    QVERIFY(fixture.catalog.allSchemas().isEmpty());
}

QTEST_MAIN(TestManifestClient)
#include "test_manifestclient.moc"
