#include <QtTest>
#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btphandshake.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"

using traceview::BtpFrame;
using traceview::BtpHandshake;
using traceview::BtpSession;
using traceview::ProtocolRouter;

namespace {

constexpr quint16 kControlHello = 0x0001;
constexpr quint16 kControlHelloResult = 0x0002;
constexpr int kHelloPayloadFixedSize = 40;  // offset where `versions` starts

void appendLe(QByteArray& out, quint32 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// Drives a real BtpHandshake over a real BtpSession pair: the HELLO frame it
// sends is genuinely COBS-encoded and decoded back, and the plain-text
// ENTER/READY exchange (session-and-terminal.md section 3) is driven exactly
// as
// SerialManager's raw bytes would, not mocked.
class Harness {
public:
    // enterTimeoutMs/maxEnterAttempts default to "use the production policy"
    // (-1); the retry tests pass a few milliseconds instead, so they exercise
    // the whole budget without waiting the real 20 seconds out.
    explicit Harness(int enterTimeoutMs = -1, int maxEnterAttempts = -1)
        : handshake(&outbound, &router, nullptr, enterTimeoutMs, maxEnterAttempts) {
        QObject::connect(&outbound, &BtpSession::bytesToWrite, &loopback, &BtpSession::feedBytes);
        QObject::connect(&loopback, &BtpSession::frameReceived, &loopback,
                         [this](const BtpFrame& frame) { sent.append(frame); });
        QObject::connect(&handshake, &BtpHandshake::bytesToWrite, &handshake,
                         [this](const QByteArray& data) {
                             enterLine = data;
                             enterLines.append(data);
                         });
    }

    // The nonce out of an ENTER line: "BTP/1 ENTER <16 hex>\r\n".
    static QByteArray nonceOf(const QByteArray& line) {
        QByteArray nonce = line.mid(QByteArray("BTP/1 ENTER ").size());
        nonce.chop(2);  // trailing \r\n
        return nonce;
    }

    // Runs the ENTER/READY handshake far enough that sendHello() fires, and
    // returns the HELLO frame captured on the other end of the loopback.
    const BtpFrame& sendHelloAndCapture() {
        handshake.start();
        // enterLine == "BTP/1 ENTER <16-hex-char nonce>\r\n"
        handshake.feedRawBytes("BTP/1 READY " + nonceOf(enterLine) + "\r\n");
        return sent.last();
    }

    // Delivers a HELLO_RESULT exactly as ProtocolRouter would after decoding
    // one off the wire; the request-reference prefix (offset 0-11) is left
    // zeroed since BtpHandshake doesn't correlate on it.
    void deliverHelloResult(quint8 status, quint8 selectedVersion) {
        QByteArray payload(12, '\0');  // request reference, unchecked
        payload.append(static_cast<char>(status));
        payload.append(static_cast<char>(selectedVersion));
        appendLe(payload, 0, 2);  // error_code

        BtpFrame frame;
        frame.type = btp::MessageType::Control;
        frame.sourceId = 0xDEADBEEF;
        frame.bootId = 0x12345678;
        frame.sequence = 1;
        frame.objectId = kControlHelloResult;
        frame.payload = payload;
        router.onFrameReceived(frame);
    }

    BtpSession outbound;
    BtpSession loopback;
    ProtocolRouter router;
    BtpHandshake handshake{&outbound, &router};
    QVector<BtpFrame> sent;
    QByteArray enterLine;       // the most recent one
    QVector<QByteArray> enterLines;  // every one, in order
};

class TestBtpHandshake : public QObject {
    Q_OBJECT

private slots:
    void helloAdvertisesTheLibrarysFullSupportedVersionRange();
    void sessionEstablishedWhenSelectedVersionIsWithinTheAdvertisedRange();
    void sessionFailsWhenSelectedVersionIsOutsideTheAdvertisedRange();
    void enterIsResentWithTheSameNonceWhenNoReadyArrives();
    void aReadyArrivingAfterAnEarlierTimeoutStillEstablishesTheSession();
    void silenceThroughTheWholeBudgetFailsAndAsksForRecovery();
    void consoleLineAfterEstablishedIsTreatedAsSessionLoss();
    void consoleLineBeforeEstablishedIsNotAFailure();
};

void TestBtpHandshake::helloAdvertisesTheLibrarysFullSupportedVersionRange() {
    Harness h;
    const BtpFrame& hello = h.sendHelloAndCapture();

    QCOMPARE(hello.type, btp::MessageType::Control);
    QCOMPARE(hello.objectId, kControlHello);

    // Not hardcoded to a single version: `versions` (from offset 40) must
    // list every envelope version this build's btp::codec supports, so the
    // dongle -- which picks the highest one common to both sides -- sees our
    // real ceiling.
    const quint8 expectedCount = btp::kMaximumProtocolVersion - btp::kMinimumProtocolVersion + 1;
    QCOMPARE(quint8(hello.payload.at(1)), expectedCount);  // version_count
    QCOMPARE(hello.payload.size(), kHelloPayloadFixedSize + int(expectedCount));

    for (quint8 version = btp::kMinimumProtocolVersion; version <= btp::kMaximumProtocolVersion;
         ++version) {
        const int offset = kHelloPayloadFixedSize + (version - btp::kMinimumProtocolVersion);
        QCOMPARE(quint8(hello.payload.at(offset)), version);
    }
}

void TestBtpHandshake::sessionEstablishedWhenSelectedVersionIsWithinTheAdvertisedRange() {
    Harness h;
    h.sendHelloAndCapture();
    QSignalSpy establishedSpy(&h.handshake, &BtpHandshake::sessionEstablished);
    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::sessionFailed);

    h.deliverHelloResult(/*status=*/0x00, btp::kMaximumProtocolVersion);

    QCOMPARE(establishedSpy.size(), 1);
    QCOMPARE(failedSpy.size(), 0);
}

void TestBtpHandshake::sessionFailsWhenSelectedVersionIsOutsideTheAdvertisedRange() {
    Harness h;
    h.sendHelloAndCapture();
    QSignalSpy establishedSpy(&h.handshake, &BtpHandshake::sessionEstablished);
    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::sessionFailed);

    // A peer claiming a version we never offered is a peer not honoring the
    // negotiation -- not a version this client can silently go along with.
    h.deliverHelloResult(/*status=*/0x00, quint8(btp::kMaximumProtocolVersion + 1));

    QCOMPARE(establishedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 1);
}

// A lost ENTER is the common failure here -- the first line is written while
// the dongle is still deep in AppRuntime::begin() (SD, SQLite, ESP-NOW) and
// not yet reading the port, or into a device that just power-cycled.
// Resending is the whole recovery.
void TestBtpHandshake::enterIsResentWithTheSameNonceWhenNoReadyArrives() {
    Harness h(/*enterTimeoutMs=*/150, /*maxEnterAttempts=*/5);
    h.handshake.start();
    QCOMPARE(h.enterLines.size(), 1);

    // ">= 2", never "== 2": the retry timer keeps firing, so under load the
    // count can step past 2 between two polls of the QTRY loop and an
    // equality check would then wait out its whole timeout and fail on a
    // retry budget that is working exactly as intended.
    QTRY_VERIFY_WITH_TIMEOUT(h.enterLines.size() >= 2, 4000);

    // The SAME nonce, not a fresh one: a READY answering the first ENTER can
    // still arrive late, and it has to keep matching. See m_enterNonce.
    QCOMPARE(Harness::nonceOf(h.enterLines.at(1)), Harness::nonceOf(h.enterLines.at(0)));
}

// The reason the nonce is stable across retries, stated as a test: a slow
// dongle answers the FIRST ENTER after that ENTER's own timeout already
// fired. With a fresh nonce per attempt that reply would match nothing and
// the handshake would stall until the budget ran out, even though the peer
// did reply.
void TestBtpHandshake::aReadyArrivingAfterAnEarlierTimeoutStillEstablishesTheSession() {
    Harness h(/*enterTimeoutMs=*/150, /*maxEnterAttempts=*/5);
    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::sessionFailed);

    h.handshake.start();
    const QByteArray firstNonce = Harness::nonceOf(h.enterLines.at(0));
    QTRY_VERIFY_WITH_TIMEOUT(h.enterLines.size() >= 2, 4000);

    // Answering the FIRST attempt, long after its timer expired.
    h.handshake.feedRawBytes("BTP/1 READY " + firstNonce + "\r\n");

    QCOMPARE(h.sent.size(), 1);  // the HELLO went out, so the READY was accepted
    QCOMPARE(h.sent.last().objectId, kControlHello);

    QSignalSpy establishedSpy(&h.handshake, &BtpHandshake::sessionEstablished);
    h.deliverHelloResult(/*status=*/0x00, btp::kMaximumProtocolVersion);
    QCOMPARE(establishedSpy.size(), 1);
    QCOMPARE(failedSpy.size(), 0);
}

// Exhausting the budget is a real failure and has to say so exactly once --
// BtpBackend turns sessionFailed into the recovery that recycles the port
// (Backend::sessionRecoveryNeeded), and a second emission would recycle a
// connection that is already being reopened.
void TestBtpHandshake::silenceThroughTheWholeBudgetFailsAndAsksForRecovery() {
    Harness h(/*enterTimeoutMs=*/100, /*maxEnterAttempts=*/3);
    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::sessionFailed);

    h.handshake.start();

    QTRY_VERIFY_WITH_TIMEOUT(failedSpy.size() >= 1, 4000);
    // Exactly the configured number of attempts, and not one more after the
    // failure: the timer is single-shot and onEnterTimeout() stops retrying
    // once the state left AwaitingReady.
    QCOMPARE(h.enterLines.size(), 3);
    // Well past another interval: nothing more is written once the budget is
    // spent, and the failure is announced exactly once.
    QTest::qWait(300);
    QCOMPARE(h.enterLines.size(), 3);
    QCOMPARE(failedSpy.size(), 1);
}

// The dongle prints "BTP/1 CONSOLE\r\n" whenever it drops the session back to
// console (its inactivity watchdog, a SESSION_CLOSE, a bench human). The
// transport stays up and BtpSession has no watchdog, so without this the
// desktop would sit on a dead session forever. Fold it into the same
// sessionFailed -> recovery path an exhausted handshake uses.
void TestBtpHandshake::consoleLineAfterEstablishedIsTreatedAsSessionLoss() {
    Harness h;
    h.sendHelloAndCapture();
    h.deliverHelloResult(/*status=*/0x00, btp::kMaximumProtocolVersion);

    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::sessionFailed);
    // Arrives glued to the tail of a binary frame, exactly as it would on the
    // wire -- the scan is over raw bytes, not parsed frames.
    h.handshake.feedRawBytes(QByteArrayLiteral("\x00\x11\x22") + "BTP/1 CONSOLE\r\n");

    QCOMPARE(failedSpy.size(), 1);
}

// Before a session exists there is nothing to lose: a stray CONSOLE line
// during negotiation (a previous session's tail, a bench human) must not be
// mistaken for a failure of the handshake in progress.
void TestBtpHandshake::consoleLineBeforeEstablishedIsNotAFailure() {
    Harness h;
    h.handshake.start();  // AwaitingReady

    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::sessionFailed);
    h.handshake.feedRawBytes("BTP/1 CONSOLE\r\n");

    QCOMPARE(failedSpy.size(), 0);
    // And the real READY still lands the session.
    h.handshake.feedRawBytes("BTP/1 READY " + Harness::nonceOf(h.enterLine) + "\r\n");
    QCOMPARE(h.sent.size(), 1);
    QCOMPARE(h.sent.last().objectId, kControlHello);
}

}  // namespace

QTEST_MAIN(TestBtpHandshake)
#include "test_btphandshake.moc"
