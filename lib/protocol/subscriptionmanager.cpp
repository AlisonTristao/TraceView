#include "protocol/subscriptionmanager.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"
#include "protocol/telemetrycatalog.h"

namespace traceview {

namespace {

constexpr quint16 kControlSubscribe = 0x0005;
constexpr quint16 kControlSubscribeResult = 0x0006;
constexpr quint16 kControlUnsubscribe = 0x0007;
constexpr quint16 kControlUnsubscribeResult = 0x0008;
constexpr quint16 kControlStatus = 0x0009;
constexpr quint8 kStatusSuccess = 0x00;

constexpr int kSubscribeResultSize = 28;    // 12 ref + 1 + 1 + 2 + 4 + 4 + 4
constexpr int kUnsubscribeResultSize = 16;  // 12 ref + 1 + 1 + 2

// How early a lease is renewed. Half the granted lease leaves a full lease
// period of margin for one lost SUBSCRIBE/SUBSCRIBE_RESULT round trip before
// the source would drop the subscription (commands.md section 4).
constexpr quint32 kMinRenewIntervalMs = 500;
constexpr int kLeaseTimerIntervalMs = 1000;

void appendLe(QByteArray& out, quint32 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

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

qint64 renewIntervalFor(quint32 grantedLeaseMs) {
    return qint64(qMax(grantedLeaseMs / 2, kMinRenewIntervalMs));
}

}  // namespace

SubscriptionManager::SubscriptionManager(BtpSession* session, ProtocolRouter* router, TelemetryCatalog* catalog,
                                         QObject* parent)
    : QObject(parent), m_session(session), m_catalog(catalog) {
    connect(router, &ProtocolRouter::controlFrameReceived, this, &SubscriptionManager::onControlFrameReceived);

    // Private per-process wire identity, the same construction ManifestClient
    // and SerialWidgetBridge already use: SUBSCRIBE_RESULT correlates only by
    // the (source_id, boot_id, sequence) triple echoed back from the request
    // envelope (commands.md section 1), so this does not have to
    // be the identity BtpHandshake used for HELLO.
    m_clientSourceId = QRandomGenerator::global()->generate() | 1u;
    m_clientBootId = QRandomGenerator::global()->generate() | 1u;

    m_leaseTimer.setInterval(kLeaseTimerIntervalMs);
    connect(&m_leaseTimer, &QTimer::timeout, this,
            [this] { renewDueSubscriptions(QDateTime::currentMSecsSinceEpoch()); });
    m_leaseTimer.start();
}

quint64 SubscriptionManager::makeKey(quint32 sourceId, quint16 topicId) {
    return (quint64(sourceId) << 16) | quint64(topicId);
}

quint32 SubscriptionManager::desiredRateFor(quint64 key) const {
    quint32 highest = 0;
    for (const Subscriber& subscriber : m_subscribers) {
        if (subscriber.key == key) {
            highest = qMax(highest, subscriber.rateMillihz);
        }
    }
    return highest;
}

int SubscriptionManager::subscriberCountFor(quint64 key) const {
    int count = 0;
    for (const Subscriber& subscriber : m_subscribers) {
        if (subscriber.key == key) {
            ++count;
        }
    }
    return count;
}

quint64 SubscriptionManager::addSubscriber(quint32 sourceId, quint16 topicId, quint32 requestedRateMillihz) {
    if (sourceId == 0 || topicId == 0 || requestedRateMillihz == 0) {
        return 0;  // an unconfigured widget must not put anything on the wire
    }

    const quint64 key = makeKey(sourceId, topicId);
    const quint64 handle = m_nextHandle++;
    m_subscribers.insert(handle, Subscriber{key, requestedRateMillihz});

    if (!m_topics.contains(key)) {
        TopicState topic;
        topic.sourceId = sourceId;
        topic.topicId = topicId;
        m_topics.insert(key, topic);
    }
    syncTopic(key);
    return handle;
}

quint64 SubscriptionManager::updateSubscriber(quint64 handle, quint32 sourceId, quint16 topicId,
                                              quint32 requestedRateMillihz) {
    const auto it = m_subscribers.constFind(handle);
    if (it != m_subscribers.constEnd() && it.value().key == makeKey(sourceId, topicId) &&
        it.value().rateMillihz == requestedRateMillihz) {
        return handle;  // unchanged binding: no churn on the wire
    }
    removeSubscriber(handle);
    return addSubscriber(sourceId, topicId, requestedRateMillihz);
}

void SubscriptionManager::removeSubscriber(quint64 handle) {
    const auto it = m_subscribers.constFind(handle);
    if (it == m_subscribers.constEnd()) {
        return;
    }
    const quint64 key = it.value().key;
    m_subscribers.erase(it);
    syncTopic(key);
}

void SubscriptionManager::syncTopic(quint64 key) {
    const auto it = m_topics.find(key);
    if (it == m_topics.end()) {
        return;
    }
    TopicState& topic = it.value();
    const quint32 desired = desiredRateFor(key);

    if (desired == 0) {
        // Last consumer of this topic is gone -- this is the only place an
        // UNSUBSCRIBE is ever sent (topico 17 PASSO 5).
        topic.deferredForBootId = false;
        if (topic.subscriptionId != 0) {
            sendUnsubscribe(topic);
            topic.subscriptionId = 0;
            topic.effectiveRateMillihz = 0;
            topic.grantedLeaseMs = 0;
            topic.sentRateMillihz = 0;
            topic.renewAtMs = 0;
        }
        // If a SUBSCRIBE is still in flight there is no subscription_id to
        // unsubscribe yet; handleSubscribeResult() releases the grant the
        // moment it arrives.
        dropTopicIfIdle(key);
        emit subscriptionsChanged();
        return;
    }

    const bool noGrantYet = topic.subscriptionId == 0 && topic.inFlightSequence == 0;
    if (desired != topic.sentRateMillihz || noGrantYet) {
        sendSubscribe(topic, desired);
    }
    emit subscriptionsChanged();
}

void SubscriptionManager::sendSubscribe(TopicState& topic, quint32 rateMillihz) {
    // section 4 marks target_boot_id non-zero, and only MANIFEST_DATA tells
    // this client which boot a source is on (TelemetryCatalog::sourceBootId,
    // populated by ManifestClient). Until then the request is held back
    // rather than sent with a zero/guessed boot -- onCatalogUpdated() retries.
    const quint32 bootId = m_catalog ? m_catalog->sourceBootId(topic.sourceId) : 0;
    if (bootId == 0) {
        topic.deferredForBootId = true;
        return;
    }

    QByteArray payload;
    payload.reserve(20);
    appendLe(payload, topic.sourceId, 4);
    appendLe(payload, bootId, 4);
    appendLe(payload, topic.topicId, 2);
    appendLe(payload, 0, 2);  // flags: zero in v1
    appendLe(payload, rateMillihz, 4);
    appendLe(payload, kRequestedLeaseMs, 4);

    const quint32 sequence = m_nextSequence++;
    sendControl(kControlSubscribe, payload, sequence);

    topic.targetBootId = bootId;
    topic.sentRateMillihz = rateMillihz;
    topic.inFlightSequence = sequence;
    topic.deferredForBootId = false;
    m_pendingSubscribes.insert(sequence, makeKey(topic.sourceId, topic.topicId));
}

void SubscriptionManager::sendUnsubscribe(const TopicState& topic) {
    QByteArray payload;
    payload.reserve(12);
    appendLe(payload, topic.sourceId, 4);
    appendLe(payload, topic.targetBootId, 4);
    appendLe(payload, topic.subscriptionId, 4);

    const quint32 sequence = m_nextSequence++;
    sendControl(kControlUnsubscribe, payload, sequence);
    m_pendingUnsubscribes.insert(sequence, makeKey(topic.sourceId, topic.topicId));
}

void SubscriptionManager::sendControl(quint16 objectId, const QByteArray& payload, quint32 sequence) {
    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = m_clientSourceId;
    header.boot_id = m_clientBootId;
    header.sequence = sequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = objectId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    frame.payload.size = static_cast<std::size_t>(payload.size());
    m_session->sendFrame(frame);
}

void SubscriptionManager::onControlFrameReceived(const BtpFrame& frame) {
    switch (frame.objectId) {
        case kControlSubscribeResult:
            handleSubscribeResult(frame);
            break;
        case kControlUnsubscribeResult:
            handleUnsubscribeResult(frame);
            break;
        case kControlStatus:
            handleStatus(frame);
            break;
        default:
            break;
    }
}

void SubscriptionManager::handleSubscribeResult(const BtpFrame& frame) {
    if (frame.payload.size() < kSubscribeResultSize) {
        return;
    }
    // Correlation is the full (request_source_id, request_boot_id,
    // reply_to_sequence) triple -- reply_to_sequence alone is not unique
    // (commands.md section 1).
    if (readLe32(frame.payload, 0) != m_clientSourceId || readLe32(frame.payload, 4) != m_clientBootId) {
        return;
    }
    const quint32 replyToSequence = readLe32(frame.payload, 8);
    const auto pending = m_pendingSubscribes.find(replyToSequence);
    if (pending == m_pendingSubscribes.end()) {
        return;
    }
    const quint64 key = pending.value();
    m_pendingSubscribes.erase(pending);

    const auto it = m_topics.find(key);
    if (it == m_topics.end()) {
        return;
    }
    TopicState& topic = it.value();
    if (topic.inFlightSequence != replyToSequence) {
        return;  // superseded by a newer SUBSCRIBE for the same topic
    }
    topic.inFlightSequence = 0;

    const quint8 status = quint8(frame.payload.at(12));
    const quint16 errorCode = readLe16(frame.payload, 14);
    const quint32 subscriptionId = readLe32(frame.payload, 16);
    const quint32 effectiveRate = readLe32(frame.payload, 20);
    const quint32 grantedLease = readLe32(frame.payload, 24);

    topic.lastStatus = status;
    topic.lastErrorCode = errorCode;

    if (status == kStatusSuccess && subscriptionId != 0) {
        const quint32 requested = topic.sentRateMillihz;
        topic.subscriptionId = subscriptionId;
        topic.effectiveRateMillihz = effectiveRate;
        topic.grantedLeaseMs = grantedLease;
        topic.renewAtMs = QDateTime::currentMSecsSinceEpoch() + renewIntervalFor(grantedLease);
        if (effectiveRate != 0 && effectiveRate < requested) {
            // The source clamped the request to its own schema maximum. Never
            // assume the requested rate from here on: everything the UI shows
            // comes from effectiveRateMillihz.
            emit subscriptionRateLimited(topic.sourceId, topic.topicId, requested, effectiveRate);
        }
    } else {
        topic.subscriptionId = 0;
        topic.effectiveRateMillihz = 0;
        topic.grantedLeaseMs = 0;
        topic.renewAtMs = 0;
        // Clear the remembered request so a later change (a new consumer, a
        // reconnect, a manifest update) retries instead of assuming the
        // rejected rate is still pending. Nothing retries on its own: a
        // rejection repeated verbatim would only be rejected again.
        topic.sentRateMillihz = 0;
        emit subscriptionRejected(topic.sourceId, topic.topicId, status, errorCode);
    }

    // Reconcile with whatever happened to the consumers while the request was
    // in flight (a widget closed, or another one asked for a higher rate).
    const quint32 desired = desiredRateFor(key);
    if (desired == 0) {
        if (topic.subscriptionId != 0) {
            sendUnsubscribe(topic);
            topic.subscriptionId = 0;
            topic.effectiveRateMillihz = 0;
            topic.sentRateMillihz = 0;
            topic.renewAtMs = 0;
        }
        dropTopicIfIdle(key);
    } else if (topic.subscriptionId != 0 && desired != topic.sentRateMillihz) {
        sendSubscribe(topic, desired);
    }
    emit subscriptionsChanged();
}

void SubscriptionManager::handleUnsubscribeResult(const BtpFrame& frame) {
    if (frame.payload.size() < kUnsubscribeResultSize) {
        return;
    }
    if (readLe32(frame.payload, 0) != m_clientSourceId || readLe32(frame.payload, 4) != m_clientBootId) {
        return;
    }
    const quint32 replyToSequence = readLe32(frame.payload, 8);
    const auto pending = m_pendingUnsubscribes.find(replyToSequence);
    if (pending == m_pendingUnsubscribes.end()) {
        return;
    }
    const quint64 key = pending.value();
    m_pendingUnsubscribes.erase(pending);
    // The local state was already released when the UNSUBSCRIBE went out
    // (removing a subscription that is already gone is defined as
    // SUCCESS/NONE, so a failed/duplicated one changes nothing here); this
    // only garbage-collects the topic entry.
    dropTopicIfIdle(key);
    emit subscriptionsChanged();
}

void SubscriptionManager::handleStatus(const BtpFrame& frame) {
    StatusReport report;
    if (!parseStatusPayload(frame.payload, &report)) {
        return;
    }
    m_lastStatus = report;
    if (report.statusVersion >= 2) {
        // A v2 message is a full snapshot of the emitter's tracked topics, so
        // it replaces the map wholesale (a topic nobody subscribes to anymore
        // simply stops being reported). A v1 message carries no per-topic
        // information at all and therefore leaves the map untouched rather
        // than clearing it.
        m_topicStatuses.clear();
        for (const StatusTopicRecord& record : report.topics) {
            m_topicStatuses.insert(makeKey(record.sourceId, record.topicId), record);
        }
    }
    emit statusReceived();
}

void SubscriptionManager::dropTopicIfIdle(quint64 key) {
    const auto it = m_topics.find(key);
    if (it == m_topics.end()) {
        return;
    }
    const TopicState& topic = it.value();
    if (topic.inFlightSequence != 0 || topic.subscriptionId != 0 || subscriberCountFor(key) > 0) {
        return;
    }
    m_topics.erase(it);
}

QVector<TopicSubscriptionState> SubscriptionManager::subscriptions() const {
    QVector<TopicSubscriptionState> result;
    result.reserve(m_topics.size());
    for (auto it = m_topics.constBegin(); it != m_topics.constEnd(); ++it) {
        const TopicState& topic = it.value();
        TopicSubscriptionState state;
        state.sourceId = topic.sourceId;
        state.topicId = topic.topicId;
        state.subscriberCount = subscriberCountFor(it.key());
        state.requestedRateMillihz = desiredRateFor(it.key());
        state.effectiveRateMillihz = topic.effectiveRateMillihz;
        state.grantedLeaseMs = topic.grantedLeaseMs;
        state.subscriptionId = topic.subscriptionId;
        state.lastStatus = topic.lastStatus;
        state.lastErrorCode = topic.lastErrorCode;
        state.awaitingResult = topic.inFlightSequence != 0;
        result.append(state);
    }
    return result;
}

QVector<StatusTopicRecord> SubscriptionManager::topicStatuses() const {
    QVector<StatusTopicRecord> result;
    result.reserve(m_topicStatuses.size());
    for (const StatusTopicRecord& record : m_topicStatuses) {
        result.append(record);
    }
    return result;
}

void SubscriptionManager::renewDueSubscriptions(qint64 nowMs) {
    const QList<quint64> keys = m_topics.keys();
    for (quint64 key : keys) {
        const auto it = m_topics.find(key);
        if (it == m_topics.end()) {
            continue;
        }
        TopicState& topic = it.value();
        if (topic.subscriptionId == 0 || topic.inFlightSequence != 0 || topic.renewAtMs == 0 ||
            nowMs < topic.renewAtMs) {
            continue;
        }
        const quint32 desired = desiredRateFor(key);
        if (desired == 0) {
            continue;  // no consumer left; syncTopic() already unsubscribed
        }
        // A new sequence with the same bytes renews (re-creates atomically)
        // the same session's subscription for this topic -- section 4.
        sendSubscribe(topic, desired);
    }
}

void SubscriptionManager::onSessionEstablished() {
    // Every subscription_id belonged to the session that just ended, so none
    // of them can be renewed or unsubscribed anymore -- they are dropped
    // silently and re-requested below (topico 17 PASSO 6).
    m_pendingSubscribes.clear();
    m_pendingUnsubscribes.clear();
    for (auto it = m_topics.begin(); it != m_topics.end(); ++it) {
        TopicState& topic = it.value();
        topic.subscriptionId = 0;
        topic.effectiveRateMillihz = 0;
        topic.grantedLeaseMs = 0;
        topic.sentRateMillihz = 0;
        topic.inFlightSequence = 0;
        topic.renewAtMs = 0;
        topic.deferredForBootId = false;
    }
    const QList<quint64> keys = m_topics.keys();
    for (quint64 key : keys) {
        syncTopic(key);
    }
    emit subscriptionsChanged();
}

void SubscriptionManager::onSessionLost() {
    m_pendingSubscribes.clear();
    m_pendingUnsubscribes.clear();
    const QList<quint64> keys = m_topics.keys();
    for (quint64 key : keys) {
        const auto it = m_topics.find(key);
        if (it == m_topics.end()) {
            continue;
        }
        TopicState& topic = it.value();
        topic.subscriptionId = 0;
        topic.effectiveRateMillihz = 0;
        topic.grantedLeaseMs = 0;
        topic.sentRateMillihz = 0;
        topic.inFlightSequence = 0;
        topic.renewAtMs = 0;
        topic.deferredForBootId = false;
        // Consumers stay registered: they are widgets that are still open.
        dropTopicIfIdle(key);
    }
    emit subscriptionsChanged();
}

void SubscriptionManager::onCatalogUpdated() {
    const QList<quint64> keys = m_topics.keys();
    for (quint64 key : keys) {
        const auto it = m_topics.constFind(key);
        if (it != m_topics.constEnd() && it.value().deferredForBootId) {
            syncTopic(key);
        }
    }
}

}  // namespace traceview
