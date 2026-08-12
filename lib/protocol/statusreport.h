#pragma once

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// One `topic_status` record of a STATUS payload with `status_version=2`
// (COMMANDS_AND_ACTIONS.md section 8.1) -- 28 fixed octets, no `record_size`
// of its own (the list is delimited by `topic_status_count`).
//
// A topic is identified by the (sourceId, topicId) *pair*: topic_id alone is
// not globally unique, and a gateway (the dongle) reports topics belonging to
// several robots in the same message.
struct StatusTopicRecord {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 subscriberCount = 0;
    quint32 effectiveRateMillihz = 0;  // zero: not being published right now
    quint64 bytesTotal = 0;            // logical TELEMETRY payload octets, monotonic since the emitter's boot
    quint64 samplesDroppedTotal = 0;   // monotonic since the emitter's boot
};

// A decoded STATUS payload (COMMANDS_AND_ACTIONS.md section 8, plus 8.1's
// per-topic extension). The 92-octet v1 block has the same layout at the same
// offsets in both versions, so a v1 reader and a v2 reader agree on every
// counter below; only `topics` is version-2-only.
struct StatusReport {
    quint16 statusVersion = 0;
    quint16 flags = 0;  // bit 0 DEGRADED
    quint64 uptimeUs = 0;
    quint64 framesRx = 0;
    quint64 framesTx = 0;
    quint64 framesDropped = 0;
    quint64 crcErrors = 0;
    quint64 decodeErrors = 0;
    quint64 reassemblyCompleted = 0;
    quint64 reassemblyTimeouts = 0;
    quint64 reassemblyRejected = 0;
    quint64 commandDuplicates = 0;
    quint64 telemetryDropped = 0;
    QVector<StatusTopicRecord> topics;  // always empty when statusVersion == 1

    bool degraded() const { return (flags & 0x0001) != 0; }
};

// Decodes a CONTROL/STATUS payload. Returns false (leaving `*out` untouched)
// for anything this client must not guess at: fewer than 92 octets, a
// `status_version` other than 1 or 2, or a v2 message whose
// `topic_status_count` does not fit the bytes that follow.
//
// With `status_version=1` the decoder MUST stop at 92 octets (section 8.1):
// trailing bytes are never reinterpreted as topic_status records, they are
// simply not read. That is what keeps a v1 emitter and this v2-aware reader
// compatible in both directions.
bool parseStatusPayload(const QByteArray& payload, StatusReport* out);

}  // namespace traceview
