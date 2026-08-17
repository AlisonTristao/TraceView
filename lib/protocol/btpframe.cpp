#include "protocol/btpframe.h"

namespace traceview {

BtpFrame BtpFrame::fromHeaderAndPayload(const btp::Header& header, const btp::ByteView& payload) {
    BtpFrame frame;
    frame.type = header.type;
    frame.flags = header.flags;
    frame.sourceId = header.source_id;
    frame.bootId = header.boot_id;
    frame.sequence = header.sequence;
    frame.timestampUs = header.timestamp_us;
    frame.objectId = header.object_id;
    frame.fragmentIndex = header.fragment_index;
    frame.fragmentCount = header.fragment_count;
    if (payload.data != nullptr && payload.size > 0) {
        frame.payload = QByteArray(reinterpret_cast<const char*>(payload.data), int(payload.size));
    }
    return frame;
}

BtpFrame BtpFrame::fromDecoded(const btp::DecodedFrame& decoded) {
    return fromHeaderAndPayload(decoded.header, decoded.payload);
}

}  // namespace traceview
