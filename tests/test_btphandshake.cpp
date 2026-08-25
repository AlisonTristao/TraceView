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
    Harness() {
        QObject::connect(&outbound, &BtpSession::bytesToWrite, &loopback, &BtpSession::feedBytes);
        QObject::connect(&loopback, &BtpSession::frameReceived, &loopback,
                         [this](const BtpFrame& frame) { sent.append(frame); });
        QObject::connect(&handshake, &BtpHandshake::bytesToWrite, &handshake,
                         [this](const QByteArray& data) { enterLine = data; });
    }

    // Runs the ENTER/READY handshake far enough that sendHello() fires, and
    // returns the HELLO frame captured on the other end of the loopback.
    const BtpFrame& sendHelloAndCapture() {
        handshake.start();
        // enterLine == "BTP/1 ENTER <16-hex-char nonce>\r\n"
        QByteArray nonce = enterLine.mid(QByteArray("BTP/1 ENTER ").size());
        nonce.chop(2);  // trailing \r\n
        handshake.feedRawBytes("BTP/1 READY " + nonce + "\r\n");
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
    QByteArray enterLine;
};

class TestBtpHandshake : public QObject {
    Q_OBJECT

private slots:
    void helloAdvertisesTheLibrarysFullSupportedVersionRange();
    void sessionEstablishedWhenSelectedVersionIsWithinTheAdvertisedRange();
    void sessionFailsWhenSelectedVersionIsOutsideTheAdvertisedRange();
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

}  // namespace

QTEST_MAIN(TestBtpHandshake)
#include "test_btphandshake.moc"
