#include "protocol/btpsession.h"

#include <QDateTime>
#include <btp/codec.hpp>

namespace traceview {

BtpSession::Framing BtpSession::framingFor(btp::TransportProfile transport) {
    switch (transport) {
        case btp::TransportProfile::Serial:
            return Framing::CobsStream;
        case btp::TransportProfile::UsbHid:
        case btp::TransportProfile::EspNow:
            // A HID report carries its own length octet and an ESP-NOW
            // datagram is the frame -- either way the layer below already
            // delivers one bounded frame per chunk (fragmentation-and-
            // transports.md sections 3.1 and 3.3).
            return Framing::PreFramed;
    }
    return Framing::CobsStream;
}

BtpSession::BtpSession(Framing framing, btp::TransportProfile encodeProfile, QObject* parent)
    : QObject(parent),
      m_framing(framing),
      m_encodeProfile(encodeProfile),
      // Serial-sized on purpose, independently of encodeProfile -- see the
      // member declaration in the header: btp::SerialDecoder::valid()
      // requires these exact capacities.
      m_encodedBuffer(btp::kSerialMaxCobsBlockSize),
      m_decodedBuffer(btp::kSerialMaxFrameSize),
      m_decoder(m_encodedBuffer.data(), m_encodedBuffer.size(), m_decodedBuffer.data(),
                m_decodedBuffer.size()),
      m_reassemblyStorageA(kReassemblyStorageBytes),
      m_reassemblyStorageB(kReassemblyStorageBytes),
      m_reassemblyStorage{
          {m_reassemblyStorageA.data(), m_reassemblyStorageA.size()},
          {m_reassemblyStorageB.data(), m_reassemblyStorageB.size()},
      },
      m_reassembler(m_reassemblySlots, m_reassemblyStorage, kReassemblySlotCount,
                    kReassemblyTimeoutMs) {}

BtpSession::BtpSession(btp::TransportProfile transport, QObject* parent)
    : BtpSession(framingFor(transport), transport, parent) {}

bool BtpSession::emitFramed(const std::uint8_t* frame, std::size_t frameSize) {
    if (m_framing == Framing::PreFramed) {
        // No COBS, no delimiters -- the transport below adds whatever
        // bounding its link needs (UsbHidManager prepends Report ID +
        // length; an ESP-NOW datagram needs nothing at all).
        emit bytesToWrite(QByteArray(reinterpret_cast<const char*>(frame), int(frameSize)));
        return true;
    }

    // Sized from the frame actually in hand rather than from a per-profile
    // constant: any profile's frame ceiling is at most kSerialMaxFrameSize,
    // so the resulting block always fits what a peer's SerialDecoder
    // accepts, and a 250-octet EspNow frame does not pay for a 4113-octet
    // buffer.
    std::size_t cobsCapacity = 0;
    if (btp::cobs_max_encoded_size(frameSize, &cobsCapacity) != btp::CobsError::Ok) {
        return false;
    }
    std::vector<std::uint8_t> cobsBlock(cobsCapacity);
    std::size_t cobsBytes = 0;
    if (btp::cobs_encode(frame, frameSize, cobsBlock.data(), cobsBlock.size(), &cobsBytes) !=
        btp::CobsError::Ok) {
        return false;
    }

    QByteArray packet;
    packet.reserve(int(cobsBytes) + 2);
    packet.append('\0');
    packet.append(reinterpret_cast<const char*>(cobsBlock.data()), int(cobsBytes));
    packet.append('\0');
    emit bytesToWrite(packet);
    return true;
}

bool BtpSession::sendFrame(const btp::Frame& frame) {
    // Axis (b): the encode profile decides the ceiling, so it also decides
    // how big the encode buffer has to be. btp::encode() enforces that
    // ceiling itself and writes nothing when the payload exceeds it.
    std::vector<std::uint8_t> encoded(btp::max_frame_size(m_encodeProfile));
    std::size_t frameBytes = 0;
    if (btp::encode(frame, m_encodeProfile, encoded.data(), encoded.size(), &frameBytes) !=
        btp::Error::Ok) {
        return false;
    }
    // Axis (a) is applied entirely separately, and knows nothing about which
    // profile produced these octets.
    if (!emitFramed(encoded.data(), frameBytes)) {
        return false;
    }
    emit frameSent(BtpFrame::fromHeaderAndPayload(frame.header, frame.payload));
    return true;
}

bool BtpSession::sendRawFrame(const QByteArray& alreadyEncoded) {
    const std::size_t frameSize = static_cast<std::size_t>(alreadyEncoded.size());
    // The only two things checked, both about size and neither about
    // content: no re-encode, no CRC recomputation, no header parse (see the
    // header's contract -- these octets carry someone else's AEAD nonce and
    // CRC).
    if (frameSize < btp::kV1MinimumFrameSize || frameSize > btp::max_frame_size(m_encodeProfile)) {
        return false;
    }
    if (!emitFramed(reinterpret_cast<const std::uint8_t*>(alreadyEncoded.constData()), frameSize)) {
        return false;
    }
    // Decoded only to populate the monitor's struct -- these octets are a
    // relayed child's frame, already encoded under its own profile, and are
    // never re-encoded here (see this method's contract). Serial is the
    // largest ceiling, so it accepts an EspNow-sized child frame too. A
    // decode failure does not undo the send that already happened.
    btp::DecodedFrame decoded;
    if (btp::decode(reinterpret_cast<const std::uint8_t*>(alreadyEncoded.constData()), frameSize,
                    btp::TransportProfile::Serial, &decoded) == btp::Error::Ok) {
        emit frameSent(BtpFrame::fromDecoded(decoded));
    } else {
        BtpFrame raw;
        raw.payload = alreadyEncoded;
        emit frameSent(raw);
    }
    return true;
}

void BtpSession::handleReassembly(const btp::DecodedFrame& fragment) {
    if ((fragment.header.flags & btp::kFlagFragmented) == 0) {
        ++m_diagnostics.framesDecoded;
        emit frameReceived(BtpFrame::fromDecoded(fragment));
        return;
    }

    const auto nowMs = static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch());
    btp::ReassembledMessage completed;
    const btp::ReassemblyEvent event = m_reassembler.push(fragment, nowMs, &completed);
    switch (event) {
        case btp::ReassemblyEvent::Accepted:
        case btp::ReassemblyEvent::Duplicate:
            // Still waiting on more fragments (or a fragment we already
            // have, byte-for-byte) -- nothing to deliver yet.
            break;
        case btp::ReassemblyEvent::Complete:
            ++m_diagnostics.framesDecoded;
            emit frameReceived(BtpFrame::fromHeaderAndPayload(completed.header, completed.payload));
            m_reassembler.release(completed.slot_index);
            break;
        case btp::ReassemblyEvent::InvalidFragment:
        case btp::ReassemblyEvent::Conflict:
        case btp::ReassemblyEvent::MessageTooLarge:
        case btp::ReassemblyEvent::NoSlot:
            ++m_diagnostics.reassemblyDrops;
            emit frameRejected(QString::fromLatin1(btp::reassembly_event_string(event)));
            break;
        case btp::ReassemblyEvent::InvalidArgument:
            emit frameRejected(QStringLiteral("reassembler invalid argument"));
            break;
    }
}

void BtpSession::feedBytes(const QByteArray& data) {
    if (data.isEmpty()) {
        return;
    }

    // Cheap, timer-free way to bound stale in-flight reassemblies: piggyback
    // the expiry sweep on whatever cadence bytes actually arrive at, rather
    // than requiring this transport-agnostic class to own a QTimer.
    m_reassembler.expire(static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()));

    bool diagnosticsDirty = false;

    if (m_framing == Framing::PreFramed) {
        // No COBS to decode -- the transport below already handed us exactly
        // one bounded chunk, which is either a complete BTP frame or nothing
        // at all (see the class comment).
        //
        // Decoded against the Serial (largest) ceiling, NOT m_encodeProfile:
        // for a hub child m_encodeProfile is EspNow (the size limit of ITS
        // frames to the robot over the radio), but the frames it RECEIVES come
        // two ways -- a robot's telemetry relayed verbatim (EspNow-sized) and
        // the hub's OWN cache-served MANIFEST_DATA / SUBSCRIBE_RESULT, which
        // ride channel A and can legitimately exceed 250 octets (a robot with
        // a couple of topics is ~316). The transport below already bounded the
        // chunk; the decoder's job here is only "is this a valid BTP frame".
        btp::DecodedFrame decoded;
        const btp::Error error =
            btp::decode(reinterpret_cast<const std::uint8_t*>(data.constData()),
                        static_cast<std::size_t>(data.size()), btp::TransportProfile::Serial, &decoded);
        if (error == btp::Error::Ok) {
            diagnosticsDirty = true;
            // btp::decode() only accepts an input whose size is exactly
            // 40 + payload_size, so `data` IS the frame octets, with nothing
            // to trim -- forward it as is, without a copy or a re-encode.
            emit frameBytesReceived(decoded.header.source_id, data);
            handleReassembly(decoded);
        } else {
            if (error == btp::Error::CrcMismatch) {
                ++m_diagnostics.crcErrors;
            } else {
                ++m_diagnostics.frameErrors;
            }
            diagnosticsDirty = true;
            emit frameRejected(QString::fromLatin1(btp::error_string(error)));
        }
        if (diagnosticsDirty) {
            emit diagnosticsChanged();
        }
        return;
    }

    for (unsigned char byte : data) {
        btp::DecodedFrame decoded;
        const btp::SerialDecodeResult result = m_decoder.push(byte, &decoded);
        switch (result.event) {
            case btp::SerialDecodeEvent::None:
                break;
            case btp::SerialDecodeEvent::Frame:
                diagnosticsDirty = true;
                // btp::SerialDecoder hands back a DecodedFrame (header +
                // payload view + crc32), never the whole frame as one span.
                // The frame octets do exist though, contiguously: decode()
                // sets payload.data = input + kV1HeaderSize on the buffer it
                // was given, so the frame starts kV1HeaderSize octets before
                // the payload view and runs 40 + payload_size octets. That
                // is arithmetic over the codec's documented layout, NOT a
                // re-encode -- reconstructing these octets by encoding the
                // decoded header again would change the CRC's provenance and
                // is exactly what the AEAD relay path forbids.
                //
                // The copy into a QByteArray is required: the view points
                // into m_decodedBuffer, which the next completed candidate
                // overwrites (btp::SerialDecoder::push()'s validity
                // guarantee).
                emit frameBytesReceived(
                    decoded.header.source_id,
                    QByteArray(
                        reinterpret_cast<const char*>(decoded.payload.data - btp::kV1HeaderSize),
                        int(btp::kV1MinimumFrameSize + decoded.payload.size)));
                handleReassembly(decoded);
                break;
            case btp::SerialDecodeEvent::CobsError:
                ++m_diagnostics.cobsErrors;
                diagnosticsDirty = true;
                emit frameRejected(QStringLiteral("COBS decode error"));
                break;
            case btp::SerialDecodeEvent::FrameError:
                if (result.frame_error == btp::Error::CrcMismatch) {
                    ++m_diagnostics.crcErrors;
                } else {
                    ++m_diagnostics.frameErrors;
                }
                diagnosticsDirty = true;
                emit frameRejected(QString::fromLatin1(btp::error_string(result.frame_error)));
                break;
            case btp::SerialDecodeEvent::Overflow:
                ++m_diagnostics.overflowDrops;
                diagnosticsDirty = true;
                emit frameRejected(
                    QStringLiteral("serial candidate exceeded the COBS block limit"));
                break;
            case btp::SerialDecodeEvent::InvalidConfiguration:
                // Construction-time contract violation (buffers too small);
                // cannot happen given the fixed-size buffers above, but
                // don't silently ignore it either.
                emit frameRejected(QStringLiteral("invalid decoder configuration"));
                break;
        }
    }
    if (diagnosticsDirty) {
        emit diagnosticsChanged();
    }
}

void BtpSession::reset() {
    m_decoder.reset();
    m_reassembler.clear();
}

}  // namespace traceview
