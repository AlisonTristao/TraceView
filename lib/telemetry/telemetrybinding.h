#pragma once

#include <QMetaType>
#include <QtGlobal>

namespace traceview {

// Identifies one plottable value within a telemetry source: which topic it
// belongs to and which field (and, for an array field, which element) within
// that topic. Generic addressing -- (sourceId, topicId, fieldId,
// elementIndex) are opaque integers a Backend implementation assigns
// meaning to; nothing here assumes BTP's wire format. Moved out of
// protocol/telemetryfieldrouter.h so dashboard/backend code can use this
// binding without depending on traceview_protocol (and, transitively, BTP).
struct TelemetryFieldBinding {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 fieldId = 0;
    quint16 elementIndex = 0;  // 0 for a scalar field or an array's first element

    bool operator==(const TelemetryFieldBinding& other) const {
        return sourceId == other.sourceId && topicId == other.topicId && fieldId == other.fieldId &&
               elementIndex == other.elementIndex;
    }
};

}  // namespace traceview

Q_DECLARE_METATYPE(traceview::TelemetryFieldBinding)
