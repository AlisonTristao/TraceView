#include <QtTest>
#include <btp/codec.hpp>
#include <btp/fragmentation.hpp>
#include <btp/stream.hpp>
#include <vector>

#include "protocol/btpsession.h"

using traceview::BtpFrame;
using traceview::BtpSession;

// topico 25: BtpSession's two axes -- link framing (COBS stream vs.
// pre-framed) and encode profile (the btp::TransportProfile ceiling OUTBOUND
// frames are encoded under) -- used to be one field, because for Serial and
// UsbHid they coincide. These tests pin the behavior that only exists once
// they are separate, and the two guarantees the relay path of topico 26 will
// depend on:
//
//  - a { PreFramed, EspNow } session encodes under the EspNow ceiling even
//    though no ESP-NOW radio is anywhere near it (the encode axis really is
//    independent of the framing axis) -- but it DECODES inbound frames under
//    the Serial (largest) ceiling, because a hub child's inbound path is the
//    parent's channel A, which carries the hub's own >250-octet cache
//    responses (plano 36);
//  - sendRawFrame() only wraps -- the octets on the wire are byte-for-byte
//    the octets handed in, never re-encoded, because the header's
//    source_id/boot_id/sequence are an AEAD nonce and the CRC covers a
//    sealed payload;
//  - frameBytesReceived() hands over the WHOLE frame (header + payload +
//    CRC) once per physical fragment, not once per reassembled logical
//    message.
//
// The pre-existing behavior of both real transports is covered by
// test_btpsession.cpp, which this refactor deliberately did not touch.
namespace {

btp::Header basicHeader(quint32 sourceId = 0x11223344, quint16 objectId = 0x0101) {
    btp::Header header{};
    header.type = btp::MessageType::Telemetry;
    header.flags = 0;
    header.source_id = sourceId;
    header.boot_id = 0xA1B2C3D4;
    header.sequence = 7;
    header.timestamp_us = 0x0102030405060708ULL;
    header.object_id = objectId;
    header.fragment_index = 0;
    header.fragment_count = 1;
    return header;
}

// The canonical encoder, as any BTP endpoint would call it -- used both to
// build inputs and to state the expected octets, so a test failure means
// BtpSession disagrees with the codec rather than with a hand-rolled copy.
QByteArray encodeFrame(const btp::Frame& frame, btp::TransportProfile profile) {
    std::vector<std::uint8_t> encoded(btp::max_frame_size(profile));
    std::size_t frameBytes = 0;
    const btp::Error error =
        btp::encode(frame, profile, encoded.data(), encoded.size(), &frameBytes);
    if (error != btp::Error::Ok) {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(encoded.data()), int(frameBytes));
}

QByteArray serialPacket(const btp::Frame& frame) {
    const QByteArray encoded = encodeFrame(frame, btp::TransportProfile::Serial);
    std::vector<std::uint8_t> cobs(btp::kSerialMaxCobsBlockSize);
    std::size_t cobsBytes = 0;
    const btp::CobsError cobsError =
        btp::cobs_encode(reinterpret_cast<const std::uint8_t*>(encoded.constData()),
                         std::size_t(encoded.size()), cobs.data(), cobs.size(), &cobsBytes);
    Q_ASSERT(cobsError == btp::CobsError::Ok);
    Q_UNUSED(cobsError);

    QByteArray packet;
    packet.append('\0');
    packet.append(reinterpret_cast<const char*>(cobs.data()), int(cobsBytes));
    packet.append('\0');
    return packet;
}

class TestBtpSessionFraming : public QObject {
    Q_OBJECT

private slots:
    void espNowProfileCeilingHoldsUnderPreFramedFraming();
    void sendRawFrameWrapsInCobsWithoutTouchingTheOctets();
    void sendRawFrameRejectsWhatCannotBeAFrame();
    void sendRawFrameOnPreFramedSessionEmitsVerbatim();
    void frameBytesReceivedCarriesTheWholeFrame();
    void frameBytesReceivedFiresOncePerFragmentNotPerMessage();
    void frameBytesReceivedOnPreFramedPathIsTheInputItself();
};

// The whole point of splitting the axes: this session is PreFramed (no COBS
// anywhere) yet encodes under the EspNow ceiling -- 210 octets of payload,
// 250 on the wire. Neither of the two combinations that existed before could
// express it, and dimensioning the encode buffer from the *branch* (as the
// old code did, kUsbHidMaxFrameSize in the HID branch and kSerialMaxFrameSize
// in the other) could not have produced a 250-octet frame at all.
void TestBtpSessionFraming::espNowProfileCeilingHoldsUnderPreFramedFraming() {
    BtpSession session(BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow);
    QCOMPARE(session.framing(), BtpSession::Framing::PreFramed);
    QCOMPARE(session.encodeProfile(), btp::TransportProfile::EspNow);

    QList<QByteArray> written;
    connect(&session, &BtpSession::bytesToWrite, &session,
            [&](const QByteArray& bytes) { written.append(bytes); });

    const QByteArray atCeiling(int(btp::kEspNowMaxPayloadSize), 'a');
    QCOMPARE(atCeiling.size(), 210);
    btp::Header header = basicHeader();
    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(atCeiling.constData()),
                            std::size_t(atCeiling.size())}};
    QVERIFY(session.sendFrame(frame));
    QCOMPARE(written.size(), 1);
    // PreFramed: exactly the encoded frame, no delimiters, no COBS -- and
    // 40 + 210 == the EspNow max frame size, not the Serial one.
    QCOMPARE(written.at(0).size(), int(btp::kEspNowMaxFrameSize));
    QCOMPARE(written.at(0), encodeFrame(frame, btp::TransportProfile::EspNow));

    // One octet past the EspNow payload ceiling: refused, nothing emitted.
    // (A Serial-profile session would have accepted this happily -- the same
    // payload is nowhere near kSerialMaxPayloadSize.)
    const QByteArray pastCeiling(int(btp::kEspNowMaxPayloadSize) + 1, 'a');
    const btp::Frame tooBig{header,
                            {reinterpret_cast<const std::uint8_t*>(pastCeiling.constData()),
                             std::size_t(pastCeiling.size())}};
    QVERIFY(!session.sendFrame(tooBig));
    QCOMPARE(written.size(), 1);  // still just the first one
}

// The test that stops someone from "following the house style" and
// re-encoding inside sendRawFrame(). The relay must not touch these octets:
// re-encoding would recompute the CRC over a re-serialized header, and the
// identity triple in that header is the AEAD nonce for a payload this end
// has no key for.
void TestBtpSessionFraming::sendRawFrameWrapsInCobsWithoutTouchingTheOctets() {
    // The parent side of a relay: a serial dongle, i.e. the framing/profile
    // pair that already existed.
    BtpSession parent;
    QCOMPARE(parent.framing(), BtpSession::Framing::CobsStream);

    QList<QByteArray> written;
    connect(&parent, &BtpSession::bytesToWrite, &parent,
            [&](const QByteArray& bytes) { written.append(bytes); });

    // Octets produced somewhere else entirely, under a *different* profile
    // than this session's (EspNow, as a HubChannel child would). A payload
    // full of zeros and delimiters is exactly what COBS has to survive.
    QByteArray payload(int(btp::kEspNowMaxPayloadSize), '\0');
    for (int i = 0; i < payload.size(); i += 3) {
        payload[i] = char(0xFF);
    }
    btp::Header header = basicHeader(0x0BADF00D);
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};
    const QByteArray childFrame = encodeFrame(frame, btp::TransportProfile::EspNow);
    QCOMPARE(childFrame.size(), int(btp::kEspNowMaxFrameSize));

    QVERIFY(parent.sendRawFrame(childFrame));
    QCOMPARE(written.size(), 1);
    const QByteArray packet = written.at(0);

    // 0x00 || COBS(bytes) || 0x00, and no delimiter anywhere inside.
    QVERIFY(packet.size() > 2);
    QCOMPARE(packet.at(0), char(0));
    QCOMPARE(packet.at(packet.size() - 1), char(0));
    const QByteArray block = packet.mid(1, packet.size() - 2);
    QCOMPARE(block.indexOf(char(0)), -1);

    // Decode the envelope back off and compare byte for byte: the frame that
    // went on the wire is the frame that was handed in, CRC included.
    std::vector<std::uint8_t> decoded(btp::kSerialMaxFrameSize);
    std::size_t decodedBytes = 0;
    QCOMPARE(
        btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(block.constData()),
                         std::size_t(block.size()), decoded.data(), decoded.size(), &decodedBytes),
        btp::CobsError::Ok);
    const QByteArray unwrapped(reinterpret_cast<const char*>(decoded.data()), int(decodedBytes));
    QCOMPARE(unwrapped.size(), childFrame.size());
    QCOMPARE(unwrapped, childFrame);

    // ...and the part a byte-for-byte comparison of a *well-formed* frame
    // cannot prove on its own: btp::encode() is deterministic, so an
    // implementation that decoded these octets and re-encoded them would
    // produce the same bytes and slip through the check above. So hand over
    // octets that no encoder would ever produce -- a frame whose payload was
    // altered after its CRC was computed -- and require them to come out
    // exactly as they went in. A re-encoding implementation would either
    // refuse them (decode fails) or "fix" the CRC; a relay must do neither,
    // because the frame it is carrying may be sealed with a key it does not
    // have and a CRC it cannot recompute meaningfully.
    QByteArray staleCrcFrame = childFrame;
    staleCrcFrame[int(btp::kV1HeaderSize)] = char(staleCrcFrame.at(int(btp::kV1HeaderSize)) ^ 0xFF);
    QVERIFY(parent.sendRawFrame(staleCrcFrame));
    QCOMPARE(written.size(), 2);
    const QByteArray secondBlock = written.at(1).mid(1, written.at(1).size() - 2);
    std::size_t secondBytes = 0;
    QCOMPARE(btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(secondBlock.constData()),
                              std::size_t(secondBlock.size()), decoded.data(), decoded.size(),
                              &secondBytes),
             btp::CobsError::Ok);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(decoded.data()), int(secondBytes)),
             staleCrcFrame);
    QVERIFY(staleCrcFrame != childFrame);  // the mutation really was there
}

void TestBtpSessionFraming::sendRawFrameRejectsWhatCannotBeAFrame() {
    BtpSession parent;
    int emissions = 0;
    connect(&parent, &BtpSession::bytesToWrite, &parent, [&](const QByteArray&) { ++emissions; });

    // Shorter than a header + CRC can possibly be.
    QVERIFY(!parent.sendRawFrame(QByteArray(int(btp::kV1MinimumFrameSize) - 1, 'x')));
    QVERIFY(!parent.sendRawFrame(QByteArray()));
    // Past this session's encode profile's frame ceiling.
    QVERIFY(!parent.sendRawFrame(QByteArray(int(btp::kSerialMaxFrameSize) + 1, 'x')));
    QCOMPARE(emissions, 0);

    // The smallest thing that *could* be a frame is accepted -- sendRawFrame
    // checks size only, and deliberately does not validate the frame it is
    // relaying (that is the business of the two endpoints).
    QVERIFY(parent.sendRawFrame(QByteArray(int(btp::kV1MinimumFrameSize), 'x')));
    QCOMPARE(emissions, 1);
}

// Documented degenerate case: PreFramed framing adds nothing, so
// sendRawFrame() emits the octets as they are. Still never re-encoded.
void TestBtpSessionFraming::sendRawFrameOnPreFramedSessionEmitsVerbatim() {
    BtpSession child(BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow);
    QList<QByteArray> written;
    connect(&child, &BtpSession::bytesToWrite, &child,
            [&](const QByteArray& bytes) { written.append(bytes); });

    const QByteArray payload("relayed");
    btp::Header header = basicHeader();
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};
    const QByteArray encoded = encodeFrame(frame, btp::TransportProfile::EspNow);

    QVERIFY(child.sendRawFrame(encoded));
    QCOMPARE(written.size(), 1);
    QCOMPARE(written.at(0), encoded);

    // The size check is against this session's own encode profile (EspNow),
    // not against Serial.
    QVERIFY(!child.sendRawFrame(QByteArray(int(btp::kEspNowMaxFrameSize) + 1, 'x')));
    QCOMPARE(written.size(), 1);
}

// The read side of the same guarantee: what comes out is the whole frame,
// header + payload + CRC, exactly what btp::decode() accepted -- so a child
// session can decode it itself without anybody re-serializing anything.
void TestBtpSessionFraming::frameBytesReceivedCarriesTheWholeFrame() {
    BtpSession session;
    QList<QPair<quint32, QByteArray>> raws;
    connect(&session, &BtpSession::frameBytesReceived, &session,
            [&](quint32 sourceId, QByteArray raw) {
                raws.append({sourceId, raw});
            });

    // Zeros and delimiters inside the payload again: proves the octets come
    // from the COBS-decoded frame, not from the wire packet.
    const QByteArray payload("\x00\x0A\x0D\xFF", 4);
    btp::Header header = basicHeader(0xDEADBEEF, 0x0202);
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};

    session.feedBytes(serialPacket(frame));

    QCOMPARE(raws.size(), 1);
    QCOMPARE(raws.at(0).first, quint32(0xDEADBEEF));
    const QByteArray expected = encodeFrame(frame, btp::TransportProfile::Serial);
    QCOMPARE(expected.size(), int(btp::kV1MinimumFrameSize) + payload.size());
    QCOMPARE(raws.at(0).second.size(), expected.size());
    QCOMPARE(raws.at(0).second, expected);  // header + payload + CRC, verbatim

    // A rejected candidate produces no raw octets at all: only frames that
    // decoded (envelope and CRC included) are relayable.
    session.feedBytes(QByteArray("\x00\x01\x02\x03\x00", 5));
    QCOMPARE(raws.size(), 1);
}

// Per PHYSICAL fragment, before this session's own reassembly. The relay
// reassembles nothing (the endpoint's own BtpSession has a Reassembler), so
// the parent must hand over each fragment as it arrives -- while
// frameReceived(), the local consumer's signal, still fires once for the
// reassembled logical message.
void TestBtpSessionFraming::frameBytesReceivedFiresOncePerFragmentNotPerMessage() {
    BtpSession session;
    QList<QByteArray> raws;
    int logicalFrames = 0;
    connect(&session, &BtpSession::frameBytesReceived, &session,
            [&](quint32, QByteArray raw) { raws.append(raw); });
    connect(&session, &BtpSession::frameReceived, &session,
            [&](const BtpFrame&) { ++logicalFrames; });

    QByteArray logicalPayload(int(btp::kSerialMaxPayloadSize) + 1, 'x');
    logicalPayload[0] = char(0x00);
    btp::Header header = basicHeader();
    const btp::ByteView logicalView{
        reinterpret_cast<const std::uint8_t*>(logicalPayload.constData()),
        std::size_t(logicalPayload.size())};

    std::uint8_t fragmentCount = 0;
    QCOMPARE(btp::fragment_count(logicalView.size, btp::TransportProfile::Serial, &fragmentCount),
             btp::Error::Ok);
    QCOMPARE(int(fragmentCount), 2);

    QList<QByteArray> expectedFragments;
    for (std::uint8_t i = 0; i < fragmentCount; ++i) {
        btp::Frame fragment{};
        QCOMPARE(
            btp::make_fragment(header, logicalView, btp::TransportProfile::Serial, i, &fragment),
            btp::Error::Ok);
        expectedFragments.append(encodeFrame(fragment, btp::TransportProfile::Serial));
        session.feedBytes(serialPacket(fragment));
    }

    QCOMPARE(raws.size(), 2);    // one per fragment...
    QCOMPARE(logicalFrames, 1);  // ...but one logical message
    QCOMPARE(raws.at(0), expectedFragments.at(0));
    QCOMPARE(raws.at(1), expectedFragments.at(1));
    // Each relayed fragment is a complete, independently valid frame with its
    // own header and CRC (fragmentation-and-transports.md section 1) -- which
    // is what lets the far endpoint reassemble instead of the relay.
    btp::DecodedFrame decoded;
    QCOMPARE(btp::decode(reinterpret_cast<const std::uint8_t*>(raws.at(1).constData()),
                         std::size_t(raws.at(1).size()), btp::TransportProfile::Serial, &decoded),
             btp::Error::Ok);
    QCOMPARE(int(decoded.header.fragment_index), 1);
    QVERIFY((decoded.header.flags & btp::kFlagFragmented) != 0);
}

void TestBtpSessionFraming::frameBytesReceivedOnPreFramedPathIsTheInputItself() {
    BtpSession session(BtpSession::Framing::PreFramed, btp::TransportProfile::EspNow);
    QList<QPair<quint32, QByteArray>> raws;
    connect(&session, &BtpSession::frameBytesReceived, &session,
            [&](quint32 sourceId, QByteArray raw) {
                raws.append({sourceId, raw});
            });

    const QByteArray payload(int(btp::kEspNowMaxPayloadSize), 'z');
    btp::Header header = basicHeader(0x0000BEEF);
    const btp::Frame frame{
        header,
        {reinterpret_cast<const std::uint8_t*>(payload.constData()), std::size_t(payload.size())}};
    const QByteArray encoded = encodeFrame(frame, btp::TransportProfile::EspNow);

    session.feedBytes(encoded);

    QCOMPARE(raws.size(), 1);
    QCOMPARE(raws.at(0).first, quint32(0x0000BEEF));
    QCOMPARE(raws.at(0).second, encoded);

    // The RECEIVE side is bounded by Serial (the largest ceiling), NOT by this
    // session's encode profile. A {PreFramed, EspNow} session is only ever a
    // hub child, and a hub child never reads a frame straight off a radio: its
    // inbound path is the parent's channel A (COBS/Serial), which also carries
    // the hub's OWN cache-served MANIFEST_DATA / SUBSCRIBE_RESULT -- and a
    // robot with a couple of topics is ~316 octets, well over the 250 EspNow
    // ceiling. Bounding receive by EspNow silently dropped exactly those and
    // left a hub child's catalog permanently empty. Encode stays EspNow-bound
    // (espNowProfileCeilingHoldsUnderPreFramedFraming) -- that IS a radio hop.
    int rejections = 0;
    connect(&session, &BtpSession::frameRejected, &session, [&](const QString&) { ++rejections; });
    const QByteArray serialSizedPayload(int(btp::kEspNowMaxPayloadSize) + 100, 'z');
    const btp::Frame serialSizedFrame{
        header,
        {reinterpret_cast<const std::uint8_t*>(serialSizedPayload.constData()),
         std::size_t(serialSizedPayload.size())}};
    const QByteArray overEspNow = encodeFrame(serialSizedFrame, btp::TransportProfile::Serial);
    QVERIFY(overEspNow.size() > int(btp::kEspNowMaxFrameSize));

    session.feedBytes(overEspNow);
    QCOMPARE(raws.size(), 2);                 // accepted, not dropped
    QCOMPARE(raws.at(1).second, overEspNow);
    QCOMPARE(rejections, 0);
}

}  // namespace

QTEST_MAIN(TestBtpSessionFraming)
#include "test_btpsessionframing.moc"
