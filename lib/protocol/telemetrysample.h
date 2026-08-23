#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace traceview {

// One TELEMETRY logical message, already envelope-validated and (if it
// arrived fragmented) reassembled by BtpSession/ProtocolRouter, with the
// 2-byte schema_version prefix (telemetry.md section 2) split out of the
// payload. `payload` is the encoded_body only -- still fully opaque bytes;
// this type carries schema_version and topic separately but does not itself
// know the encoding or decode anything (see TelemetryCatalog/decodePackedLe
// for that, PLANO_GERAL.txt decisions 5/6/8).
//
// source_id/boot_id/sequence/timestamp_us come from the origin and MUST NOT
// be rewritten by any hop in between (dongle or TraceView) -- see
// model.md sections 3-4. This struct exists specifically so nothing
// downstream is tempted to substitute e.g. local arrival time for
// timestampUs.
struct TelemetrySample {
    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    quint64 timestampUs = 0;
    quint16 topicId = 0;  // envelope object_id, see telemetry.md section 1
    quint16 schemaVersion = 0;
    QByteArray payload;  // encoded_body: opaque, never assumed to be UTF-8
};

}  // namespace traceview
