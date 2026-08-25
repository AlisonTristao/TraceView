#include <QSignalSpy>
#include <QtTest/QtTest>

#include <btp/codec.hpp>

#include <cstdint>
#include <vector>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/clocksync.h"
#include "protocol/protocolrouter.h"

using namespace traceview;

// ClockSync replaces the dongle's old boot-time "informe data/hora" prompt
// with a COMMAND_REQUEST/COMMAND_RESULT round trip: ask "dongle clock", and
// only if the answer has drifted past the tolerance, correct it with
// "dongle set_clock".
//
// What is worth pinning here is the correlation and the drift decision --
// both are the kind of thing that looks right and silently is not. A reply
// matched too loosely lets some other tool's COMMAND_RESULT on the same
// session drive this one's state machine; a tolerance applied in the wrong
// direction means either never correcting a wrong clock or rewriting a
// correct one on every connect.

namespace {

constexpr quint16 kCommandRequestObjectId = 0x0001;
constexpr quint16 kCommandResultObjectId = 0x0002;
constexpr quint8 kStatusSuccess = 0x00;
constexpr quint8 kStatusFailure = 0x01;

// The dongle's own identity, as HELLO_RESULT reports it.
constexpr quint32 kDongleSourceId = 0x0D0D0D0Du;
constexpr quint32 kDongleBootId = 0x00C0FFEEu;

// Matches ClockSync's own kDriftToleranceSecs.
constexpr qint64 kToleranceSecs = 5;

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

quint32 readLe32(const QByteArray& data, int offset) {
    return quint32(quint8(data.at(offset))) | (quint32(quint8(data.at(offset + 1))) << 8) |
           (quint32(quint8(data.at(offset + 2))) << 16) | (quint32(quint8(data.at(offset + 3))) << 24);
}

// Decodes one COBS-wrapped serial-profile frame out of what the session
// asked its transport to write -- same shape as test_hubendpoint's own
// helper.
bool decodeWritten(const QByteArray& written, btp::DecodedFrame* out, std::vector<std::uint8_t>* storage) {
    if (written.size() < 3 || written.at(0) != char(0) || written.at(written.size() - 1) != char(0)) {
        return false;
    }
    const QByteArray block = written.mid(1, written.size() - 2);
    storage->assign(btp::kSerialMaxFrameSize, 0);
    std::size_t decoded = 0;
    if (btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(block.constData()), std::size_t(block.size()),
                          storage->data(), storage->size(), &decoded) != btp::CobsError::Ok) {
        return false;
    }
    storage->resize(decoded);
    return btp::decode(storage->data(), storage->size(), btp::TransportProfile::Serial, out) == btp::Error::Ok;
}

// The shell command line carried by a COMMAND_REQUEST payload
// (commands.md section 2: a 20-byte prefix, then command_size bytes).
QString commandLineOf(const btp::DecodedFrame& frame) {
    constexpr std::size_t kRequestPrefixSize = 20;
    if (frame.payload.size < kRequestPrefixSize) return QString();
    return QString::fromUtf8(reinterpret_cast<const char*>(frame.payload.data) + kRequestPrefixSize,
                              int(frame.payload.size - kRequestPrefixSize));
}

// A COMMAND_RESULT echoing back the request's (source_id, boot_id,
// sequence), which is the whole of this protocol's correlation.
BtpFrame makeResult(quint32 requestSourceId, quint32 requestBootId, quint32 replyToSequence, quint8 status,
                     const QByteArray& message) {
    QByteArray payload;
    appendLe32(payload, requestSourceId);
    appendLe32(payload, requestBootId);
    appendLe32(payload, replyToSequence);
    payload.append(4, char(0));      // bytes 12..15, unused here
    payload.append(char(status));    // byte 16
    payload.append(3, char(0));      // bytes 17..19
    appendLe16(payload, quint16(message.size()));  // message_size at 20..21
    payload.append(message);

    BtpFrame frame;
    frame.type = btp::MessageType::Command;  // ProtocolRouter dispatches on this
    frame.objectId = kCommandResultObjectId;
    frame.payload = payload;
    return frame;
}

// Everything one test needs, wired the way BtpBackend wires it.
struct Fixture {
    BtpSession session{btp::TransportProfile::Serial};
    ProtocolRouter router;
    ClockSync sync{&session, &router};
    QSignalSpy written{&session, &BtpSession::bytesToWrite};

    // Decodes the Nth frame the session was asked to write.
    bool writtenFrame(int index, btp::DecodedFrame* out, std::vector<std::uint8_t>* storage) const {
        if (index >= written.count()) return false;
        return decodeWritten(written.at(index).at(0).toByteArray(), out, storage);
    }

    // The (source_id, boot_id, sequence) of the Nth written request -- the
    // private per-process identity ClockSync generates for itself, which a
    // reply has to echo back exactly.
    bool requestIdentity(int index, quint32* sourceId, quint32* bootId, quint32* sequence) const {
        btp::DecodedFrame frame{};
        std::vector<std::uint8_t> storage;
        if (!writtenFrame(index, &frame, &storage)) return false;
        *sourceId = frame.header.source_id;
        *bootId = frame.header.boot_id;
        *sequence = frame.header.sequence;
        return true;
    }
};

// Feeds a reply that correlates with the fixture's Nth outstanding request.
bool replyTo(Fixture& fixture, int requestIndex, quint8 status, const QByteArray& message) {
    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    if (!fixture.requestIdentity(requestIndex, &sourceId, &bootId, &sequence)) return false;
    fixture.router.onFrameReceived(makeResult(sourceId, bootId, sequence, status, message));
    return true;
}

QByteArray epochReply(qint64 epochSecs) {
    return QByteArray("ok epoch=") + QByteArray::number(epochSecs);
}

}  // namespace

class TestClockSync : public QObject {
    Q_OBJECT

private slots:
    void sessionEstablishedAsksTheDongleForItsClock();
    void aClockWithinToleranceIsLeftAlone();
    void aDongleRunningBehindIsCorrected();
    void aDongleRunningAheadIsCorrectedToo();
    void aReplyFromAnotherClientIsIgnored();
    void aReplyToAnEarlierSequenceIsIgnored();
    void aFailedCommandIsReportedAndNotActedOn();
    void anUnparseableReplyCorrectsNothing();
    void nothingIsSentBeforeASessionExists();
};

void TestClockSync::sessionEstablishedAsksTheDongleForItsClock() {
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    QCOMPARE(fixture.written.count(), 1);

    btp::DecodedFrame frame{};
    std::vector<std::uint8_t> storage;
    QVERIFY(fixture.writtenFrame(0, &frame, &storage));
    QCOMPARE(int(frame.header.type), int(btp::MessageType::Command));
    QCOMPARE(frame.header.object_id, kCommandRequestObjectId);
    QCOMPARE(commandLineOf(frame), QStringLiteral("dongle clock"));

    // The request has to be addressed to the dongle that just handshaked:
    // SerialMux::handleCommandRequest on the firmware side refuses one whose
    // target doesn't match its own (source_id, boot_id).
    const QByteArray payload(reinterpret_cast<const char*>(frame.payload.data), int(frame.payload.size));
    QCOMPARE(readLe32(payload, 0), kDongleSourceId);
    QCOMPARE(readLe32(payload, 4), kDongleBootId);
}

void TestClockSync::aClockWithinToleranceIsLeftAlone() {
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);
    QCOMPARE(fixture.written.count(), 1);

    // Inside the tolerance: round-trip jitter explains this, and rewriting
    // the clock on every connect over a couple of seconds of noise would be
    // worse than leaving it.
    QVERIFY(replyTo(fixture, 0, kStatusSuccess,
                     epochReply(QDateTime::currentSecsSinceEpoch() - (kToleranceSecs - 1))));

    QCOMPARE(fixture.written.count(), 1);  // no set_clock followed
}

void TestClockSync::aDongleRunningBehindIsCorrected() {
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    QVERIFY(replyTo(fixture, 0, kStatusSuccess,
                     epochReply(QDateTime::currentSecsSinceEpoch() - (kToleranceSecs + 3600))));

    QCOMPARE(fixture.written.count(), 2);
    btp::DecodedFrame frame{};
    std::vector<std::uint8_t> storage;
    QVERIFY(fixture.writtenFrame(1, &frame, &storage));
    QVERIFY(commandLineOf(frame).startsWith(QStringLiteral("dongle set_clock ")));

    // Reported to the user once the dongle confirms, not optimistically at
    // send time.
    QSignalSpy status(&fixture.sync, &ClockSync::statusMessage);
    QVERIFY(replyTo(fixture, 1, kStatusSuccess, QByteArray("clock set")));
    QCOMPARE(status.count(), 1);
}

void TestClockSync::aDongleRunningAheadIsCorrectedToo() {
    // The drift check is on the absolute difference: a dongle whose clock
    // runs fast is exactly as wrong as one running slow, and a sign error
    // here would silently only ever fix one of the two.
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    QVERIFY(replyTo(fixture, 0, kStatusSuccess,
                     epochReply(QDateTime::currentSecsSinceEpoch() + (kToleranceSecs + 3600))));

    QCOMPARE(fixture.written.count(), 2);
    btp::DecodedFrame frame{};
    std::vector<std::uint8_t> storage;
    QVERIFY(fixture.writtenFrame(1, &frame, &storage));
    QVERIFY(commandLineOf(frame).startsWith(QStringLiteral("dongle set_clock ")));
}

void TestClockSync::aReplyFromAnotherClientIsIgnored() {
    // COMMAND_RESULT is broadcast on the session, so some other tool's reply
    // can reach this router. Correlation is (source_id, boot_id, sequence)
    // echoed back; a mismatch on the identity must not advance this state
    // machine.
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    QVERIFY(fixture.requestIdentity(0, &sourceId, &bootId, &sequence));

    fixture.router.onFrameReceived(makeResult(sourceId ^ 0xFFFFu, bootId, sequence, kStatusSuccess,
                                               epochReply(QDateTime::currentSecsSinceEpoch() - 7200)));
    QCOMPARE(fixture.written.count(), 1);

    fixture.router.onFrameReceived(makeResult(sourceId, bootId ^ 0xFFFFu, sequence, kStatusSuccess,
                                               epochReply(QDateTime::currentSecsSinceEpoch() - 7200)));
    QCOMPARE(fixture.written.count(), 1);

    // The genuine reply still lands afterwards -- the rejections above must
    // not have consumed the outstanding request.
    QVERIFY(replyTo(fixture, 0, kStatusSuccess, epochReply(QDateTime::currentSecsSinceEpoch() - 7200)));
    QCOMPARE(fixture.written.count(), 2);
}

void TestClockSync::aReplyToAnEarlierSequenceIsIgnored() {
    // A late answer to an already-timed-out attempt carries a stale
    // sequence. Accepting it would act on an epoch measured who-knows-how-
    // long ago.
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    QVERIFY(fixture.requestIdentity(0, &sourceId, &bootId, &sequence));

    fixture.router.onFrameReceived(makeResult(sourceId, bootId, sequence - 1, kStatusSuccess,
                                               epochReply(QDateTime::currentSecsSinceEpoch() - 7200)));
    QCOMPARE(fixture.written.count(), 1);
}

void TestClockSync::aFailedCommandIsReportedAndNotActedOn() {
    Fixture fixture;
    QSignalSpy status(&fixture.sync, &ClockSync::statusMessage);
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    QVERIFY(replyTo(fixture, 0, kStatusFailure, QByteArray("unknown command")));

    QCOMPARE(status.count(), 1);
    QVERIFY(status.at(0).at(0).toString().contains(QStringLiteral("unknown command")));
    QCOMPARE(fixture.written.count(), 1);  // no set_clock off a failed read
}

void TestClockSync::anUnparseableReplyCorrectsNothing() {
    // A dongle whose "dongle clock" output doesn't carry epoch=<n> is one
    // this can't reason about. Better to leave its clock alone than to
    // correct against a number that wasn't there.
    Fixture fixture;
    fixture.sync.onSessionEstablished(kDongleSourceId, kDongleBootId);

    QVERIFY(replyTo(fixture, 0, kStatusSuccess, QByteArray("ok but nothing parseable here")));
    QCOMPARE(fixture.written.count(), 1);
}

void TestClockSync::nothingIsSentBeforeASessionExists() {
    // No handshake yet means no target to address a COMMAND_REQUEST to, and
    // commands.md gives no meaning to one aimed at (0, 0).
    Fixture fixture;
    QCOMPARE(fixture.written.count(), 0);

    // A stray COMMAND_RESULT arriving in that state must not be treated as
    // a reply to something never sent.
    fixture.router.onFrameReceived(makeResult(1, 1, 1, kStatusSuccess, epochReply(0)));
    QCOMPARE(fixture.written.count(), 0);
}

QTEST_MAIN(TestClockSync)
#include "test_clocksync.moc"
