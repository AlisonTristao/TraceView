#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <btp/fragmentation.hpp>
#include <btp/stream.hpp>

#include <cstdint>
#include <vector>

#include "protocol/btpframe.h"

namespace traceview {

// Layers BTP v1's serial framing (TRANSPORT_SERIAL.md section 2:
// 0x00 || COBS(frame) || 0x00) over the raw byte stream a transport hands
// it -- deliberately not coupled to SerialManager/QSerialPort (see
// CONTRIBUTING.md's transport/protocol/UI split): feedBytes() takes
// whatever bytes arrived, however they arrived, and bytesToWrite() is what
// a transport should write back, whichever transport that ends up being.
// Wraps bally_protocol's btp::SerialDecoder (STREAM_AND_REASSEMBLY.md) for
// incremental COBS decode + envelope/CRC validation, btp::Reassembler for
// fragmented logical messages (a consumer MUST NOT see isolated fragments,
// TELEMETRY.md section 7 / BTP_V1.md section 5), and btp::encode +
// btp::cobs_encode for the outbound direction.
//
// Deliberately out of scope here: the ENTER/READY console handshake and
// HELLO negotiation described in COMMANDS_AND_ACTIONS.md section 10 and
// TRANSPORT_SERIAL.md section 5. That belongs to session/subscription
// management (topicos 15-17), which need a real dongle running topico 13
// (in progress in parallel with this one) to validate against. What's here
// is the framing layer those topicos build on: once bytes matching
// 0x00 || COBS(frame) || 0x00 arrive, they are decoded, validated,
// reassembled if needed, and delivered as BtpFrame -- regardless of how the
// session got into a state where the peer is sending them.
class BtpSession : public QObject {
    Q_OBJECT

public:
    struct Diagnostics {
        quint64 framesDecoded = 0;    // single-frame or reassembled-complete
        quint64 crcErrors = 0;        // SerialDecodeEvent::FrameError,
                                       // frame_error == Error::CrcMismatch
        quint64 frameErrors = 0;      // FrameError, any other Error
        quint64 cobsErrors = 0;       // SerialDecodeEvent::CobsError
        quint64 overflowDrops = 0;    // SerialDecodeEvent::Overflow
        quint64 reassemblyDrops = 0;  // Reassembler: InvalidFragment,
                                       // Conflict, MessageTooLarge, NoSlot
    };

    explicit BtpSession(QObject* parent = nullptr);

    const Diagnostics& diagnostics() const { return m_diagnostics; }

    // Encodes `frame` as a BTP v1 frame for the serial transport profile
    // (kSerialMaxFrameSize/kSerialMaxPayloadSize) and, on success, emits
    // bytesToWrite() with the full 0x00 || COBS(...) || 0x00 packet and
    // returns true. Returns false without emitting on any encode/COBS
    // failure (oversized payload, invalid header, etc.) -- the caller
    // fragments a logical message too large for one frame itself (see
    // btp::fragment_count/make_fragment) before calling this per fragment.
    bool sendFrame(const btp::Frame& frame);

public slots:
    // Feeds raw bytes off the wire, in arrival order; may be called with
    // fragments split arbitrarily across calls (mirrors SerialManager::
    // dataReceived's contract). Emits frameReceived() zero or more times,
    // and frameRejected()/diagnosticsChanged() on any decode/reassembly
    // failure.
    void feedBytes(const QByteArray& data);

    // Resets the incremental COBS decoder to WaitingDelimiter, discarding
    // any partially-collected candidate, and discards all in-flight
    // reassemblies -- e.g. on a fresh connection, the same role
    // SerialLineAssembler::reset() played for the old line protocol.
    void reset();

signals:
    void frameReceived(const traceview::BtpFrame& frame);
    // Human-readable reason, for diagnostics/logging only -- no fallback
    // parsing is attempted (PLANO_GERAL.txt decision 1).
    void frameRejected(const QString& reason);
    void bytesToWrite(const QByteArray& data);
    void diagnosticsChanged();

private:
    static constexpr std::size_t kReassemblySlotCount = 2;
    static constexpr std::size_t kReassemblyStorageBytes = 65536;  // covers
        // the largest logical payloads defined so far (manifest 49152,
        // command params/result 32768 -- COMMANDS_AND_ACTIONS.md section
        // 13) with headroom; desktop memory is not a tight constraint here.
    static constexpr std::uint64_t kReassemblyTimeoutMs = 4000;  // matches
        // the dongle-side timeout used for the same purpose (topico 12's
        // RESULTADO, ProtocolRouter's Reassembler).

    void handleReassembly(const btp::DecodedFrame& fragment);

    std::vector<std::uint8_t> m_encodedBuffer;
    std::vector<std::uint8_t> m_decodedBuffer;
    btp::SerialDecoder m_decoder;

    std::vector<std::uint8_t> m_reassemblyStorageA;
    std::vector<std::uint8_t> m_reassemblyStorageB;
    btp::ReassemblySlot m_reassemblySlots[kReassemblySlotCount];
    btp::ReassemblyStorage m_reassemblyStorage[kReassemblySlotCount];
    btp::Reassembler m_reassembler;

    Diagnostics m_diagnostics;
};

}  // namespace traceview
