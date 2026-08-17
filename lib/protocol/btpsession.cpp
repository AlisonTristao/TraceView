#include "protocol/btpsession.h"

#include <QDateTime>

#include <btp/codec.hpp>

namespace traceview {

BtpSession::BtpSession(QObject* parent)
    : QObject(parent),
      m_encodedBuffer(btp::kSerialMaxCobsBlockSize),
      m_decodedBuffer(btp::kSerialMaxFrameSize),
      m_decoder(m_encodedBuffer.data(), m_encodedBuffer.size(), m_decodedBuffer.data(), m_decodedBuffer.size()),
      m_reassemblyStorageA(kReassemblyStorageBytes),
      m_reassemblyStorageB(kReassemblyStorageBytes),
      m_reassemblyStorage{
          {m_reassemblyStorageA.data(), m_reassemblyStorageA.size()},
          {m_reassemblyStorageB.data(), m_reassemblyStorageB.size()},
      },
      m_reassembler(m_reassemblySlots, m_reassemblyStorage, kReassemblySlotCount, kReassemblyTimeoutMs) {}

bool BtpSession::sendFrame(const btp::Frame& frame) {
    std::vector<std::uint8_t> encoded(btp::kSerialMaxFrameSize);
    std::size_t frameBytes = 0;
    if (btp::encode(frame, btp::TransportProfile::Serial, encoded.data(), encoded.size(), &frameBytes) !=
        btp::Error::Ok) {
        return false;
    }

    std::vector<std::uint8_t> cobsBlock(btp::kSerialMaxCobsBlockSize);
    std::size_t cobsBytes = 0;
    if (btp::cobs_encode(encoded.data(), frameBytes, cobsBlock.data(), cobsBlock.size(), &cobsBytes) !=
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
    for (unsigned char byte : data) {
        btp::DecodedFrame decoded;
        const btp::SerialDecodeResult result = m_decoder.push(byte, &decoded);
        switch (result.event) {
            case btp::SerialDecodeEvent::None:
                break;
            case btp::SerialDecodeEvent::Frame:
                diagnosticsDirty = true;
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
                emit frameRejected(QStringLiteral("serial candidate exceeded the COBS block limit"));
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
