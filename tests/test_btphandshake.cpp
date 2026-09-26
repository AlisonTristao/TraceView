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
// BtpBackend's own HELLO_RESULT deadline (kHelloTimeoutMs, private there).
constexpr int kHelloTimeoutMs = 3000;

void appendLe(QByteArray& out, quint32 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

// ---------------------------------------------------------------------------
// Standalone BtpHandshake: all that is left of it is the CONSOLE watch -- see
// btphandshake.h's class comment. There is no ENTER/READY any more; HELLO is
// btp::Node::connect(), driven by BtpBackend.
// ---------------------------------------------------------------------------

class TestBtpHandshake : public QObject {
    Q_OBJECT

private slots:
    void consoleLineAfterEstablishedFiresConsoleLineDetected();
    void consoleLineBeforeEstablishedIsIgnored();

    // Full HELLO/HELLO_RESULT negotiation now lives on btp::Node, driven by
    // BtpBackend (m_node->connect()) -- these drive a real BtpBackend end to
    // end, exactly what an ordinary serial device does.
    void helloAdvertisesTheLibrarysFullSupportedVersionRange();
    void serialSendsHelloStraightAwayWithNoEnterLine();
    void directModeSendsHelloStraightAway();
    void helloIsResentWhenNoHelloResultArrives();
    void rejectedWriteIsReportedByTheBackend();
    void sessionEstablishedWhenSelectedVersionIsWithinTheAdvertisedRange();
    void sessionFailsWhenSelectedVersionIsOutsideTheAdvertisedRange();
};

// The dongle prints "BTP/1 CONSOLE\r\n" whenever it drops the session back to
// console (its inactivity watchdog, a SESSION_CLOSE, a bench human). The
// transport stays up and BtpSession has no watchdog, so without this the
// desktop would sit on a dead session forever. onSessionEstablished() is
// BtpBackend's own signal that HELLO succeeded (this class does not
// negotiate it, so it does not know on its own).
void TestBtpHandshake::consoleLineAfterEstablishedFiresConsoleLineDetected() {
    BtpHandshake handshake;
    handshake.onSessionEstablished();

    QSignalSpy consoleSpy(&handshake, &BtpHandshake::consoleLineDetected);
    // Arrives glued to the tail of a binary frame, exactly as it would on the
    // wire -- the scan is over raw bytes, not parsed frames.
    handshake.feedRawBytes(QByteArrayLiteral("\x00\x11\x22") + "BTP/1 CONSOLE\r\n");

    QCOMPARE(consoleSpy.size(), 1);
}

// Before a session exists there is nothing to lose: a stray CONSOLE line
// during negotiation (a previous session's tail, a bench human) must not be
// mistaken for a failure of the handshake in progress.
void TestBtpHandshake::consoleLineBeforeEstablishedIsIgnored() {
    BtpHandshake handshake;

    QSignalSpy consoleSpy(&handshake, &BtpHandshake::consoleLineDetected);
    handshake.feedRawBytes("BTP/1 CONSOLE\r\n");
    QCOMPARE(consoleSpy.size(), 0);
}

// ---------------------------------------------------------------------------
// Full HELLO negotiation, driven by a real BtpBackend (m_node->connect()).
// A second BtpSession, fed everything BtpBackend writes, decodes exactly as
// a real dongle's own receive path would -- the loopback the old Harness
// used, unchanged in spirit.
// ---------------------------------------------------------------------------

class BackendHarness {
public:
    explicit BackendHarness(BtpBackend::SessionStartMode mode =
                                BtpBackend::SessionStartMode::Console)
        : backend(btp::kSerialTransport, mode) {
        QObject::connect(&backend, &Backend::bytesToWrite, &loopback, &BtpSession::feedBytes);
        QObject::connect(&loopback, &BtpSession::bytesToWrite, &backend, &Backend::feedBytes);
        QObject::connect(&loopback, &BtpSession::frameReceived, &loopback,
                         [this](const BtpFrame& frame) { sent.append(frame); });
        QObject::connect(&backend, &Backend::bytesToWrite, &backend,
                         [this](const QByteArray& data) { written.append(data); });
    }

    // Brings the link up -- HELLO goes out at once -- and returns the HELLO
    // frame captured on the other end of the loopback.
    const BtpFrame& sendHelloAndCapture() {
        backend.onTransportConnectionChanged(true);
        helloFrame = sent.last();
        return helloFrame;
    }

    int helloCount() const {
        int count = 0;
        for (const BtpFrame& frame : sent) {
            if (frame.type == btp::MessageType::Control && frame.objectId == kControlHello) {
                ++count;
            }
        }
        return count;
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
    QVector<QByteArray> written;  // every raw write, text or frame
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

// No ENTER/READY text exchange on serial any more: the very first bytes on
// the wire are the HELLO frame, the same as TCP/BLE.
void TestBtpHandshake::serialSendsHelloStraightAwayWithNoEnterLine() {
    BackendHarness h;
    const BtpFrame& hello = h.sendHelloAndCapture();

    QCOMPARE(hello.type, btp::MessageType::Control);
    QCOMPARE(hello.objectId, kControlHello);
    QCOMPARE(h.helloCount(), 1);
    for (const QByteArray& data : h.written) {
        QVERIFY2(!data.contains("BTP/1"), data.constData());
    }
}

void TestBtpHandshake::directModeSendsHelloStraightAway() {
    BackendHarness h(BtpBackend::SessionStartMode::DirectBtp);
    const BtpFrame& hello = h.sendHelloAndCapture();

    QCOMPARE(hello.type, btp::MessageType::Control);
    QCOMPARE(hello.objectId, kControlHello);
}

// The first HELLO on a serial port is easily lost (opening the port can
// reset the ESP32), so a missing HELLO_RESULT is answered with another HELLO
// before the port is recycled.
void TestBtpHandshake::helloIsResentWhenNoHelloResultArrives() {
    BackendHarness h;
    QSignalSpy recoverySpy(&h.backend, &Backend::sessionRecoveryNeeded);
    h.sendHelloAndCapture();

    QTRY_VERIFY_WITH_TIMEOUT(h.helloCount() >= 2, kHelloTimeoutMs + 2000);
    QCOMPARE(recoverySpy.size(), 0);
}

void TestBtpHandshake::rejectedWriteIsReportedByTheBackend() {
    BtpBackend backend;
    QSignalSpy statusSpy(&backend, &Backend::statusMessage);
    QSignalSpy recoverySpy(&backend, &Backend::sessionRecoveryNeeded);

    backend.onTransportWriteRejected(QStringLiteral("queue full"));

    QCOMPARE(statusSpy.count(), 1);
    QVERIFY(statusSpy.at(0).at(0).toString().contains(QStringLiteral("queue full")));
    QCOMPARE(recoverySpy.count(), 1);
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
