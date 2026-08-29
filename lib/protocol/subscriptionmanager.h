#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QtGlobal>
#include <functional>

#include "protocol/statusreport.h"
#include "telemetry/subscriptionstate.h"

namespace traceview {

class BtpSession;
class ProtocolRouter;
class TelemetryCatalog;
struct BtpFrame;

// Aggregates every widget's interest in a telemetry topic into a single
// wire-level SUBSCRIBE per (source_id, topic_id) -- topico 17 PASSOS 2/5/6/10,
// commands.md section 4.
//
// The model is a reference count, not one subscription per widget: several
// charts/gauges routinely plot different fields of the same topic (decision 10
// of PLANO_GERAL.txt -- a widget binds to source + topic + field, and the wire
// only ever carries whole topics). So:
//   - the first consumer of a topic triggers one SUBSCRIBE carrying the
//     highest rate any live consumer asked for;
//   - a second consumer of the same topic adds no traffic at all unless it
//     wants a *higher* rate, in which case one new SUBSCRIBE replaces the old
//     one atomically (section 4: "a new sequence atomically creates or
//     replaces the subscription for that session and topic");
//   - closing one of several consumers sends nothing, except a rate-lowering
//     SUBSCRIBE when the one that left was the one asking for the top rate;
//   - only the last consumer leaving sends UNSUBSCRIBE.
//
// Deliberately independent of any widget type (QtCore only, like the rest of
// traceview_protocol): consumers are opaque handles, so the same manager
// serves chart widgets, gauges, or a future headless recorder. MainWindow is
// what maps a DashboardWidget's configured (sourceId, topicId) onto
// addSubscriber()/removeSubscriber().
//
// Session lifetime (PASSO 6): a subscription is scoped to the BTP session that
// created it, so onSessionEstablished() drops every remembered
// subscription_id and re-sends SUBSCRIBE for whatever consumers are still
// alive; onSessionLost() only forgets the grants, never the consumers. Leases
// are renewed while a consumer exists (section 4: "a subscription expires
// after its lease unless renewed by another SUBSCRIBE").
//
// This class also consumes CONTROL/STATUS (section 5/5.1) so the per-topic
// effective rate/bytes/drops a `status_version=2` emitter publishes can be
// shown next to what this client asked for; a `status_version=1` emitter is
// read exactly as before (see statusreport.h).
class SubscriptionManager : public QObject {
    Q_OBJECT

public:
    // Lease this client asks for. Matches the session_timeout_ms BtpHandshake
    // advertises in HELLO, so a link that has gone quiet expires the
    // subscription on the source side at roughly the same time the session
    // itself would be considered dead.
    static constexpr quint32 kRequestedLeaseMs = 15000;

    explicit SubscriptionManager(BtpSession* session, ProtocolRouter* router,
                                 TelemetryCatalog* catalog, QObject* parent = nullptr);

    // Registers one consumer of (sourceId, topicId) wanting at least
    // `requestedRateMillihz`, and returns an opaque, never-reused handle to
    // pass back to removeSubscriber()/updateSubscriber(). Returns 0 (no
    // consumer registered) when any argument is zero -- a widget with no
    // topic configured yet must not generate wire traffic.
    quint64 addSubscriber(quint32 sourceId, quint16 topicId, quint32 requestedRateMillihz);

    // Moves an existing consumer to another topic and/or rate (a widget whose
    // config was edited), returning the handle to keep using -- `handle` when
    // the binding is unchanged, a fresh one otherwise, 0 when the new binding
    // is empty. Passing 0 as `handle` is the same as addSubscriber().
    quint64 updateSubscriber(quint64 handle, quint32 sourceId, quint16 topicId,
                             quint32 requestedRateMillihz);

    // Drops one consumer. Sends UNSUBSCRIBE only if it was the last one for
    // its topic; lowers the rate with a new SUBSCRIBE if the remaining
    // consumers all want less than this one did. A handle of 0 or an unknown
    // handle is ignored.
    void removeSubscriber(quint64 handle);

    // Current per-topic view, for display. Only topics with at least one live
    // consumer (or an in-flight request for one) appear.
    QVector<TopicSubscriptionState> subscriptions() const;

    // Last per-topic metrics received in a `status_version=2` STATUS, keyed
    // by (source_id, topic_id). Empty while the peer only publishes
    // `status_version=1`.
    QVector<StatusTopicRecord> topicStatuses() const;

    // Whole last STATUS payload, v1 or v2 (all zeroes before the first one).
    const StatusReport& lastStatus() const {
        return m_lastStatus;
    }

    // Renews any grant at or past its renewal deadline by re-sending
    // SUBSCRIBE with a new sequence. Driven internally by a 1 s timer; taking
    // `nowMs` explicitly keeps it callable from a test without waiting on
    // wall-clock time.
    void renewDueSubscriptions(qint64 nowMs);

    // Makes this manager speak as a hub-channel device's own identity instead
    // of its private per-process (m_clientSourceId/m_clientBootId) one, and
    // seal every SUBSCRIBE/UNSUBSCRIBE it sends under `endpointKey` (channel
    // B, key E) -- see BtpBackend::setHubEndpoint(). `endpointKey` empty
    // means "hub, but no key configured yet": sendControl() then refuses to
    // send rather than transmit in the clear to a robot. `nextSequence` MUST
    // be the same shared counter every other sealed message this backend
    // originates draws from -- see CommandClient::configure()'s comment on
    // why. Passing `selfSourceId == 0` (the default) restores the private
    // per-process identity, unsealed -- the console/dongle-facing behavior
    // this class always had.
    void setEndpointIdentity(quint32 selfSourceId, quint32 selfBootId,
                             const QByteArray& endpointKey,
                             std::function<quint32()> nextSequence);

public slots:
    // A fresh BTP session came up: every subscription_id from the previous
    // one is void, so re-subscribe everything that still has a consumer.
    void onSessionEstablished();

    // The session/transport went away. Grants are forgotten (they died with
    // the session), consumers are not -- reconnecting re-subscribes them.
    void onSessionLost();

    // One source rebooted (new boot_id) while the session/transport stayed up
    // -- what a hub child sees when its robot power-cycles. Every grant this
    // client held for that source is void (the robot lost its per-boot
    // subscription state), so drop them and re-SUBSCRIBE the still-live
    // consumers. Scoped to one source: unlike onSessionEstablished(), no other
    // source is touched. TelemetryCatalog::sourceBootId(sourceId) must already
    // hold the NEW boot -- ManifestClient applies the MANIFEST_DATA before
    // BtpBackend calls this.
    void onPeerRebooted(quint32 sourceId);

    // Wired to ManifestClient::catalogUpdated: SUBSCRIBE needs a non-zero
    // target_boot_id (section 4), which only MANIFEST_DATA supplies, so a
    // subscription requested before its source's manifest arrived is held
    // back and sent from here.
    void onCatalogUpdated();

signals:
    // Any change to a topic's state (grant, rejection, rate change,
    // subscriber count) -- a UI refresh hook, coarse on purpose.
    void subscriptionsChanged();
    // The source granted less than was asked for. CRITERIO DE ACEITE
    // "pedido acima do maximo e limitado e informado ao cliente".
    void subscriptionRateLimited(quint32 sourceId, quint16 topicId, quint32 requestedMillihz,
                                 quint32 effectiveMillihz);
    // SUBSCRIBE_RESULT came back with a non-SUCCESS status (section 1 codes).
    void subscriptionRejected(quint32 sourceId, quint16 topicId, quint8 status, quint16 errorCode);
    // A STATUS message was decoded (either version).
    void statusReceived();

private slots:
    void onControlFrameReceived(const traceview::BtpFrame& frame);

private:
    struct Subscriber {
        quint64 key = 0;
        quint32 rateMillihz = 0;
    };

    struct TopicState {
        quint32 sourceId = 0;
        quint16 topicId = 0;
        quint32 targetBootId = 0;      // boot this subscription was addressed to
        quint32 sentRateMillihz = 0;   // rate carried by the newest SUBSCRIBE sent
        quint32 inFlightSequence = 0;  // 0 = no SUBSCRIBE awaiting its result
        quint32 subscriptionId = 0;
        quint32 effectiveRateMillihz = 0;
        quint32 grantedLeaseMs = 0;
        qint64 renewAtMs = 0;
        bool deferredForBootId = false;  // waiting on MANIFEST_DATA for target_boot_id
        quint8 lastStatus = 0;
        quint16 lastErrorCode = 0;
    };

    static quint64 makeKey(quint32 sourceId, quint16 topicId);

    quint32 desiredRateFor(quint64 key) const;
    int subscriberCountFor(quint64 key) const;
    // m_nextEndpointSequence() when set (hub-channel identity, shared with
    // every other sealed message this backend sends), else the private
    // per-process m_nextSequence++ this class always used.
    quint32 nextSequence();
    // Brings the wire state of `key` in line with its consumers: subscribes,
    // re-subscribes at a new rate, unsubscribes, or does nothing.
    void syncTopic(quint64 key);
    void sendSubscribe(TopicState& topic, quint32 rateMillihz);
    void sendUnsubscribe(const TopicState& topic);
    void sendControl(quint16 objectId, const QByteArray& payload, quint32 sequence);
    void handleSubscribeResult(const BtpFrame& frame);
    void handleUnsubscribeResult(const BtpFrame& frame);
    void handleStatus(const BtpFrame& frame);
    // Erases `key` once it has no consumers, no grant and nothing in flight.
    void dropTopicIfIdle(quint64 key);

    BtpSession* m_session;
    TelemetryCatalog* m_catalog;

    quint32 m_clientSourceId;
    quint32 m_clientBootId;
    quint32 m_nextSequence = 1;
    quint64 m_nextHandle = 1;

    // Set by setEndpointIdentity() for a hub-channel device; m_endpointSourceId
    // == 0 (the default) means "not a hub, use the private identity above,
    // unsealed" -- see that method's comment.
    quint32 m_endpointSourceId = 0;
    quint32 m_endpointBootId = 0;
    QByteArray m_endpointKey;
    std::function<quint32()> m_nextEndpointSequence;

    QHash<quint64, Subscriber> m_subscribers;       // handle -> consumer
    QHash<quint64, TopicState> m_topics;            // (source, topic) key -> wire state
    QHash<quint32, quint64> m_pendingSubscribes;    // request sequence -> topic key
    QHash<quint32, quint64> m_pendingUnsubscribes;  // request sequence -> topic key

    StatusReport m_lastStatus;
    QHash<quint64, StatusTopicRecord> m_topicStatuses;

    QTimer m_leaseTimer;
};

}  // namespace traceview
