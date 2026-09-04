#include <QtTest>
#include <btp/codec.hpp>

#include "protocol/btpbackend.h"
#include "protocol/btpframe.h"
#include "protocol/btphandshake.h"
#include "protocol/btpsession.h"

using traceview::Backend;
using traceview::BtpBackend;
using traceview::BtpFrame;
using traceview::BtpHandshake;
using traceview::BtpSession;

namespace {

constexpr quint16 kControlHello = 0x0001;
constexpr quint16 kControlHelloResult = 0x0002;
constexpr int kHelloPayloadFixedSize = 40;  // offset where `versions` starts

void appendLe(QByteArray& out, quint32 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// The nonce out of an ENTER line: "BTP/1 ENTER <16 hex>\r\n".
QByteArray nonceOf(const QByteArray& line) {
    QByteArray nonce = line.mid(QByteArray("BTP/1 ENTER ").size());
    nonce.chop(2);  // trailing \r\n
    return nonce;
}

// ---------------------------------------------------------------------------
// Standalone BtpHandshake: the ENTER/READY link handshake and the CONSOLE
// watch it still owns (session-and-terminal.md section 3) -- see
// btphandshake.h's class comment for why HELLO/HELLO_RESULT moved out of
// this class entirely (btp::Node::connect(), driven by BtpBackend). Nothing
// here decodes a frame; readyForHello() is as far as this class goes on its
// own.
// ---------------------------------------------------------------------------

class HandshakeHarness {
public:
    explicit HandshakeHarness(int enterTimeoutMs = -1, int maxEnterAttempts = -1)
        : handshake(nullptr, enterTimeoutMs, maxEnterAttempts) {
        QObject::connect(&handshake, &BtpHandshake::bytesToWrite, &handshake,
                         [this](const QByteArray& data) {
                             enterLine = data;
                             enterLines.append(data);
                         });
        QObject::connect(&handshake, &BtpHandshake::readyForHello, &handshake,
                         [this] { ++readyCount; });
    }

    BtpHandshake handshake;
    QByteArray enterLine;       // the most recent one
    QVector<QByteArray> enterLines;  // every one, in order
    int readyCount = 0;
};

class TestBtpHandshake : public QObject {
    Q_OBJECT

private slots:
    void enterIsResentWithTheSameNonceWhenNoReadyArrives();
    void aReadyArrivingAfterAnEarlierTimeoutStillFiresReadyForHello();
    void silenceThroughTheWholeBudgetFailsWithEnterFailed();
    void consoleLineAfterEstablishedFiresConsoleLineDetected();
    void consoleLineBeforeEstablishedIsIgnored();

    // Full HELLO/HELLO_RESULT negotiation now lives on btp::Node, driven by
    // BtpBackend (m_node->connect()) -- these drive a real BtpBackend end to
    // end, exactly what an ordinary serial device does.
    void helloAdvertisesTheLibrarysFullSupportedVersionRange();
    void sessionEstablishedWhenSelectedVersionIsWithinTheAdvertisedRange();
    void sessionFailsWhenSelectedVersionIsOutsideTheAdvertisedRange();
};

// A lost ENTER is the common failure here -- the first line is written while
// the dongle is still deep in AppRuntime::begin() (SD, SQLite, ESP-NOW) and
// not yet reading the port, or into a device that just power-cycled.
// Resending is the whole recovery.
void TestBtpHandshake::enterIsResentWithTheSameNonceWhenNoReadyArrives() {
    HandshakeHarness h(/*enterTimeoutMs=*/150, /*maxEnterAttempts=*/5);
    h.handshake.start();
    QCOMPARE(h.enterLines.size(), 1);

    // ">= 2", never "== 2": the retry timer keeps firing, so under load the
    // count can step past 2 between two polls of the QTRY loop and an
    // equality check would then wait out its whole timeout and fail on a
    // retry budget that is working exactly as intended.
    QTRY_VERIFY_WITH_TIMEOUT(h.enterLines.size() >= 2, 4000);

    // The SAME nonce, not a fresh one: a READY answering the first ENTER can
    // still arrive late, and it has to keep matching. See m_enterNonce.
    QCOMPARE(nonceOf(h.enterLines.at(1)), nonceOf(h.enterLines.at(0)));
}

// The reason the nonce is stable across retries, stated as a test: a slow
// dongle answers the FIRST ENTER after that ENTER's own timeout already
// fired. With a fresh nonce per attempt that reply would match nothing and
// the handshake would stall until the budget ran out, even though the peer
// did reply.
void TestBtpHandshake::aReadyArrivingAfterAnEarlierTimeoutStillFiresReadyForHello() {
    HandshakeHarness h(/*enterTimeoutMs=*/150, /*maxEnterAttempts=*/5);
    h.handshake.start();
    const QByteArray firstNonce = nonceOf(h.enterLines.at(0));
    QTRY_VERIFY_WITH_TIMEOUT(h.enterLines.size() >= 2, 4000);

    // Answering the FIRST attempt, long after its timer expired.
    h.handshake.feedRawBytes("BTP/1 READY " + firstNonce + "\r\n");

    QCOMPARE(h.readyCount, 1);
}

// Exhausting the budget is a real failure and has to say so exactly once --
// BtpBackend turns enterFailed into the recovery that recycles the port
// (Backend::sessionRecoveryNeeded), and a second emission would recycle a
// connection that is already being reopened.
void TestBtpHandshake::silenceThroughTheWholeBudgetFailsWithEnterFailed() {
    HandshakeHarness h(/*enterTimeoutMs=*/100, /*maxEnterAttempts=*/3);
    QSignalSpy failedSpy(&h.handshake, &BtpHandshake::enterFailed);

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
// desktop would sit on a dead session forever. onSessionEstablished() is
// BtpBackend's own signal that HELLO succeeded (this class no longer
// negotiates it, so it no longer knows on its own).
void TestBtpHandshake::consoleLineAfterEstablishedFiresConsoleLineDetected() {
    HandshakeHarness h;
    h.handshake.onSessionEstablished();

    QSignalSpy consoleSpy(&h.handshake, &BtpHandshake::consoleLineDetected);
    // Arrives glued to the tail of a binary frame, exactly as it would on the
    // wire -- the scan is over raw bytes, not parsed frames.
    h.handshake.feedRawBytes(QByteArrayLiteral("\x00\x11\x22") + "BTP/1 CONSOLE\r\n");

    QCOMPARE(consoleSpy.size(), 1);
}

// Before a session exists there is nothing to lose: a stray CONSOLE line
// during negotiation (a previous session's tail, a bench human) must not be
// mistaken for a failure of the handshake in progress.
void TestBtpHandshake::consoleLineBeforeEstablishedIsIgnored() {
    HandshakeHarness h;
    h.handshake.start();  // AwaitingReady

    QSignalSpy consoleSpy(&h.handshake, &BtpHandshake::consoleLineDetected);
    h.handshake.feedRawBytes("BTP/1 CONSOLE\r\n");
    QCOMPARE(consoleSpy.size(), 0);

    // And the real READY still lands.
    h.handshake.feedRawBytes("BTP/1 READY " + nonceOf(h.enterLine) + "\r\n");
    QCOMPARE(h.readyCount, 1);
}

// ---------------------------------------------------------------------------
// Full HELLO negotiation, driven by a real BtpBackend (m_node->connect()).
// A second BtpSession, fed everything BtpBackend writes, decodes exactly as
// a real dongle's own receive path would -- the loopback the old Harness
// used, unchanged in spirit.
// ---------------------------------------------------------------------------

class BackendHarness {
public:
    BackendHarness() {
        QObject::connect(&backend, &Backend::bytesToWrite, &loopback, &BtpSession::feedBytes);
        QObject::connect(&loopback, &BtpSession::bytesToWrite, &backend, &Backend::feedBytes);
        QObject::connect(&loopback, &BtpSession::frameReceived, &loopback,
                         [this](const BtpFrame& frame) { sent.append(frame); });
        QObject::connect(&backend, &Backend::bytesToWrite, &backend,
                         [this](const QByteArray& data) {
                             if (data.startsWith("BTP/1 ENTER ")) {
                                 enterLine = data;
                             }
                         });
    }

    // Runs the ENTER/READY handshake far enough that HELLO fires, and
    // returns the HELLO frame captured on the other end of the loopback.
    const BtpFrame& sendHelloAndCapture() {
        backend.onTransportConnectionChanged(true);
        backend.feedBytes("BTP/1 READY " + nonceOf(enterLine) + "\r\n");
        helloFrame = sent.last();
        return helloFrame;
    }

    // Delivers a HELLO_RESULT exactly as a real dongle would build one: the
    // request reference echoes back the HELLO's own (source_id, boot_id,
    // sequence) -- btp::SessionInitiator::on_frame() correlates on that
    // triple (session-and-terminal.md section 2), not on anything this test
    // gets to invent. sendHelloAndCapture() must run first.
    void deliverHelloResult(quint8 status, quint8 selectedVersion) {
        // The full 52-octet HELLO_RESULT layout (session-and-terminal.md
        // section 2): request reference, status, selected_version,
        // error_code, effective limits, peer_uuid, config_revision.
        QByteArray payload;
        appendLe(payload, helloFrame.sourceId, 4);
        appendLe(payload, helloFrame.bootId, 4);
        appendLe(payload, helloFrame.sequence, 4);
        payload.append(static_cast<char>(status));
        payload.append(static_cast<char>(selectedVersion));
        appendLe(payload, 0, 2);   // error_code
        appendLe(payload, 0, 4);   // max_logical_payload
        appendLe(payload, 0, 2);   // max_inflight_reassemblies
        appendLe(payload, 0, 2);   // max_subscriptions
        appendLe(payload, 0, 4);   // max_dedup_entries
        appendLe(payload, 0, 4);   // session_timeout_ms
        payload.append(16, '\0');  // peer_uuid
        appendLe(payload, 0, 4);   // config_revision

        btp::Header header{};
        header.type = btp::MessageType::Control;
        header.source_id = 0xDEADBEEF;  // the "dongle"'s own identity -- unrelated to correlation
        header.boot_id = 0x12345678;
        header.sequence = 1;
        header.object_id = kControlHelloResult;
        header.fragment_index = 0;
        header.fragment_count = 1;
        const btp::Frame frame{
            header, {reinterpret_cast<const std::uint8_t*>(payload.constData()),
                     std::size_t(payload.size())}};
        loopback.sendFrame(frame);
    }

    BtpBackend backend;    // default: Serial / COBS, no hub endpoint
    BtpSession loopback;   // the "dongle" side of the cable
    QVector<BtpFrame> sent;
    QByteArray enterLine;
    BtpFrame helloFrame;
};

void TestBtpHandshake::helloAdvertisesTheLibrarysFullSupportedVersionRange() {
    BackendHarness h;
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
    BackendHarness h;
    h.sendHelloAndCapture();
    QSignalSpy identifiedSpy(&h.backend, &Backend::deviceIdentified);
    QSignalSpy recoverySpy(&h.backend, &Backend::sessionRecoveryNeeded);

    h.deliverHelloResult(/*status=*/0x00, btp::kMaximumProtocolVersion);

    QCOMPARE(identifiedSpy.size(), 1);
    QCOMPARE(recoverySpy.size(), 0);
}

void TestBtpHandshake::sessionFailsWhenSelectedVersionIsOutsideTheAdvertisedRange() {
    BackendHarness h;
    h.sendHelloAndCapture();
    QSignalSpy identifiedSpy(&h.backend, &Backend::deviceIdentified);
    QSignalSpy recoverySpy(&h.backend, &Backend::sessionRecoveryNeeded);

    // A peer claiming a version we never offered is a peer not honoring the
    // negotiation -- not a version this client can silently go along with.
    h.deliverHelloResult(/*status=*/0x00, quint8(btp::kMaximumProtocolVersion + 1));

    QCOMPARE(identifiedSpy.size(), 0);
    QCOMPARE(recoverySpy.size(), 1);
}

}  // namespace

QTEST_MAIN(TestBtpHandshake)
#include "test_btphandshake.moc"
