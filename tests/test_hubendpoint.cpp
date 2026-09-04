#include <QSignalSpy>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <cstdint>
#include <vector>

#include "core/deviceconnection.h"
#include "devices/device.h"
#include "protocol/btpbackend.h"

using namespace traceview;

// Topico 28, desktop side: a child device stops being a reader of somebody
// else's link and becomes an ENDPOINT. It originates its own traffic, and
// every piece of it has to be addressed to its own robot -- not to the hub the
// frames happen to travel through, and not to nobody.
//
// The regression that matters just as much is the ordinary serial device: it
// is still the main case, and nothing here may change how it behaves.

namespace {

// Decodes one frame out of what a backend asked its transport to write. A
// serial-profile backend wraps in COBS; a child (ESP-NOW profile, pre-framed)
// does not, so the caller says which.
bool decodeWritten(const QByteArray& written, bool cobsWrapped, btp::DecodedFrame* out,
                   std::vector<std::uint8_t>* storage) {
    QByteArray frameBytes = written;
    if (cobsWrapped) {
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
        frameBytes = QByteArray(reinterpret_cast<const char*>(storage->data()), int(decoded));
    }
    storage->assign(frameBytes.constBegin(), frameBytes.constEnd());
    return btp::decode(storage->data(), storage->size(),
                       cobsWrapped ? btp::kSerialTransport : btp::kEspNowTransport,
                       out) == btp::Error::Ok;
}

quint32 readLe32(const std::uint8_t* p) {
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

const quint32 kRobot = 0x0A0A0A0Au;
const quint32 kRobotBoot = 0x00C0FFEEu;

void le16(QByteArray& o, quint16 v) { o.append(char(v)); o.append(char(v >> 8)); }
void le32(QByteArray& o, quint32 v) {
    o.append(char(v)); o.append(char(v >> 8)); o.append(char(v >> 16)); o.append(char(v >> 24));
}
void f64(QByteArray& o, double v) { char b[8]; std::memcpy(b, &v, 8); o.append(b, 8); }
void utf8(QByteArray& o, const QString& s) { const QByteArray b = s.toUtf8(); le16(o, quint16(b.size())); o.append(b); }

QByteArray fieldRecord(quint16 id, const QString& name) {
    QByteArray body;
    le16(body, id); le16(body, id); body.append(char(0x09)); body.append(char(0));
    le16(body, 1); le16(body, 0); f64(body, 1.0); f64(body, 0.0); le16(body, 0);
    utf8(body, name); utf8(body, QStringLiteral("u")); utf8(body, QString());
    QByteArray rec; le32(rec, quint32(body.size())); rec.append(body); return rec;
}
QByteArray topicRecord(quint16 id, quint16 ver, const QString& name) {
    QByteArray body;
    le16(body, id); le16(body, ver); body.append(char(0x05)); body.append(char(0));
    le16(body, 1); le32(body, 1000); utf8(body, name); utf8(body, QString());
    body.append(fieldRecord(1, QStringLiteral("value")));
    QByteArray rec; le32(rec, quint32(body.size())); rec.append(body); return rec;
}

// A robot MANIFEST_DATA payload matching bally_OS ManifestResponder's layout.
QByteArray robotManifestPayload(quint32 configRevision, const QVector<QByteArray>& topics) {
    QByteArray p;
    p.append(12, char(0));  // request-reference
    p.append(char(0));      // status SUCCESS
    p.append(char(0x02));   // flags CATALOG_COMPLETE
    le16(p, 0);             // errorCode
    le16(p, 1);             // formatVersion
    le16(p, 0);             // reserved
    le32(p, configRevision);
    p.append(16, char(0));  // uuid
    le32(p, kRobot);
    le32(p, kRobotBoot);
    p.append(char(1));      // role ROBOT
    p.append(char(1));      // source flags: online
    le16(p, 0); le16(p, 1); // catalogIndex, catalogCount
    le16(p, quint16(topics.size()));
    le16(p, 0);             // actionCount
    utf8(p, QStringLiteral("bally_software"));
    for (const QByteArray& t : topics) p.append(t);
    return p;
}

// The pre-framed BTP frame octets carrying `payload` as an unsealed CONTROL
// message with `objectId`, from `sourceId`.
QByteArray controlFrame(quint16 objectId, quint32 sourceId, const QByteArray& payload) {
    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = sourceId;
    header.boot_id = kRobotBoot;
    header.sequence = 1;
    header.timestamp_us = 0;
    header.object_id = objectId;
    header.fragment_index = 0;
    header.fragment_count = 1;
    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(payload.constData()),
                            std::size_t(payload.size())}};
    std::vector<std::uint8_t> out(btp::kSerialTransport.max_frame_size);
    std::size_t n = 0;
    if (btp::encode(frame, btp::kSerialTransport, out.data(), out.size(), &n) != btp::Error::Ok) {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(out.data()), int(n));
}

}  // namespace

class TestHubEndpoint : public QObject {
    Q_OBJECT

private slots:
    void childIdentityIsStableAcrossRuns();
    void childAsksItsOwnRobotForAManifestNotAnEnumeration();
    void childTerminalInputIsSealedAndCarriesTheChildsOwnStableIdentity();
    void childTerminalInputWithoutAKeyIsNotSent();
    void largeTerminalPasteIsSplitIntoAcceptableFrames();
    void anOrdinarySerialBackendStillHandshakes();
    void anUnconfiguredChildAsksNobodyAnything();
    void hubCacheMANIFESTDATAOverEspNowCeilingStillFillsTheChildsCatalog();
};

// plano 36: the catalog of a keyed hub child stayed empty because of three
// bugs in series -- the dongle stamped its cache response with its own
// source_id (never routed to the child), the child dropped it as an unsealed
// downgrade, and the child's BtpSession decoded it under the 250-octet EspNow
// ceiling even though a real 2-topic manifest is ~316. This exercises the
// TraceView half: an unsealed CONTROL/MANIFEST_DATA from the robot's
// source_id, larger than kEspNowMaxFrameSize, must reach the catalog AND
// surface the robot's identity (a child has no HELLO_RESULT).
void TestHubEndpoint::hubCacheMANIFESTDATAOverEspNowCeilingStillFillsTheChildsCatalog() {
    const quint32 childId = hubChannelSourceId(QStringLiteral("child-catalog"));
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setHubEndpoint(childId, kRobot, QByteArray(16, 'k'));  // keyed channel

    QSignalSpy catalogChanged(&backend, &Backend::catalogChanged);
    QSignalSpy identified(&backend, &Backend::deviceIdentified);
    QSignalSpy status(&backend, &Backend::statusMessage);

    const QByteArray payload = robotManifestPayload(
        5, {topicRecord(0x0001, 1, QStringLiteral("protocol.test")),
            topicRecord(0x0002, 3, QStringLiteral("robot.state"))});
    const QByteArray frame = controlFrame(/*MANIFEST_DATA=*/0x0004, kRobot, payload);
    QVERIFY(frame.size() > int(btp::kEspNowMaxFrameSize));  // the bug-3 condition

    backend.feedBytes(frame);

    QCOMPARE(catalogChanged.count(), 1);
    QVERIFY2(!backend.catalogTopics().isEmpty(), "the child's catalog must be populated");
    QCOMPARE(identified.count(), 1);
    QCOMPARE(identified.at(0).at(1).toString(),
             QStringLiteral("0x%1").arg(kRobot, 8, 16, QChar('0')).toUpper());
    for (const QList<QVariant>& call : status) {
        QVERIFY2(!call.at(0).toString().contains(QStringLiteral("nsealed")),
                 "the hub's own plaintext MANIFEST_DATA must not read as a downgrade");
    }
}

// A paste bigger than the dongle's negotiated logical payload (2048 today)
// would, as one frame, be silently truncated into the server-side pty -- the
// dongle does not reassemble serial fragments (topico 35 D.1). sendTerminalIn
// must hand it over in chunks each side accepts whole, with every byte
// preserved and in order.
void TestHubEndpoint::largeTerminalPasteIsSplitIntoAcceptableFrames() {
    BtpBackend backend;  // Serial / COBS console backend
    QSignalSpy written(&backend, &Backend::bytesToWrite);

    QByteArray paste;
    for (int i = 0; i < 3000; ++i) {
        paste.append(char('a' + (i % 26)));
    }
    backend.sendTerminalIn(paste);

    QVERIFY2(written.count() >= 2, "a 3000-byte paste must not go as a single frame");

    QByteArray reassembled;
    for (int i = 0; i < written.count(); ++i) {
        btp::DecodedFrame decoded{};
        std::vector<std::uint8_t> storage;
        QVERIFY(decodeWritten(written.at(i).at(0).toByteArray(), /*cobsWrapped=*/true, &decoded,
                              &storage));
        QCOMPARE(int(decoded.header.type), int(btp::MessageType::Terminal));
        QVERIFY2(decoded.payload.size <= 2048,
                 "each chunk must fit the dongle's negotiated logical payload");
        reassembled.append(reinterpret_cast<const char*>(decoded.payload.data),
                           int(decoded.payload.size));
    }
    QCOMPARE(reassembled, paste);  // every byte, in order
}

// The reason a child cannot use the per-run random identity the console
// channel uses: the hub routes the downstream direction by the child's
// source_id, from a table an operator fills in by hand. A value that changed
// every launch would invalidate that binding every launch, silently.
void TestHubEndpoint::childIdentityIsStableAcrossRuns() {
    const quint32 first = hubChannelSourceId(QStringLiteral("device-abc"));
    const quint32 again = hubChannelSourceId(QStringLiteral("device-abc"));
    QCOMPARE(first, again);
    QVERIFY(first != 0);

    // Distinct devices get distinct identities -- two children of one hub
    // sharing a source_id would mean one of them never receives anything.
    QVERIFY(hubChannelSourceId(QStringLiteral("device-abc")) !=
            hubChannelSourceId(QStringLiteral("device-abd")));

    // No id means not configured, and 0 is the value BTP reserves for exactly
    // that.
    QCOMPARE(hubChannelSourceId(QString()), 0u);
}

// A hub answers an enumeration with every device it has heard of; a robot has
// only its own catalog. Asking a robot to enumerate is asking a question it
// cannot answer, and the child would sit with no catalog and no error.
void TestHubEndpoint::childAsksItsOwnRobotForAManifestNotAnEnumeration() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setHubEndpoint(hubChannelSourceId(QStringLiteral("child-1")), kRobot, QByteArray());
    QCOMPARE(backend.peerSourceId(), kRobot);

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.onTransportConnectionChanged(true);

    QVERIFY2(written.count() >= 1, "a child must ask for its catalog as soon as the link is up");

    btp::DecodedFrame decoded{};
    std::vector<std::uint8_t> storage;
    QVERIFY(decodeWritten(written.at(0).at(0).toByteArray(), /*cobsWrapped=*/false, &decoded,
                          &storage));

    // CONTROL / MANIFEST_REQUEST, and the target in its payload is the robot.
    QCOMPARE(int(decoded.header.type), int(btp::MessageType::Control));
    QCOMPARE(decoded.header.object_id, quint16(0x0003));
    QVERIFY(decoded.payload.size >= 4);
    const quint32 targetSourceId = readLe32(decoded.payload.data);
    QCOMPARE(targetSourceId, kRobot);
    QVERIFY2(targetSourceId != 0, "0 is the enumeration wildcard -- a robot cannot answer it");
}

// TERMINAL_IN has no target field anywhere in its payload, which is precisely
// why the hub needs a bind table: the only thing identifying where it should
// go is the child's own source_id in the header. So that field has to be the
// stable identity, not the random console one. And, since topico 19bis, a
// child's terminal talks end to end with the robot: TERMINAL_IN is sealed with
// the channel-B key exactly like a COMMAND_REQUEST.
void TestHubEndpoint::childTerminalInputIsSealedAndCarriesTheChildsOwnStableIdentity() {
    const quint32 childId = hubChannelSourceId(QStringLiteral("child-2"));
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setHubEndpoint(childId, kRobot, QByteArray(16, 'k'));  // keyed channel

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.sendTerminalIn(QByteArrayLiteral("status\n"));
    QCOMPARE(written.count(), 1);

    btp::DecodedFrame decoded{};
    std::vector<std::uint8_t> storage;
    QVERIFY(decodeWritten(written.at(0).at(0).toByteArray(), /*cobsWrapped=*/false, &decoded,
                          &storage));

    QCOMPARE(int(decoded.header.type), int(btp::MessageType::Terminal));
    QCOMPARE(decoded.header.object_id, quint16(0x0001));
    QCOMPARE(decoded.header.source_id, childId);
    // Sealed: ENCRYPTED flag set, AES-GCM, and the payload grew by the tag.
    QVERIFY((decoded.header.flags & btp::kFlagEncrypted) != 0);
    QCOMPARE(int(btp::cipher_id(decoded.header.flags)), int(btp::CipherId::AesGcm));
    QCOMPARE(int(decoded.payload.size), int(std::strlen("status\n")) + 16);
}

// A child with no channel-B key configured yet must not put terminal
// keystrokes on the wire in the clear -- same fail-closed rule CommandClient
// and SubscriptionManager already follow for their sealed traffic.
void TestHubEndpoint::childTerminalInputWithoutAKeyIsNotSent() {
    const quint32 childId = hubChannelSourceId(QStringLiteral("child-nokey"));
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setHubEndpoint(childId, kRobot, QByteArray());  // no key

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    QSignalSpy status(&backend, &Backend::statusMessage);
    backend.sendTerminalIn(QByteArrayLiteral("status\n"));

    QCOMPARE(written.count(), 0);
    QVERIFY(status.count() >= 1);
}

// The regression that matters most: an ordinary serial device is untouched. It
// still handshakes, which is what a hub offers and a robot does not.
void TestHubEndpoint::anOrdinarySerialBackendStillHandshakes() {
    BtpBackend backend;  // the default: Serial / COBS, no hub endpoint set
    QCOMPARE(backend.peerSourceId(), 0u);

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.onTransportConnectionChanged(true);

    // BtpHandshake opens with the ENTER line, in plain text and not a frame --
    // the marker that the console path is still the one being taken.
    QVERIFY2(written.count() >= 1, "a serial backend must still start its handshake");
    const QByteArray first = written.at(0).at(0).toByteArray();
    QVERIFY2(first.startsWith("BTP/"), qPrintable(QStringLiteral("expected an ENTER line, got: %1")
                                                      .arg(QString::fromLatin1(first.left(16)))));
}

// A child whose robot was never configured must ask nothing at all. Peer 0 is
// the "not configured" value a project file missing the field falls back to,
// and a child that guessed a target would talk to whichever robot answered.
void TestHubEndpoint::anUnconfiguredChildAsksNobodyAnything() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setHubEndpoint(hubChannelSourceId(QStringLiteral("child-3")), 0, QByteArray());
    QCOMPARE(backend.peerSourceId(), 0u);

    // With no peer this is not a child at all, so it takes the console path --
    // which over a hub channel reaches nobody, and that is the safe outcome:
    // nothing is addressed to a robot that was never chosen.
    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.onTransportConnectionChanged(true);
    for (int i = 0; i < written.count(); ++i) {
        btp::DecodedFrame decoded{};
        std::vector<std::uint8_t> storage;
        if (!decodeWritten(written.at(i).at(0).toByteArray(), /*cobsWrapped=*/false, &decoded,
                           &storage)) {
            continue;  // the ENTER line is text, not a frame
        }
        QVERIFY2(decoded.header.object_id != 0x0003,
                 "no manifest request may be sent when no robot is configured");
    }
}

QTEST_MAIN(TestHubEndpoint)
#include "test_hubendpoint.moc"
