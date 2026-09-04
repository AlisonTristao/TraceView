#include <QSignalSpy>
#include <QtTest>
#include <btp/codec.hpp>
#include <vector>

#include "backend/backend.h"
#include "protocol/btpbackend.h"

using namespace traceview;

namespace {

constexpr quint16 kControlHelloResult = 0x0002;
constexpr quint16 kControlSessionClose = 0x000A;
constexpr quint16 kTerminalIn = 0x0001;

void appendLe32(QByteArray& out, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(char((value >> shift) & 0xFF));
    }
}

quint32 readLe32(const std::uint8_t* data) {
    return quint32(data[0]) | (quint32(data[1]) << 8) | (quint32(data[2]) << 16) |
           (quint32(data[3]) << 24);
}

QByteArray buildSerialPacket(const btp::Frame& frame) {
    std::vector<std::uint8_t> encoded(btp::kSerialMaxFrameSize);
    std::size_t encodedSize = 0;
    if (btp::encode(frame, btp::kSerialTransport, encoded.data(), encoded.size(),
                    &encodedSize) != btp::Error::Ok) {
        return {};
    }

    std::vector<std::uint8_t> cobs(btp::kSerialMaxCobsBlockSize);
    std::size_t cobsSize = 0;
    if (btp::cobs_encode(encoded.data(), encodedSize, cobs.data(), cobs.size(), &cobsSize) !=
        btp::CobsError::Ok) {
        return {};
    }

    QByteArray packet(1, char(0));
    packet.append(reinterpret_cast<const char*>(cobs.data()), int(cobsSize));
    packet.append(char(0));
    return packet;
}

bool decodeWritten(const QByteArray& written, btp::DecodedFrame* out,
                   std::vector<std::uint8_t>* storage) {
    if (written.size() < 3 || written.front() != char(0) || written.back() != char(0)) {
        return false;
    }

    const QByteArray block = written.mid(1, written.size() - 2);
    storage->assign(btp::kSerialMaxFrameSize, 0);
    std::size_t decodedSize = 0;
    if (btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(block.constData()),
                         std::size_t(block.size()), storage->data(), storage->size(),
                         &decodedSize) != btp::CobsError::Ok) {
        return false;
    }
    storage->resize(decodedSize);
    return btp::decode(storage->data(), storage->size(), btp::kSerialTransport, out) ==
           btp::Error::Ok;
}

// Runs the real ENTER/READY/HELLO_RESULT exchange far enough that
// BtpBackend regards the session as established. Other bootstrap requests
// (manifest and clock sync) may be emitted afterward; callers deliberately
// locate frames by object id rather than relying on an incidental index.
void establishSession(BtpBackend& backend, QSignalSpy& written) {
    backend.onTransportConnectionChanged(true);
    QVERIFY(written.count() >= 1);

    const QByteArray enter = written.at(0).at(0).toByteArray();
    QVERIFY(enter.startsWith(QByteArrayLiteral("BTP/1 ENTER ")));
    QByteArray nonce = enter.mid(QByteArrayLiteral("BTP/1 ENTER ").size());
    nonce.chop(2);  // CRLF
    backend.feedBytes(QByteArrayLiteral("BTP/1 READY ") + nonce + QByteArrayLiteral("\r\n"));

    QByteArray resultPayload;
    appendLe32(resultPayload, 0);  // request_source_id
    appendLe32(resultPayload, 0);  // request_boot_id
    appendLe32(resultPayload, 1);  // reply_to_sequence
    resultPayload.append(char(0)); // SUCCESS
    resultPayload.append(char(btp::kMinimumProtocolVersion));
    resultPayload.append(2, char(0));  // error_code NONE
    appendLe32(resultPayload, 2048);   // max_logical_payload
    resultPayload.append(char(1));
    resultPayload.append(char(0));     // max_inflight_reassemblies
    resultPayload.append(char(8));
    resultPayload.append(char(0));     // max_subscriptions
    appendLe32(resultPayload, 16);     // max_dedup_entries
    appendLe32(resultPayload, 30000);  // session_timeout_ms
    resultPayload.append(16, char(0x5A));  // peer_uuid, non-zero
    appendLe32(resultPayload, 1);          // config_revision

    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.source_id = 0xD00D0001;
    header.boot_id = 0xB0070001;
    header.sequence = 1;
    header.object_id = kControlHelloResult;
    header.fragment_count = 1;
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(resultPayload.constData()),
         std::size_t(resultPayload.size())}};
    backend.feedBytes(buildSerialPacket(frame));
}

}  // namespace

class TestSessionClose : public QObject {
    Q_OBJECT

private slots:
    void nothingIsSentWithoutAnEstablishedConsoleSession();
    void gracefulCloseHasCanonicalPayloadAndSharedSequence();
};

void TestSessionClose::nothingIsSentWithoutAnEstablishedConsoleSession() {
    BtpBackend backend;
    QSignalSpy written(&backend, &Backend::bytesToWrite);

    QVERIFY(!backend.requestSessionClose());
    QCOMPARE(written.count(), 0);

    BtpBackend child(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    child.setHubEndpoint(0x11111111, 0x22222222, QByteArray());
    QVERIFY(!child.requestSessionClose());
}

void TestSessionClose::gracefulCloseHasCanonicalPayloadAndSharedSequence() {
    BtpBackend backend;
    QSignalSpy written(&backend, &Backend::bytesToWrite);
    establishSession(backend, written);

    backend.sendTerminalIn(QByteArrayLiteral("x"));
    btp::DecodedFrame terminal{};
    std::vector<std::uint8_t> terminalStorage;
    QVERIFY(decodeWritten(written.last().at(0).toByteArray(), &terminal, &terminalStorage));
    QCOMPARE(int(terminal.header.type), int(btp::MessageType::Terminal));
    QCOMPARE(terminal.header.object_id, kTerminalIn);

    const int beforeClose = written.count();
    QVERIFY(backend.requestSessionClose());
    QCOMPARE(written.count(), beforeClose + 1);
    QVERIFY(!backend.requestSessionClose());
    QCOMPARE(written.count(), beforeClose + 1);  // exactly once

    btp::DecodedFrame close{};
    std::vector<std::uint8_t> closeStorage;
    QVERIFY(decodeWritten(written.last().at(0).toByteArray(), &close, &closeStorage));
    QCOMPARE(int(close.header.type), int(btp::MessageType::Control));
    QCOMPARE(close.header.object_id, kControlSessionClose);
    QCOMPARE(close.header.source_id, terminal.header.source_id);
    QCOMPARE(close.header.boot_id, terminal.header.boot_id);
    QCOMPARE(close.header.sequence, terminal.header.sequence + 1);
    QCOMPARE(close.payload.size, std::size_t(8));
    QCOMPARE(close.payload.data[0], std::uint8_t(0x02));  // CLIENT_SHUTDOWN
    QCOMPARE(close.payload.data[1], std::uint8_t(0));
    QCOMPARE(close.payload.data[2], std::uint8_t(0));
    QCOMPARE(close.payload.data[3], std::uint8_t(0));
    QCOMPARE(readLe32(close.payload.data + 4), quint32(500));
}

QTEST_MAIN(TestSessionClose)
#include "test_sessionclose.moc"
