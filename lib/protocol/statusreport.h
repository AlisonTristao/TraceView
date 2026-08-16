#pragma once

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

#include "telemetry/statustopicrecord.h"

namespace traceview {

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
