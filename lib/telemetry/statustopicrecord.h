#pragma once

#include <QtGlobal>

namespace traceview {

// Per-topic metrics for one (sourceId, topicId) a Backend is tracking.
// A topic is identified by the (sourceId, topicId) *pair*: topicId alone is
// not globally unique when a single connection carries topics belonging to
// several sources. Generic value type -- moved out of protocol/statusreport.h
// so dashboard/backend code can use it without depending on
// traceview_protocol (and, transitively, BTP).
struct StatusTopicRecord {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 subscriberCount = 0;
    quint32 effectiveRateMillihz = 0;  // zero: not being published right now
    quint64 bytesTotal = 0;            // logical payload octets, monotonic since the source's boot
    quint64 samplesDroppedTotal = 0;   // monotonic since the source's boot
};

}  // namespace traceview
