#include <QSignalSpy>
#include <QtTest>
#include <btp/codec.hpp>
#include <btp/stream.hpp>
#include <vector>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/hubbinder.h"
#include "protocol/protocolrouter.h"

using traceview::BtpFrame;
using traceview::BtpSession;
using traceview::HubBinder;
using traceview::ProtocolRouter;

// HubBinder is what replaced a human typing "hub -bind" into the dongle's
// console. The properties worth pinning are the ones whose absence is SILENT
// rather than loud, because silence is the whole failure mode this class was
// written to remove:
//
//  - a binding survives a dropped session and is re-issued on the next one
//    (the dongle's HubRegistry is RAM-only, so a dongle reboot empties it
//    while this desktop still believes its children are routed);
//  - a binding the dongle REJECTS is reported, naming the device, instead of
//    leaving a child that looks connected and routes nowhere;
//  - the exact spelling of the command line, which two separate parsers on
//    the firmware side read (see sourceIdsAreHexPrefixed... below).

namespace {

constexpr quint16 kCommandResultObjectId = 0x0002;
constexpr quint8 kStatusSuccess = 0x00;
constexpr quint8 kStatusFailure = 0x01;

constexpr quint32 kDongleSourceId = 0x0D0D0D0Du;
constexpr quint32 kDongleBootId = 0x00C0FFEEu;
constexpr quint32 kChildA = 0x11112222u;
constexpr quint32 kRobotA = 0x33334444u;
constexpr quint32 kChildB = 0x55556666u;
constexpr quint32 kRobotB = 0x77778888u;

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

// Same helper test_clocksync uses: unwrap one COBS-framed serial frame out of
// what the session asked its transport to write.
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

QString commandLineOf(const btp::DecodedFrame& frame) {
    constexpr std::size_t kRequestPrefixSize = 20;
    if (frame.payload.size < kRequestPrefixSize)
        return QString();
    return QString::fromUtf8(reinterpret_cast<const char*>(frame.payload.data) + kRequestPrefixSize,
                             int(frame.payload.size - kRequestPrefixSize));
}

BtpFrame makeResult(quint32 requestSourceId, quint32 requestBootId, quint32 replyToSequence,
                    quint8 status, const QByteArray& message) {
    QByteArray payload;
    appendLe32(payload, requestSourceId);
    appendLe32(payload, requestBootId);
    appendLe32(payload, replyToSequence);
    payload.append(4, char(0));
    payload.append(char(status));
    payload.append(3, char(0));
    appendLe16(payload, quint16(message.size()));
    payload.append(message);

    BtpFrame frame;
    frame.type = btp::MessageType::Command;
    frame.objectId = kCommandResultObjectId;
    frame.payload = payload;
    return frame;
}

struct Fixture {
    BtpSession session{btp::TransportProfile::Serial};
    ProtocolRouter router;
    HubBinder binder{&session, &router};
    QSignalSpy written{&session, &BtpSession::bytesToWrite};

    bool writtenFrame(int index, btp::DecodedFrame* out, std::vector<std::uint8_t>* storage) const {
        if (index >= written.count())
            return false;
        return decodeWritten(written.at(index).at(0).toByteArray(), out, storage);
    }

    QString commandAt(int index) const {
        btp::DecodedFrame frame{};
        std::vector<std::uint8_t> storage;
        if (!writtenFrame(index, &frame, &storage))
            return QString();
        return commandLineOf(frame);
    }

    // Every command line written so far, so a test can assert on a set of
    // re-issued bindings without depending on QHash iteration order.
    QStringList allCommands() const {
        QStringList out;
        for (int i = 0; i < written.count(); ++i)
            out.append(commandAt(i));
        return out;
    }

    bool requestIdentity(int index, quint32* sourceId, quint32* bootId, quint32* sequence) const {
        btp::DecodedFrame frame{};
        std::vector<std::uint8_t> storage;
        if (!writtenFrame(index, &frame, &storage))
            return false;
        *sourceId = frame.header.source_id;
        *bootId = frame.header.boot_id;
        *sequence = frame.header.sequence;
        return true;
    }
};

bool replyTo(Fixture& fixture, int requestIndex, quint8 status, const QByteArray& message) {
    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    if (!fixture.requestIdentity(requestIndex, &sourceId, &bootId, &sequence))
        return false;
    fixture.router.onFrameReceived(makeResult(sourceId, bootId, sequence, status, message));
    return true;
}

}  // namespace

class TestHubBinder : public QObject {
    Q_OBJECT

private slots:
    void nothingIsSentBeforeASessionExists();
    void aBindingDeclaredBeforeTheSessionIsIssuedWhenItComesUp();
    void aBindingDeclaredDuringASessionGoesOutImmediately();
    void sourceIdsAreHexPrefixedSoTheDongleCannotReadThemAsDecimal();
    void everyBindingIsReissuedOnEachNewSession();
    void aDroppedSessionKeepsTheIntent();
    void unbindingStopsItFromBeingReissued();
    void aRejectedBindingIsReportedAndNamesTheDevice();
    void anAcceptedBindingIsQuiet();
    void aZeroSourceIdIsNeverSent();
};

void TestHubBinder::nothingIsSentBeforeASessionExists() {
    Fixture fixture;
    fixture.binder.bindChild(kChildA, kRobotA);
    QCOMPARE(fixture.written.count(), 0);
}

void TestHubBinder::aBindingDeclaredBeforeTheSessionIsIssuedWhenItComesUp() {
    Fixture fixture;
    fixture.binder.bindChild(kChildA, kRobotA);
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);

    QCOMPARE(fixture.written.count(), 1);
    QCOMPARE(fixture.commandAt(0), QStringLiteral("hub -bind 0x11112222, 0x33334444"));
}

void TestHubBinder::aBindingDeclaredDuringASessionGoesOutImmediately() {
    Fixture fixture;
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    QCOMPARE(fixture.written.count(), 0);

    fixture.binder.bindChild(kChildA, kRobotA);
    QCOMPARE(fixture.written.count(), 1);
    QCOMPARE(fixture.commandAt(0), QStringLiteral("hub -bind 0x11112222, 0x33334444"));
}

// The dash belongs to the COMMAND and the 0x belongs to the ARGUMENTS, and
// both are load-bearing on the firmware side. TinyShell's parse_command()
// locates the command name by searching for a dash and leaves it empty when
// there is none, which comes back as FUNCTION_NOT_FOUND -- the bug
// "dongle clock" had for as long as it existed. And bally_dongle's
// parseSourceId() tries strtoul base 0 first, where a bare "33334444" reads
// as DECIMAL and would bind a completely different number with no error at
// all.
void TestHubBinder::sourceIdsAreHexPrefixedSoTheDongleCannotReadThemAsDecimal() {
    Fixture fixture;
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    fixture.binder.bindChild(kChildA, kRobotA);

    const QString line = fixture.commandAt(0);
    QVERIFY2(line.startsWith(QStringLiteral("hub -bind ")), qPrintable(line));
    QVERIFY2(line.contains(QStringLiteral("0x11112222")), qPrintable(line));
    QVERIFY2(line.contains(QStringLiteral("0x33334444")), qPrintable(line));
}

// The dongle's binding table is RAM-only: a dongle reboot empties it while
// this desktop still believes its children are routed. Re-issuing everything
// on each session is what closes that gap, and it is the normal path rather
// than an error path (HubRegistry::bind() re-binds in place, so repeating a
// binding costs one command and never a second slot).
void TestHubBinder::everyBindingIsReissuedOnEachNewSession() {
    Fixture fixture;
    fixture.binder.bindChild(kChildA, kRobotA);
    fixture.binder.bindChild(kChildB, kRobotB);
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    QCOMPARE(fixture.written.count(), 2);

    fixture.binder.onSessionLost();
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId + 1);

    QCOMPARE(fixture.written.count(), 4);
    const QStringList commands = fixture.allCommands();
    QCOMPARE(commands.count(QStringLiteral("hub -bind 0x11112222, 0x33334444")), 2);
    QCOMPARE(commands.count(QStringLiteral("hub -bind 0x55556666, 0x77778888")), 2);
}

void TestHubBinder::aDroppedSessionKeepsTheIntent() {
    Fixture fixture;
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    fixture.binder.bindChild(kChildA, kRobotA);
    fixture.binder.onSessionLost();

    // Nothing goes out with no session -- and, critically, the intent was not
    // discarded along with it.
    const int beforeReconnect = fixture.written.count();
    fixture.binder.bindChild(kChildB, kRobotB);
    QCOMPARE(fixture.written.count(), beforeReconnect);

    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId + 1);
    const QStringList commands = fixture.allCommands();
    QVERIFY(commands.contains(QStringLiteral("hub -bind 0x11112222, 0x33334444")));
    QVERIFY(commands.contains(QStringLiteral("hub -bind 0x55556666, 0x77778888")));
}

void TestHubBinder::unbindingStopsItFromBeingReissued() {
    Fixture fixture;
    fixture.binder.bindChild(kChildA, kRobotA);
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    fixture.binder.unbindChild(kChildA);

    QCOMPARE(fixture.commandAt(1), QStringLiteral("hub -unbind 0x11112222"));

    const int afterUnbind = fixture.written.count();
    fixture.binder.onSessionLost();
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId + 1);
    QCOMPARE(fixture.written.count(), afterUnbind);  // nothing left to re-issue
}

// The one outcome that must never be quiet: a child whose binding the dongle
// refused looks connected and routes nowhere, which is exactly the silent
// failure this class exists to remove.
void TestHubBinder::aRejectedBindingIsReportedAndNamesTheDevice() {
    Fixture fixture;
    QSignalSpy statusSpy(&fixture.binder, &HubBinder::statusMessage);
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    fixture.binder.bindChild(kChildA, kRobotA);

    QVERIFY(replyTo(fixture, 0, kStatusFailure, QByteArray("tabela de vinculo cheia")));

    QCOMPARE(statusSpy.count(), 1);
    const QString text = statusSpy.at(0).at(0).toString();
    QVERIFY2(text.contains(QStringLiteral("11112222")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("tabela de vinculo cheia")), qPrintable(text));
}

void TestHubBinder::anAcceptedBindingIsQuiet() {
    Fixture fixture;
    QSignalSpy statusSpy(&fixture.binder, &HubBinder::statusMessage);
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    fixture.binder.bindChild(kChildA, kRobotA);

    QVERIFY(replyTo(fixture, 0, kStatusSuccess, QByteArray("[hub] vinculo ok")));
    QCOMPARE(statusSpy.count(), 0);
}

// Zero is not a legal BTP source_id, and here it also spells "not configured
// yet" -- a hub child whose robot has not been picked. Issuing it would only
// earn a rejection.
void TestHubBinder::aZeroSourceIdIsNeverSent() {
    Fixture fixture;
    fixture.binder.onSessionEstablished(kDongleSourceId, kDongleBootId);
    fixture.binder.bindChild(0, kRobotA);
    fixture.binder.bindChild(kChildA, 0);
    QCOMPARE(fixture.written.count(), 0);
}

QTEST_MAIN(TestHubBinder)
#include "test_hubbinder.moc"
