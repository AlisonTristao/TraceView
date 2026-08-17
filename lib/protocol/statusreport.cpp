#include "protocol/statusreport.h"

namespace traceview {

namespace {

constexpr int kStatusV1Size = 92;      // section 8: exact logical size of a v1 payload
constexpr int kTopicCountOffset = 92;  // section 8.1: right after the v1 block
// section 8.1's per-record offset table: source_id@0, topic_id@4,
// subscriber_count@6, effective_rate_millihz@8, bytes_total@12,
// samples_dropped_total@20 -- 4+2+2+4+8+8 = 28. (An earlier revision of that
// section said 24, an arithmetic slip the offsets table now corrects; the
// emitters on the other end -- t_dongle_develop's
// SerialSession::kTopicStatusRecordSize and bally_software's StatusReporter --
// serialize 28 as well.) Single point of definition for the stride.
constexpr int kTopicRecordSize = 28;

quint16 readLe16(const QByteArray& data, int offset) {
    return quint16(quint8(data.at(offset))) | (quint16(quint8(data.at(offset + 1))) << 8);
}

quint32 readLe32(const QByteArray& data, int offset) {
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= quint32(quint8(data.at(offset + i))) << (8 * i);
    }
    return value;
}

quint64 readLe64(const QByteArray& data, int offset) {
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= quint64(quint8(data.at(offset + i))) << (8 * i);
    }
    return value;
}

}  // namespace

bool parseStatusPayload(const QByteArray& payload, StatusReport* out) {
    if (payload.size() < kStatusV1Size) {
        return false;
    }

    StatusReport report;
    report.statusVersion = readLe16(payload, 0);
    if (report.statusVersion != 1 && report.statusVersion != 2) {
        return false;  // unknown version: never guessed at, section 8/8.1 only define 1 and 2
    }
    report.flags = readLe16(payload, 2);
    report.uptimeUs = readLe64(payload, 4);
    report.framesRx = readLe64(payload, 12);
    report.framesTx = readLe64(payload, 20);
    report.framesDropped = readLe64(payload, 28);
    report.crcErrors = readLe64(payload, 36);
    report.decodeErrors = readLe64(payload, 44);
    report.reassemblyCompleted = readLe64(payload, 52);
    report.reassemblyTimeouts = readLe64(payload, 60);
    report.reassemblyRejected = readLe64(payload, 68);
    report.commandDuplicates = readLe64(payload, 76);
    report.telemetryDropped = readLe64(payload, 84);

    if (report.statusVersion == 2) {
        if (payload.size() < kTopicCountOffset + 2) {
            return false;
        }
        const int count = int(readLe16(payload, kTopicCountOffset));
        const int recordsStart = kTopicCountOffset + 2;
        if (payload.size() < recordsStart + count * kTopicRecordSize) {
            return false;  // truncated list: a partial record is never half-read
        }
        report.topics.reserve(count);
        for (int i = 0; i < count; ++i) {
            const int base = recordsStart + i * kTopicRecordSize;
            StatusTopicRecord record;
            record.sourceId = readLe32(payload, base);
            record.topicId = readLe16(payload, base + 4);
            record.subscriberCount = readLe16(payload, base + 6);
            record.effectiveRateMillihz = readLe32(payload, base + 8);
            record.bytesTotal = readLe64(payload, base + 12);
            record.samplesDroppedTotal = readLe64(payload, base + 20);
            report.topics.append(record);
        }
    }
    // status_version == 1: stop at 92 octets. Trailing bytes (if any) are
    // deliberately not read -- section 8.1 requires exactly that of a reader
    // that also understands v2.

    *out = report;
    return true;
}

}  // namespace traceview
