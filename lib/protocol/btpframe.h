#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QtGlobal>

#include <btp/codec.hpp>

namespace traceview {

// Qt-friendly, self-contained copy of a decoded BTP frame. btp::DecodedFrame
// (bally_protocol/include/btp/codec.hpp) holds a ByteView pointing into a
// caller-owned buffer that btp::SerialDecoder reuses for the next candidate
// frame (or that a Reassembler slot reuses once released), so it is only
// valid for the instant it's produced. BtpSession copies it into this struct
// -- payload becomes an owned QByteArray -- before emitting frameReceived(),
// so subscribers can hold onto it across the event loop without racing the
// decoder.
struct BtpFrame {
    btp::MessageType type = btp::MessageType::Invalid;
    quint16 flags = 0;
    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    quint64 timestampUs = 0;
    quint16 objectId = 0;
    quint8 fragmentIndex = 0;
    quint8 fragmentCount = 1;
    QByteArray payload;  // opaque; never converted to/from text before a
                         // channel-specific decoder interprets it.

    static BtpFrame fromDecoded(const btp::DecodedFrame& decoded);
    static BtpFrame fromHeaderAndPayload(const btp::Header& header, const btp::ByteView& payload);
};

}  // namespace traceview

Q_DECLARE_METATYPE(traceview::BtpFrame)
