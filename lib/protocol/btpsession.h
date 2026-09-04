#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <btp/codec.hpp>
#include <btp/fragmentation.hpp>
#include <btp/receiver.hpp>
#include <btp/stream.hpp>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "protocol/btpframe.h"

namespace traceview {

// Layers BTP v1 framing over the raw byte chunks a transport hands it --
// deliberately not coupled to SerialManager/UsbHidManager/QSerialPort (see
// CONTRIBUTING.md's transport/protocol/UI split): feedBytes() takes whatever
// arrived, however it arrived, and bytesToWrite() is what a transport should
// write back, whichever transport that ends up being.
//
// TWO INDEPENDENT AXES, both fixed at construction and never changed for the
// lifetime of the session (one DeviceConnection = one fixed transport, see
// devices/device.h's TransportType):
//
//   (a) Framing -- how a frame is delimited on the physical link.
//   (b) btp::TransportLimits -- which size ceiling btp::encode/btp::decode
//       apply to the frame itself (a frame-size ceiling + an allow_encrypted
//       policy bit; btp::kSerialTransport/kUsbHidTransport/kEspNowTransport
//       are the three named presets this codebase actually uses).
//
// These used to be a single field, because for the only two transports that
// existed they coincided (Serial framing implied the Serial ceiling, HID
// framing implied the HID ceiling). They are not the same question, and the
// third transport is the case that proves it:
//
//   Serial      -> { CobsStream, Serial }
//   UsbHid      -> { PreFramed,  UsbHid }
//   HubChannel  -> { PreFramed,  EspNow }   (topico 26, not built yet)
//
// A HubChannel is a child device that talks to a robot sitting behind an
// ESP-NOW radio, multiplexed over a parent dongle's serial connection. It
// encodes in the EspNow profile -- 250-octet frames, because that is what
// goes on the air -- and the parent only wraps those already-encoded octets
// in COBS to get them across the cable. The dongle unwraps and writes the
// same octets to the air, verbatim.
//
// That is what makes the relay cheap: THE DONGLE NEVER RE-FRAGMENTS. A
// relayed frame is already the right size at its source. Had the child
// encoded in the Serial profile (4056 octets of payload), the dongle would
// have to re-fragment 4056 -> 210 and produce 20 fragments with no
// retransmission behind them. Note this is deliberately NOT the gateway
// behavior of fragmentation-and-transports.md section 4 (reassemble, then
// re-fragment under the outgoing ceiling): encoding under the final profile
// from the origin removes the crossing entirely.
//
// The relayed octets must also cross the dongle INTACT: source_id/boot_id/
// sequence in the header are the AEAD nonce (encryption.md section 4), so
// re-encoding or recomputing the CRC anywhere in the middle would break the
// seal two repositories away from where the symptom shows up. sendRawFrame()
// and frameBytesReceived() below exist precisely so nothing on the relay
// path ever has to re-serialize a frame.
//
// The two framing modes concretely:
//
// - Framing::CobsStream (fragmentation-and-transports.md section 3.2:
//   0x00 || COBS(frame) || 0x00): feedBytes() may be called with arbitrary
//   byte-stream fragments, fed one octet at a time into btp::SerialDecoder
//   for incremental COBS decode + envelope/CRC validation. sendFrame()
//   COBS-encodes and wraps in 0x00 delimiters.
// - Framing::PreFramed: no COBS, no delimiters -- the layer below already
//   bounds exactly one frame per chunk, either because the report carries
//   its own length octet (UsbHidManager de-pads and de-prefixes each HID
//   report before emitting it, section 3.3) or because the datagram IS the
//   frame (ESP-NOW, section 3.1). Every feedBytes() call is assumed to be
//   exactly one already-bounded BTP frame candidate, decoded directly via
//   btp::decode(). sendFrame() encodes and hands the raw frame straight to
//   bytesToWrite(), with no wrapping at all.
//
// Both modes share the same btp::Reassembler for fragmented logical
// messages (a consumer MUST NOT see isolated fragments, TELEMETRY.md
// section 7 / BTP_V1.md section 5) -- fragmentation is defined per
// TransportLimits already, at the codec level, so nothing here needs to
// branch on framing for that part.
//
// Deliberately out of scope here: the ENTER/READY console handshake
// (Serial-only, session-and-terminal.md section 3) and HELLO negotiation
// (session-and-terminal.md sections 1-2, both transports). That belongs to
// session/subscription management (BtpHandshake and friends), which build
// on this framing layer rather than living inside it.
class BtpSession : public QObject {
    Q_OBJECT

public:
    // Axis (a): how a frame is delimited on the physical link. See the class
    // comment for what each mode implies about feedBytes()/sendFrame().
    enum class Framing { CobsStream, PreFramed };

    struct Diagnostics {
        quint64 framesDecoded = 0;    // single-frame or reassembled-complete
        quint64 crcErrors = 0;        // CrcMismatch (either mode)
        quint64 frameErrors = 0;      // any other decode error (either mode)
        quint64 cobsErrors = 0;       // SerialDecodeEvent::CobsError (CobsStream only)
        quint64 overflowDrops = 0;    // SerialDecodeEvent::Overflow (CobsStream only)
        quint64 reassemblyDrops = 0;  // Reassembler: InvalidFragment,
                                      // Conflict, MessageTooLarge, NoSlot
    };

    // The two axes, stated independently: `framing` is how frames are
    // delimited on the wire, `encodeProfile` is the limits btp::encode/
    // btp::decode apply their frame and payload ceilings under. See the
    // class comment for the three valid combinations.
    BtpSession(Framing framing, const btp::TransportLimits& encodeProfile,
              QObject* parent = nullptr);

    // Convenience constructor for the two combinations that existed before
    // the axes were split, where the profile alone determined both (see
    // framingFor()). Kept -- rather than updating the call sites -- because
    // the whole point of the split is that it changes no behavior: every
    // existing caller (BtpBackend and tests/test_btpsession.cpp, which
    // constructs a bare `BtpSession session;`) keeps compiling and keeps
    // meaning exactly what it meant, so the diff stays confined to the code
    // that actually needed the third combination. A call site whose framing
    // and profile do NOT coincide --
    // i.e. the HubChannel of topico 26 -- must use the two-axis constructor
    // above; no single profile maps to it.
    explicit BtpSession(const btp::TransportLimits& transport = btp::kSerialTransport,
                        QObject* parent = nullptr);

    // The framing the two pre-existing transports imply. Not a general
    // "limits -> framing" truth (EspNow is PreFramed as a radio datagram, but
    // a HubChannel encodes EspNow frames and ships them over a CobsStream
    // cable) -- only the legacy mapping the convenience constructor above
    // needs. Matches by max_frame_size: Serial (4096) is the only
    // CobsStream-framed transport this codebase has; every other size is
    // PreFramed.
    static Framing framingFor(const btp::TransportLimits& transport);

    Framing framing() const {
        return m_framing;
    }
    const btp::TransportLimits& encodeProfile() const {
        return m_encodeProfile;
    }

    const Diagnostics& diagnostics() const {
        return m_diagnostics;
    }

    // Encodes `frame` under this session's encode profile and, on success,
    // emits bytesToWrite() and returns true. In CobsStream framing that's
    // the full 0x00 || COBS(...) || 0x00 packet; in PreFramed framing it's
    // the raw encoded frame with no wrapping at all (UsbHidManager adds its
    // own report framing below this layer). Returns false without emitting
    // on any encode/COBS failure (oversized payload, invalid header, etc.)
    // -- the caller fragments a logical message too large for one frame
    // itself (see btp::fragment_count/make_fragment, under the same
    // encodeProfile()) before calling this per fragment.
    bool sendFrame(const btp::Frame& frame);

    // The write-side counterpart of frameBytesReceived(): takes octets that
    // are ALREADY a complete encoded BTP frame and applies only this
    // session's link framing to them. It does NOT re-encode and does NOT
    // recompute the CRC -- the octets reach the wire byte for byte as handed
    // in. That is a hard requirement, not an optimization: the header's
    // source_id/boot_id/sequence are the AEAD nonce and the CRC covers the
    // sealed payload, so a relay that "helpfully" re-encoded would
    // invalidate a tag it cannot even see (see the class comment).
    //
    // Returns false without emitting anything if the octets cannot be a
    // frame at all (shorter than btp::kV1MinimumFrameSize, or longer than
    // encodeProfile()'s frame ceiling), or if COBS encoding fails. It does
    // not otherwise validate the frame: whether those octets are well
    // formed is the business of the endpoint that encoded them and of the
    // endpoint that will decode them, not of the relay in between.
    //
    // In a PreFramed session this degenerates to "emit the octets
    // unchanged", since PreFramed framing adds nothing at all -- coherent
    // (and still never re-encoding), but not what the method is for. The
    // relaying parent is the end holding the cable, so it is a CobsStream
    // session (a serial dongle) that has a use for this; a PreFramed child
    // sending its own traffic calls sendFrame() instead.
    bool sendRawFrame(const QByteArray& alreadyEncoded);

public slots:
    // Feeds bytes off the wire. In CobsStream framing this is an arbitrary
    // byte-stream fragment, in arrival order -- may be called with
    // fragments split arbitrarily across calls (mirrors SerialManager::
    // dataReceived's contract), fed one octet at a time into the COBS
    // decoder. In PreFramed framing each call is assumed to be exactly one
    // already-bounded BTP frame candidate (UsbHidManager::dataReceived's own
    // contract: one already de-padded HID report per emission), decoded as a
    // whole. Emits frameBytesReceived() once per decoded physical frame and
    // frameReceived() zero or more times, plus frameRejected()/
    // diagnosticsChanged() on any decode/reassembly failure.
    void feedBytes(const QByteArray& data);

    // Resets the incremental COBS decoder to WaitingDelimiter, discarding
    // any partially-collected candidate, and discards all in-flight
    // reassemblies -- e.g. on a fresh connection, the same role
    // SerialLineAssembler::reset() played for the old line protocol.
    void reset();

signals:
    void frameReceived(const traceview::BtpFrame& frame);

    // The write-side mirror of frameReceived(): one BtpFrame per sendFrame()/
    // sendRawFrame() call that produced valid octets, emitted right after they
    // are handed to bytesToWrite(). Exists only for the BTP traffic monitor
    // (lib/diagnostics/framelog.h) -- nothing on the protocol path consumes it,
    // so with no monitor attached the signal has no receiver. sendRawFrame()'s
    // octets are a relayed child's already-encoded frame; they are decoded
    // best-effort only to fill this struct, and a decode failure still lets
    // the frame go out (it then carries an empty header and the raw bytes as
    // its payload).
    void frameSent(const traceview::BtpFrame& frame);

    // One decoded physical frame, as the RAW octets that came off the wire:
    // header + payload + CRC, exactly the input btp::decode() accepted, with
    // the link framing (COBS block, delimiters, report length prefix)
    // already stripped and nothing else touched.
    //
    // Raw octets rather than a parsed BtpFrame because the consumer is the
    // child transport of topico 26, which owns its OWN BtpSession and
    // decodes them itself. Handing it a parsed frame would force it to
    // re-serialize just to decode again -- and re-serializing is exactly
    // what must not happen on this path (AEAD nonce, see the class comment).
    //
    // `sourceId` is how the parent demultiplexes: each child claims the
    // frames whose header.source_id is its own target robot's. It sits in
    // the clear at a fixed header offset in every frame, encrypted payload
    // or not, which is what makes that routing rule work at all.
    //
    // PER FRAGMENT, and emitted BEFORE this session's own reassembly runs --
    // a fragmented logical message therefore produces one emission per
    // physical fragment, not one per message. The relay reassembles nothing;
    // the endpoint does, and the child's own BtpSession already has a
    // btp::Reassembler for it. Reassembling here would mean the parent
    // holding slots and applying its own timeout, and then handing over a
    // logical message the child would have to re-fragment to make sense of.
    void frameBytesReceived(quint32 sourceId, QByteArray raw);

    // Human-readable reason, for diagnostics/logging only -- no fallback
    // parsing is attempted (PLANO_GERAL.txt decision 1).
    void frameRejected(const QString& reason);
    void bytesToWrite(const QByteArray& data);
    void diagnosticsChanged();

private:
    static constexpr std::size_t kReassemblySlotCount = 2;
    static constexpr std::size_t kReassemblyStorageBytes =
        65536;  // covers
                // the largest logical payloads defined so far (manifest 49152,
                // command params/result 32768 -- commands.md section 6) with
                // headroom; desktop memory is not a tight constraint here.
    static constexpr std::uint64_t kReassemblyTimeoutMs =
        4000;  // matches
               // the dongle-side timeout used for the same purpose (topico 12's
               // RESULTADO, ProtocolRouter, which wraps the same btp::Receiver).

    void handleReassembly(const btp::DecodedFrame& fragment);
    // Applies axis (a) only -- COBS envelope in CobsStream framing, nothing
    // at all in PreFramed -- to octets that are already a complete encoded
    // frame, and emits bytesToWrite() on success. The single place the
    // framing decision is made on the write side, shared by sendFrame() and
    // sendRawFrame() so the two cannot drift apart.
    bool emitFramed(const std::uint8_t* frame, std::size_t frameSize);

    Framing m_framing;
    btp::TransportLimits m_encodeProfile;

    // Only used in CobsStream framing -- constructed unconditionally (cheap,
    // fixed-size buffers) but never fed a byte in PreFramed framing, where
    // feedBytes() takes the btp::decode() path directly instead.
    //
    // Sized by the Serial profile regardless of encodeProfile, and that is
    // not an oversight: btp::SerialDecoder::valid() requires exactly
    // kSerialMaxCobsBlockSize/kSerialMaxFrameSize capacities and decodes
    // internally under kSerialTransport, so these sizes are an API
    // requirement of that decoder rather than a choice this class makes. The
    // consequence is that a CobsStream session with a SMALLER encode profile
    // would not have that smaller frame ceiling enforced on its receive path
    // (an over-ceiling frame would still be caught by whoever decodes it
    // next). No such session exists today: the only framing/profile
    // mismatch in the plan is the HubChannel child, which is PreFramed and
    // never touches this decoder.
    std::vector<std::uint8_t> m_encodedBuffer;
    std::vector<std::uint8_t> m_decodedBuffer;
    btp::SerialDecoder m_decoder;

    std::vector<std::uint8_t> m_reassemblyStorageA;
    std::vector<std::uint8_t> m_reassemblyStorageB;
    btp::ReassemblySlot m_reassemblySlots[kReassemblySlotCount];
    btp::ReassemblyStorage m_reassemblyStorage[kReassemblySlotCount];
    // The decode + CRC + reassembly + timeout-sweep core (btp::Receiver, BTP
    // >= 2.8.0). feedBytes() still owns the COBS / pre-framed decode and the
    // frame-bytes-for-the-monitor emission; this handles only the reassembly
    // half, via the submit(DecodedFrame) overload.
    btp::Receiver m_receiver;
    // btp::Receiver copies a completed logical payload out and releases the
    // slot immediately; this is that copy's home, valid until the next submit.
    std::vector<std::uint8_t> m_reassembledOut;

    Diagnostics m_diagnostics;
};

}  // namespace traceview
