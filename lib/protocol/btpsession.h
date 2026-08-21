#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <btp/codec.hpp>
#include <btp/fragmentation.hpp>
#include <btp/stream.hpp>

#include <cstdint>
#include <vector>

#include "protocol/btpframe.h"

namespace traceview {

// Layers a BTP v1 transport profile's framing over the raw byte chunks a
// transport hands it -- deliberately not coupled to SerialManager/
// UsbHidManager/QSerialPort (see CONTRIBUTING.md's transport/protocol/UI
// split): feedBytes() takes whatever arrived, however it arrived, and
// bytesToWrite() is what a transport should write back, whichever transport
// that ends up being.
//
// Two framing modes, selected once at construction by `transport` and never
// changed for the lifetime of the session (one DeviceConnection = one fixed
// transport, see devices/device.h's TransportType):
//
// - `btp::TransportProfile::Serial` (the original mode, TRANSPORT_SERIAL.md
//   section 2: 0x00 || COBS(frame) || 0x00): feedBytes() may be called with
//   arbitrary byte-stream fragments, fed one octet at a time into
//   btp::SerialDecoder for incremental COBS decode + envelope/CRC
//   validation. sendFrame() COBS-encodes and wraps in 0x00 delimiters.
// - `btp::TransportProfile::UsbHid` (TRANSPORT_USB_HID.md): no COBS, no
//   delimiters -- the transport (UsbHidManager) already de-pads and
//   de-prefixes each HID report before emitting it, so every feedBytes()
//   call is assumed to be exactly one already-bounded BTP frame candidate,
//   decoded directly via btp::decode(). sendFrame() encodes and hands the
//   raw frame straight to bytesToWrite(), with no wrapping at all.
//
// Both modes share the same btp::Reassembler for fragmented logical
// messages (a consumer MUST NOT see isolated fragments, TELEMETRY.md
// section 7 / BTP_V1.md section 5) -- fragmentation is defined per
// TransportProfile already, at the codec level, so nothing here needs to
// branch on transport for that part.
//
// Deliberately out of scope here: the ENTER/READY console handshake
// (Serial-only, TRANSPORT_SERIAL.md section 5) and HELLO negotiation
// (COMMANDS_AND_ACTIONS.md section 10, both transports). That belongs to
// session/subscription management (BtpHandshake and friends), which build
// on this framing layer rather than living inside it.
class BtpSession : public QObject {
    Q_OBJECT

public:
    struct Diagnostics {
        quint64 framesDecoded = 0;    // single-frame or reassembled-complete
        quint64 crcErrors = 0;        // CrcMismatch (either mode)
        quint64 frameErrors = 0;      // any other decode error (either mode)
        quint64 cobsErrors = 0;       // SerialDecodeEvent::CobsError (Serial only)
        quint64 overflowDrops = 0;    // SerialDecodeEvent::Overflow (Serial only)
        quint64 reassemblyDrops = 0;  // Reassembler: InvalidFragment,
                                       // Conflict, MessageTooLarge, NoSlot
    };

    // `transport` fixes which BTP transport profile this session speaks for
    // its whole lifetime (see the class comment) -- defaults to Serial,
    // preserving every existing call site's behavior.
    explicit BtpSession(btp::TransportProfile transport = btp::TransportProfile::Serial, QObject* parent = nullptr);

    const Diagnostics& diagnostics() const { return m_diagnostics; }

    // Encodes `frame` for this session's transport profile and, on success,
    // emits bytesToWrite() and returns true. In Serial mode that's the full
    // 0x00 || COBS(...) || 0x00 packet; in UsbHid mode it's the raw encoded
    // frame with no wrapping at all (UsbHidManager adds its own report
    // framing below this layer). Returns false without emitting on any
    // encode/COBS failure (oversized payload, invalid header, etc.) -- the
    // caller fragments a logical message too large for one frame itself
    // (see btp::fragment_count/make_fragment) before calling this per
    // fragment.
    bool sendFrame(const btp::Frame& frame);

public slots:
    // Feeds bytes off the wire. In Serial mode this is an arbitrary
    // byte-stream fragment, in arrival order -- may be called with
    // fragments split arbitrarily across calls (mirrors SerialManager::
    // dataReceived's contract), fed one octet at a time into the COBS
    // decoder. In UsbHid mode each call is assumed to be exactly one
    // already-bounded BTP frame candidate (UsbHidManager::dataReceived's own
    // contract: one already de-padded HID report per emission), decoded as
    // a whole. Emits frameReceived() zero or more times, and
    // frameRejected()/diagnosticsChanged() on any decode/reassembly
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

    btp::TransportProfile m_transport;

    // Only used in Serial mode -- constructed unconditionally (cheap, fixed-
    // size buffers) but never fed a byte in UsbHid mode, where feedBytes()
    // takes the btp::decode() path directly instead.
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
