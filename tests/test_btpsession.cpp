#include <QtTest>
#include <btp/fragmentation.hpp>
#include <vector>

#include "protocol/btpsession.h"

using traceview::BtpFrame;
using traceview::BtpSession;

namespace {

// Encodes `frame` as BTP v1 for the serial transport profile and wraps it as
// 0x00 || COBS(frame) || 0x00 -- the exact stream format BtpSession consumes
// (fragmentation-and-transports.md section 3.2), built directly from the
// vendored BTP codec so this test exercises the real, canonical encoder
// rather than a hand-rolled one.
QByteArray buildSerialPacket(const btp::Frame& frame) {
    std::vector<std::uint8_t> encoded(btp::kSerialMaxFrameSize);
    std::size_t frameBytes = 0;
    const btp::Error encodeError = btp::encode(frame, btp::TransportProfile::Serial, encoded.data(),
                                               encoded.size(), &frameBytes);
    Q_ASSERT(encodeError == btp::Error::Ok);
    Q_UNUSED(encodeError);

    std::vector<std::uint8_t> cobs(btp::kSerialMaxCobsBlockSize);
    std::size_t cobsBytes = 0;
    const btp::CobsError cobsError =
        btp::cobs_encode(encoded.data(), frameBytes, cobs.data(), cobs.size(), &cobsBytes);
    Q_ASSERT(cobsError == btp::CobsError::Ok);
    Q_UNUSED(cobsError);

    QByteArray packet;
    packet.append('\0');
    packet.append(reinterpret_cast<const char*>(cobs.data()), int(cobsBytes));
    packet.append('\0');
    return packet;
}

btp::Header basicHeader(quint16 objectId = 0x0101) {
    btp::Header header{};
    header.type = btp::MessageType::Telemetry;
    header.flags = 0;
    header.source_id = 0x11223344;
    header.boot_id = 0xA1B2C3D4;
    header.sequence = 1;
    header.timestamp_us = 0x0102030405060708ULL;
    header.object_id = objectId;
    header.fragment_index = 0;
    header.fragment_count = 1;
    return header;
}

class TestBtpSession : public QObject {
    Q_OBJECT

private slots:
    void decodesSingleFrameWithEmbeddedZeroPayload();
    void decodesAcrossMultipleFeedCalls();
    void corruptedCrcIsRejectedAndCounted();
    void recoversAfterNoiseAndInvalidCandidate();
    void reassemblesFragmentedMessageAndFiresOnce();
    void resetDiscardsPartialCandidate();
};

void TestBtpSession::decodesSingleFrameWithEmbeddedZeroPayload() {
    BtpSession session;
    BtpFrame received;
    int frameCount = 0;
    connect(&session, &BtpSession::frameReceived, &session, [&](const BtpFrame& frame) {
        received = frame;
        ++frameCount;
    });

    // CRITERIO DE ACEITE: "payload binario com zero nao e truncado" -- the
    // BTP_V1.md section 10.2 example payload, containing 0x00, LF and CR.
    const QByteArray payload("\x00\x0A\x0D\xFF", 4);
    btp::Header header = basicHeader();
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};

    session.feedBytes(buildSerialPacket(frame));

    QCOMPARE(frameCount, 1);
    QCOMPARE(received.payload.size(), 4);
    QCOMPARE(received.payload, payload);
    QCOMPARE(received.sourceId, quint32(0x11223344));
    QCOMPARE(received.timestampUs, quint64(0x0102030405060708ULL));
    QCOMPARE(session.diagnostics().framesDecoded, quint64(1));
}

void TestBtpSession::decodesAcrossMultipleFeedCalls() {
    BtpSession session;
    int frameCount = 0;
    connect(&session, &BtpSession::frameReceived, &session, [&](const BtpFrame&) { ++frameCount; });

    const QByteArray payload("hello");
    btp::Header header = basicHeader();
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};
    const QByteArray packet = buildSerialPacket(frame);

    // Split arbitrarily, mirroring SerialManager::dataReceived's contract
    // that a logical unit may be split across multiple emissions.
    const int splitAt = packet.size() / 3;
    session.feedBytes(packet.left(splitAt));
    QCOMPARE(frameCount, 0);  // nothing decodable yet
    session.feedBytes(packet.mid(splitAt));

    QCOMPARE(frameCount, 1);
}

void TestBtpSession::corruptedCrcIsRejectedAndCounted() {
    BtpSession session;
    int frameCount = 0;
    QStringList rejections;
    connect(&session, &BtpSession::frameReceived, &session, [&](const BtpFrame&) { ++frameCount; });
    connect(&session, &BtpSession::frameRejected, &session,
            [&](const QString& reason) { rejections.append(reason); });

    const QByteArray payload("abcdef");
    btp::Header header = basicHeader();
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};

    std::vector<std::uint8_t> encoded(btp::kSerialMaxFrameSize);
    std::size_t frameBytes = 0;
    QVERIFY(btp::encode(frame, btp::TransportProfile::Serial, encoded.data(), encoded.size(),
                        &frameBytes) == btp::Error::Ok);
    // Flip a bit inside the payload (offset 36, right after the 36-byte
    // header) without touching the trailing CRC -- the frame is now
    // internally inconsistent, exactly what CRC exists to catch
    // (BTP_V1.md section 7).
    encoded[36] ^= 0xFF;

    std::vector<std::uint8_t> cobs(btp::kSerialMaxCobsBlockSize);
    std::size_t cobsBytes = 0;
    QVERIFY(btp::cobs_encode(encoded.data(), frameBytes, cobs.data(), cobs.size(), &cobsBytes) ==
            btp::CobsError::Ok);
    QByteArray packet;
    packet.append('\0');
    packet.append(reinterpret_cast<const char*>(cobs.data()), int(cobsBytes));
    packet.append('\0');

    session.feedBytes(packet);

    QCOMPARE(frameCount, 0);
    QCOMPARE(session.diagnostics().crcErrors, quint64(1));
    QCOMPARE(rejections.size(), 1);
}

void TestBtpSession::recoversAfterNoiseAndInvalidCandidate() {
    BtpSession session;
    int frameCount = 0;
    connect(&session, &BtpSession::frameReceived, &session, [&](const BtpFrame&) { ++frameCount; });

    // Noise with no leading delimiter at all: discarded in WaitingDelimiter
    // (STREAM_AND_REASSEMBLY.md section 4), no candidate is ever started, so
    // no event fires and nothing is decoded.
    session.feedBytes(QByteArray("garbage-not-a-frame"));
    QCOMPARE(frameCount, 0);
    QCOMPARE(session.diagnostics().cobsErrors, quint64(0));
    QCOMPARE(session.diagnostics().frameErrors, quint64(0));

    // A *bracketed* invalid candidate: leading 0x00 starts collection, three
    // arbitrary bytes are a structurally valid COBS block but decode to far
    // fewer than the 40 octets a BTP frame requires, so this must surface as
    // a FrameError (envelope-level rejection) without wedging the decoder.
    session.feedBytes(QByteArray("\x00\x01\x02\x03\x00", 5));
    QCOMPARE(frameCount, 0);
    QCOMPARE(session.diagnostics().frameErrors, quint64(1));

    // A real frame right after must still decode -- the delimiter that
    // ended the bad candidate resynchronizes the decoder (STREAM_AND_
    // REASSEMBLY.md).
    const QByteArray payload("recovered");
    btp::Header header = basicHeader();
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};
    session.feedBytes(buildSerialPacket(frame));

    QCOMPARE(frameCount, 1);
}

void TestBtpSession::reassemblesFragmentedMessageAndFiresOnce() {
    BtpSession session;
    BtpFrame received;
    int frameCount = 0;
    connect(&session, &BtpSession::frameReceived, &session, [&](const BtpFrame& frame) {
        received = frame;
        ++frameCount;
    });

    // One byte past kSerialMaxPayloadSize forces exactly 2 fragments for the
    // serial transport profile (BTP_V1.md section 8).
    QByteArray logicalPayload(int(btp::kSerialMaxPayloadSize) + 1, 'x');
    logicalPayload[0] = char(0x00);  // zero byte survives fragmentation too
    logicalPayload[10] = char(0x0A);
    logicalPayload[11] = char(0x0D);

    btp::Header header = basicHeader();
    const btp::ByteView logicalView{
        reinterpret_cast<const std::uint8_t*>(logicalPayload.constData()),
        std::size_t(logicalPayload.size())};

    std::uint8_t fragmentCount = 0;
    QVERIFY(btp::fragment_count(logicalView.size, btp::TransportProfile::Serial, &fragmentCount) ==
            btp::Error::Ok);
    QCOMPARE(int(fragmentCount), 2);

    for (std::uint8_t i = 0; i < fragmentCount; ++i) {
        btp::Frame fragment{};
        QVERIFY(btp::make_fragment(header, logicalView, btp::TransportProfile::Serial, i,
                                   &fragment) == btp::Error::Ok);
        session.feedBytes(buildSerialPacket(fragment));
    }

    QCOMPARE(frameCount, 1);  // reassembly delivers exactly one logical frame
    QCOMPARE(received.payload.size(), logicalPayload.size());
    QCOMPARE(received.payload, logicalPayload);
    QCOMPARE(session.diagnostics().framesDecoded, quint64(1));  // not 2 -- per
                                                                // physical fragment
}

void TestBtpSession::resetDiscardsPartialCandidate() {
    BtpSession session;
    int frameCount = 0;
    connect(&session, &BtpSession::frameReceived, &session, [&](const BtpFrame&) { ++frameCount; });

    const QByteArray payload("data");
    btp::Header header = basicHeader();
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};
    const QByteArray packet = buildSerialPacket(frame);

    session.feedBytes(packet.left(packet.size() / 2));
    session.reset();
    session.feedBytes(packet.mid(packet.size() / 2));

    // The first half was discarded by reset(); feeding only the back half of
    // a packet (no leading delimiter of its own) must not produce a frame.
    QCOMPARE(frameCount, 0);

    // A full, fresh packet after reset() must still decode normally.
    session.feedBytes(packet);
    QCOMPARE(frameCount, 1);
}

}  // namespace

QTEST_MAIN(TestBtpSession)
#include "test_btpsession.moc"
