#pragma once

#include <QtGlobal>

namespace traceview {

// What a Backend currently believes about one subscribed topic. Exposed so
// the UI can show the *effective* rate the source granted instead of the
// rate a widget asked for. Generic value type -- moved out of
// protocol/subscriptionmanager.h so dashboard/backend code can use it
// without depending on traceview_protocol (and, transitively, BTP); a
// non-BTP Backend implementation returns these from its own
// subscriptions()/topicStatuses().
struct TopicSubscriptionState {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    int subscriberCount = 0;             // live widgets consuming this topic
    quint32 requestedRateMillihz = 0;    // the highest rate among those widgets
    quint32 effectiveRateMillihz = 0;    // 0 = not granted (yet)
    quint32 grantedLeaseMs = 0;
    quint32 subscriptionId = 0;          // 0 while no grant is held
    quint8 lastStatus = 0;               // last subscribe-result status code, Backend-defined
    quint16 lastErrorCode = 0;
    bool awaitingResult = false;

    // True once a grant came back below what was asked for -- the source
    // clamped the request to its own limit.
    bool rateLimited() const {
        return effectiveRateMillihz != 0 && requestedRateMillihz != 0 &&
               effectiveRateMillihz < requestedRateMillihz;
    }
};

}  // namespace traceview
