#include "protocol/statusreport.h"

#include <btp/messages.hpp>

#include <cstdint>
#include <vector>

namespace traceview {

bool parseStatusPayload(const QByteArray& payload, StatusReport* out) {
    const auto* data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    const auto size = static_cast<std::size_t>(payload.size());

    // btp::messages owns the STATUS layout (commands.md section 5 / 5.1): the
    // 92-octet counter block, the status_version {1,2} check, and -- for v2 --
    // "the payload is exactly 92 + 2 + 28 * topic_status_count octets". A v1
    // payload must be exactly 92: trailing bytes are rejected, not ignored
    // (there is a `status_v1_trailing_byte` invalid conformance vector).
    std::uint16_t declared = 0;
    if (btp::status_topic_count(data, size, &declared) != btp::MessageError::Ok) {
        return false;
    }

    std::vector<btp::TopicStatusRecord> records(declared);
    btp::StatusV1 base{};
    std::size_t written = 0;
    if (btp::decode_status(data, size, &base, records.empty() ? nullptr : records.data(),
                           records.size(), &written) != btp::MessageError::Ok) {
        return false;
    }

    StatusReport report;
    report.statusVersion = base.status_version;
    report.flags = base.flags;
    report.uptimeUs = base.uptime_us;
    report.framesRx = base.frames_rx;
    report.framesTx = base.frames_tx;
    report.framesDropped = base.frames_dropped;
    report.crcErrors = base.crc_errors;
    report.decodeErrors = base.decode_errors;
    report.reassemblyCompleted = base.reassembly_completed;
    report.reassemblyTimeouts = base.reassembly_timeouts;
    report.reassemblyRejected = base.reassembly_rejected;
    report.commandDuplicates = base.command_duplicates;
    report.telemetryDropped = base.telemetry_dropped;

    report.topics.reserve(static_cast<int>(written));
    for (std::size_t i = 0; i < written; ++i) {
        StatusTopicRecord record;
        record.sourceId = records[i].source_id;
        record.topicId = records[i].topic_id;
        record.subscriberCount = records[i].subscriber_count;
        record.effectiveRateMillihz = records[i].effective_rate_millihz;
        record.bytesTotal = records[i].bytes_total;
        record.samplesDroppedTotal = records[i].samples_dropped_total;
        report.topics.append(record);
    }

    *out = report;
    return true;
}

}  // namespace traceview
