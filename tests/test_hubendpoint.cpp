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
                       cobsWrapped ? btp::TransportProfile::Serial : btp::TransportProfile::EspNow,
                       out) == btp::Error::Ok;
}

quint32 readLe32(const std::uint8_t* p) {
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

const quint32 kRobot = 0x0A0A0A0Au;

}  // namespace

class TestHubEndpoint : public QObject {
    Q_OBJECT

private slots:
    void childIdentityIsStableAcrossRuns();
    void childAsksItsOwnRobotForAManifestNotAnEnumeration();
    void childTerminalInputCarriesTheChildsOwnStableIdentity();
    void anOrdinarySerialBackendStillHandshakes();
    void anUnconfiguredChildAsksNobodyAnything();
};

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
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow);
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
// stable identity, not the random console one.
void TestHubEndpoint::childTerminalInputCarriesTheChildsOwnStableIdentity() {
    const quint32 childId = hubChannelSourceId(QStringLiteral("child-2"));
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow);
    backend.setHubEndpoint(childId, kRobot, QByteArray());

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
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow);
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
