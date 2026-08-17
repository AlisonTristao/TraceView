#pragma once

#include <QByteArray>
#include <QHash>
#include <QVector>
#include <QtGlobal>

#include "protocol/telemetrycatalog.h"

namespace traceview {

// One field's decoded value(s) from a single PACKED_LE sample body
// (TELEMETRY.md section 4.5). `elements` already has scale/offset applied
// per TELEMETRY.md section 3 (`engineering_value = raw * scale + offset`)
// for numeric types; bool and enum8/enum16 store the raw integer value
// un-scaled (section 3: scale/offset MUST NOT be applied to bool and are
// optional for enum -- they never change label selection, which always uses
// the raw integer value, so this decoder doesn't apply them there either).
struct TelemetryFieldValue {
    bool isNull = false;
    QVector<double> elements;
};

// Decodes `body` (the encoded_body after schema_version has already been
// stripped -- see TelemetrySample/ProtocolRouter) against `schema` per
// TELEMETRY.md section 4.5, filling `outValues` with one entry per field of
// `schema.fields` (keyed by fieldId). Returns false, leaving `outValues`
// untouched, on any structural violation: wrong bitmap size, truncated or
// over-length field, over-length variable array, non-finite float (section
// 6.1), or leftover bytes after the last field (section 6.3) --
// TELEMETRY.md requires the whole sample be rejected in that case, never a
// partial decode.
//
// Only `TelemetryEncoding::PackedLe` is supported; callers should check
// `schema.encoding` themselves (TelemetryFieldRouter does) before calling
// this.
bool decodePackedLe(const TelemetryTopicSchema& schema, const QByteArray& body,
                     QHash<quint16, TelemetryFieldValue>* outValues);

}  // namespace traceview
