#include <QtTest>
#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"
#include "protocol/statusreport.h"
#include "protocol/subscriptionmanager.h"
#include "protocol/telemetrycatalog.h"

using traceview::BtpFrame;
using traceview::BtpSession;
using traceview::ProtocolRouter;
using traceview::StatusTopicRecord;
using traceview::SubscriptionManager;
using traceview::TelemetryCatalog;
using traceview::TopicSubscriptionState;

namespace {

constexpr quint32 kSourceId = 0x9F442484;
constexpr quint32 kBootId = 0x00ABCDEF;
constexpr quint16 kTopicId = 0x0001;
constexpr quint16 kOtherTopicId = 0x0002;

constexpr quint16 kControlSubscribe = 0x0005;
constexpr quint16 kControlSubscribeResult = 0x0006;
constexpr quint16 kControlUnsubscribe = 0x0007;
constexpr quint16 kControlStatus = 0x0009;

void appendLe(QByteArray& out, quint64 value, int width) {
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

// Drives a real SubscriptionManager over a real BtpSession: everything it
// sends is COBS-encoded by the outbound session and decoded back by a second
// one, so the tests below assert on genuinely serialized frames (identity,
// sequence and payload as they'd appear on the wire) instead of on a mock.
class Harness {
public:
    explicit Harness(bool bootIdKnown = true) {
        if (bootIdKnown) {
            // What ManifestClient records from MANIFEST_DATA; SUBSCRIBE needs
            // it for target_boot_id (commands.md section 4).
            catalog.registerSourceBootId(kSourceId, kBootId);
        }
        QObject::connect(&outbound, &BtpSession::bytesToWrite, &loopback, &BtpSession::feedBytes);
        QObject::connect(&loopback, &BtpSession::frameReceived, &loopback,
                         [this](const BtpFrame& frame) { sent.append(frame); });
    }

    // Feeds an inbound CONTROL message, exactly as ProtocolRouter would after
    // BtpSession decoded it off the wire.
    void deliverControl(quint16 objectId, const QByteArray& payload) {
        BtpFrame frame;
        frame.type = btp::MessageType::Control;
        frame.sourceId = 0xDEADBEEF;  // the dongle; irrelevant to correlation
        frame.bootId = 0x12345678;
        frame.sequence = ++peerSequence;
        frame.objectId = objectId;
        frame.payload = payload;
        router.onFrameReceived(frame);
    }

    // Answers `request` (a SUBSCRIBE captured from the wire) the way the
    // source would, echoing the request's own identity triple back.
    void grantSubscribe(const BtpFrame& request, quint32 subscriptionId,
                        quint32 effectiveRateMillihz, quint32 grantedLeaseMs = 15000,
                        quint8 status = 0x00, quint16 errorCode = 0x0000) {
        QByteArray payload;
        appendLe(payload, request.sourceId, 4);
        appendLe(payload, request.bootId, 4);
        appendLe(payload, request.sequence, 4);
        payload.append(char(status));
        payload.append(char(0));  // reserved
        appendLe(payload, errorCode, 2);
        appendLe(payload, subscriptionId, 4);
        appendLe(payload, effectiveRateMillihz, 4);
        appendLe(payload, grantedLeaseMs, 4);
        deliverControl(kControlSubscribeResult, payload);
    }

    const BtpFrame& lastSent() const {
        return sent.last();
    }

    BtpSession outbound;
    BtpSession loopback;
    ProtocolRouter router;
    TelemetryCatalog catalog;
    SubscriptionManager manager{&outbound, &router, &catalog};
    QVector<BtpFrame> sent;
    quint32 peerSequence = 0;
};

class TestSubscriptionManager : public QObject {
    Q_OBJECT

private slots:
    void twoConsumersOfOneTopicSendASingleSubscribe();
    void aHigherRateConsumerReplacesTheSubscription();
    void closingOneOfSeveralConsumersSendsNothing();
    void closingTheLastConsumerSendsUnsubscribe();
    void loweringTheTopRateResubscribesInsteadOfUnsubscribing();
    void limitedEffectiveRateIsReportedNotAssumed();
    void rejectedSubscribeIsReportedAndLeavesNoGrant();
    void subscribeWaitsForTheSourceBootIdFromTheManifest();
    void reconnectResubscribesEveryLiveConsumer();
    void peerRebootResubscribesOnlyThatSourceAgainstTheNewBoot();
    void leaseIsRenewedWhileAConsumerExists();
    void statusVersion2FeedsPerTopicMetrics();
};

void TestSubscriptionManager::twoConsumersOfOneTopicSendASingleSubscribe() {
    Harness h;

    const quint64 fastWidget = h.manager.addSubscriber(kSourceId, kTopicId, 10000);  // 10 Hz
    QVERIFY(fastWidget != 0);
    QCOMPARE(h.sent.size(), 1);

    // A second chart plotting another field of the same topic, at a lower
    // rate: the topic is already covered, so nothing goes on the wire.
    const quint64 slowWidget = h.manager.addSubscriber(kSourceId, kTopicId, 5000);
    QVERIFY(slowWidget != 0);
    QVERIFY(slowWidget != fastWidget);
    QCOMPARE(h.sent.size(), 1);

    const BtpFrame& subscribe = h.sent.at(0);
    QCOMPARE(subscribe.type, btp::MessageType::Control);
    QCOMPARE(subscribe.objectId, kControlSubscribe);
    QCOMPARE(subscribe.payload.size(), 20);
    QCOMPARE(readLe32(subscribe.payload, 0), kSourceId);
    QCOMPARE(readLe32(subscribe.payload, 4), kBootId);
    QCOMPARE(readLe16(subscribe.payload, 8), kTopicId);
    QCOMPARE(readLe16(subscribe.payload, 10), quint16(0));      // flags: zero in v1
    QCOMPARE(readLe32(subscribe.payload, 12), quint32(10000));  // the highest rate asked for
    QCOMPARE(readLe32(subscribe.payload, 16), SubscriptionManager::kRequestedLeaseMs);

    const QVector<TopicSubscriptionState> states = h.manager.subscriptions();
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.at(0).subscriberCount, 2);
    QCOMPARE(states.at(0).requestedRateMillihz, quint32(10000));

    // A different topic of the same source is a separate subscription.
    h.manager.addSubscriber(kSourceId, kOtherTopicId, 1000);
    QCOMPARE(h.sent.size(), 2);
    QCOMPARE(readLe16(h.lastSent().payload, 8), kOtherTopicId);
}

void TestSubscriptionManager::aHigherRateConsumerReplacesTheSubscription() {
    Harness h;
    h.manager.addSubscriber(kSourceId, kTopicId, 5000);
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/11, /*effectiveRateMillihz=*/5000);
    QCOMPARE(h.sent.size(), 1);

    // A faster consumer joins: one new SUBSCRIBE (a new sequence replaces the
    // subscription atomically, section 4), never a second parallel one.
    h.manager.addSubscriber(kSourceId, kTopicId, 50000);
    QCOMPARE(h.sent.size(), 2);
    QCOMPARE(h.lastSent().objectId, kControlSubscribe);
    QCOMPARE(readLe32(h.lastSent().payload, 12), quint32(50000));
    QVERIFY(h.lastSent().sequence != h.sent.at(0).sequence);
}

void TestSubscriptionManager::closingOneOfSeveralConsumersSendsNothing() {
    Harness h;
    const quint64 first = h.manager.addSubscriber(kSourceId, kTopicId, 10000);
    const quint64 second = h.manager.addSubscriber(kSourceId, kTopicId, 5000);
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/21, /*effectiveRateMillihz=*/10000);
    QCOMPARE(h.sent.size(), 1);

    // CRITERIO DE ACEITE, contrapositive: closing a chart while another one
    // still reads the topic must not reduce traffic -- and must not touch the
    // wire at all, since the rate is unchanged.
    h.manager.removeSubscriber(second);
    QCOMPARE(h.sent.size(), 1);
    QCOMPARE(h.manager.subscriptions().size(), 1);
    QCOMPARE(h.manager.subscriptions().at(0).subscriberCount, 1);
    QCOMPARE(h.manager.subscriptions().at(0).subscriptionId, quint32(21));
    Q_UNUSED(first);
}

void TestSubscriptionManager::closingTheLastConsumerSendsUnsubscribe() {
    Harness h;
    const quint64 first = h.manager.addSubscriber(kSourceId, kTopicId, 10000);
    const quint64 second = h.manager.addSubscriber(kSourceId, kTopicId, 5000);
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/31, /*effectiveRateMillihz=*/10000);

    h.manager.removeSubscriber(second);
    QCOMPARE(h.sent.size(), 1);

    h.manager.removeSubscriber(first);
    QCOMPARE(h.sent.size(), 2);
    const BtpFrame& unsubscribe = h.lastSent();
    QCOMPARE(unsubscribe.objectId, kControlUnsubscribe);
    QCOMPARE(unsubscribe.payload.size(), 12);
    QCOMPARE(readLe32(unsubscribe.payload, 0), kSourceId);
    QCOMPARE(readLe32(unsubscribe.payload, 4), kBootId);
    QCOMPARE(readLe32(unsubscribe.payload, 8), quint32(31));  // the granted subscription_id
    QVERIFY(h.manager.subscriptions().isEmpty());
}

void TestSubscriptionManager::loweringTheTopRateResubscribesInsteadOfUnsubscribing() {
    Harness h;
    const quint64 fast = h.manager.addSubscriber(kSourceId, kTopicId, 50000);
    h.manager.addSubscriber(kSourceId, kTopicId, 5000);
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/41, /*effectiveRateMillihz=*/50000);
    QCOMPARE(h.sent.size(), 1);

    // The consumer that wanted the top rate goes away, a slower one stays:
    // the topic must keep flowing, but slower.
    h.manager.removeSubscriber(fast);
    QCOMPARE(h.sent.size(), 2);
    QCOMPARE(h.lastSent().objectId, kControlSubscribe);
    QCOMPARE(readLe32(h.lastSent().payload, 12), quint32(5000));
    QCOMPARE(h.manager.subscriptions().at(0).requestedRateMillihz, quint32(5000));
}

void TestSubscriptionManager::limitedEffectiveRateIsReportedNotAssumed() {
    Harness h;
    QSignalSpy limitedSpy(&h.manager, &SubscriptionManager::subscriptionRateLimited);

    h.manager.addSubscriber(kSourceId, kTopicId, 200000);  // 200 Hz, above the source's max
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/51, /*effectiveRateMillihz=*/50000);

    QCOMPARE(limitedSpy.size(), 1);
    const QList<QVariant> args = limitedSpy.at(0);
    QCOMPARE(args.at(0).toUInt(), kSourceId);
    QCOMPARE(args.at(1).toUInt(), quint32(kTopicId));
    QCOMPARE(args.at(2).toUInt(), quint32(200000));  // requested
    QCOMPARE(args.at(3).toUInt(), quint32(50000));   // effective

    const TopicSubscriptionState state = h.manager.subscriptions().at(0);
    QCOMPARE(state.effectiveRateMillihz, quint32(50000));
    QCOMPARE(state.requestedRateMillihz, quint32(200000));
    QVERIFY(state.rateLimited());

    // A grant at exactly the requested rate is not "limited".
    Harness exact;
    QSignalSpy exactSpy(&exact.manager, &SubscriptionManager::subscriptionRateLimited);
    exact.manager.addSubscriber(kSourceId, kTopicId, 50000);
    exact.grantSubscribe(exact.lastSent(), /*subscriptionId=*/52, /*effectiveRateMillihz=*/50000);
    QCOMPARE(exactSpy.size(), 0);
    QVERIFY(!exact.manager.subscriptions().at(0).rateLimited());
}

void TestSubscriptionManager::rejectedSubscribeIsReportedAndLeavesNoGrant() {
    Harness h;
    QSignalSpy rejectedSpy(&h.manager, &SubscriptionManager::subscriptionRejected);

    h.manager.addSubscriber(kSourceId, kTopicId, 50000);
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/0, /*effectiveRateMillihz=*/0,
                     /*grantedLeaseMs=*/0,
                     /*status=*/0x06 /*BUSY*/, /*errorCode=*/0x0005 /*CAPACITY_EXHAUSTED*/);

    QCOMPARE(rejectedSpy.size(), 1);
    QCOMPARE(rejectedSpy.at(0).at(3).toUInt(), quint32(0x0005));
    const TopicSubscriptionState state = h.manager.subscriptions().at(0);
    QCOMPARE(state.subscriptionId, quint32(0));
    QCOMPARE(state.effectiveRateMillihz, quint32(0));
    QVERIFY(!state.rateLimited());
    // Nothing is retried on its own: an identical request would only be
    // rejected again.
    QCOMPARE(h.sent.size(), 1);
}

void TestSubscriptionManager::subscribeWaitsForTheSourceBootIdFromTheManifest() {
    Harness h(/*bootIdKnown=*/false);

    // No MANIFEST_DATA seen for this source yet, so target_boot_id (which
    // section 4 marks non-zero) is unknown -- the request is held, never sent
    // with a guessed boot.
    h.manager.addSubscriber(kSourceId, kTopicId, 10000);
    QCOMPARE(h.sent.size(), 0);

    h.catalog.registerSourceBootId(kSourceId, kBootId);
    h.manager.onCatalogUpdated();
    QCOMPARE(h.sent.size(), 1);
    QCOMPARE(readLe32(h.lastSent().payload, 4), kBootId);
}

void TestSubscriptionManager::reconnectResubscribesEveryLiveConsumer() {
    Harness h;
    h.manager.addSubscriber(kSourceId, kTopicId, 10000);
    h.manager.addSubscriber(kSourceId, kOtherTopicId, 2000);
    h.grantSubscribe(h.sent.at(0), /*subscriptionId=*/61, /*effectiveRateMillihz=*/10000);
    h.grantSubscribe(h.sent.at(1), /*subscriptionId=*/62, /*effectiveRateMillihz=*/2000);
    QCOMPARE(h.sent.size(), 2);

    // The link dropped: the grants died with the session, and sending
    // anything down a dead session is pointless.
    h.manager.onSessionLost();
    QCOMPARE(h.sent.size(), 2);
    QCOMPARE(h.manager.subscriptions().at(0).subscriptionId, quint32(0));

    // New session: both still-open widgets are re-subscribed.
    h.manager.onSessionEstablished();
    QCOMPARE(h.sent.size(), 4);
    QVector<quint16> topics{readLe16(h.sent.at(2).payload, 8), readLe16(h.sent.at(3).payload, 8)};
    std::sort(topics.begin(), topics.end());
    QCOMPARE(topics, QVector<quint16>({kTopicId, kOtherTopicId}));
    QCOMPARE(h.sent.at(2).objectId, kControlSubscribe);
    QCOMPARE(h.sent.at(3).objectId, kControlSubscribe);
}

void TestSubscriptionManager::peerRebootResubscribesOnlyThatSourceAgainstTheNewBoot() {
    constexpr quint32 kOtherSourceId = 0x41770972;
    constexpr quint32 kNewBootId = 0x00FEDCBA;

    Harness h;
    h.catalog.registerSourceBootId(kOtherSourceId, kBootId);
    h.manager.addSubscriber(kSourceId, kTopicId, 10000);
    h.manager.addSubscriber(kOtherSourceId, kTopicId, 4000);
    h.grantSubscribe(h.sent.at(0), /*subscriptionId=*/71, /*effectiveRateMillihz=*/10000);
    h.grantSubscribe(h.sent.at(1), /*subscriptionId=*/72, /*effectiveRateMillihz=*/4000);
    QCOMPARE(h.sent.size(), 2);

    // The robot power-cycled. ManifestClient has already recorded its new boot
    // in the catalog before BtpBackend calls this.
    h.catalog.registerSourceBootId(kSourceId, kNewBootId);
    h.manager.onPeerRebooted(kSourceId);

    // Exactly one new SUBSCRIBE, for the rebooted source only, carrying the
    // new boot. The other source's grant is untouched.
    QCOMPARE(h.sent.size(), 3);
    const BtpFrame& resub = h.sent.at(2);
    QCOMPARE(resub.objectId, kControlSubscribe);
    QCOMPARE(readLe32(resub.payload, 0), kSourceId);
    QCOMPARE(readLe32(resub.payload, 4), kNewBootId);
    QCOMPARE(readLe16(resub.payload, 8), kTopicId);

    for (const TopicSubscriptionState& s : h.manager.subscriptions()) {
        if (s.sourceId == kOtherSourceId) {
            QCOMPARE(s.subscriptionId, quint32(72));  // never disturbed
        } else {
            QCOMPARE(s.subscriptionId, quint32(0));  // void until the new grant lands
        }
    }

    // A late SUBSCRIBE_RESULT for the pre-reboot request must not be mistaken
    // for the reply to the re-subscribe.
    h.grantSubscribe(h.sent.at(0), /*subscriptionId=*/71, /*effectiveRateMillihz=*/10000);
    for (const TopicSubscriptionState& s : h.manager.subscriptions()) {
        if (s.sourceId == kSourceId) {
            QCOMPARE(s.subscriptionId, quint32(0));  // still waiting on the new grant
        }
    }
    h.grantSubscribe(resub, /*subscriptionId=*/73, /*effectiveRateMillihz=*/10000);
    for (const TopicSubscriptionState& s : h.manager.subscriptions()) {
        if (s.sourceId == kSourceId) {
            QCOMPARE(s.subscriptionId, quint32(73));
        }
    }
}

void TestSubscriptionManager::leaseIsRenewedWhileAConsumerExists() {
    Harness h;
    h.manager.addSubscriber(kSourceId, kTopicId, 10000);
    h.grantSubscribe(h.lastSent(), /*subscriptionId=*/71, /*effectiveRateMillihz=*/10000,
                     /*grantedLeaseMs=*/2000);
    QCOMPARE(h.sent.size(), 1);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    h.manager.renewDueSubscriptions(now + 500);  // well inside the lease
    QCOMPARE(h.sent.size(), 1);

    h.manager.renewDueSubscriptions(now + 1500);  // past half the granted lease
    QCOMPARE(h.sent.size(), 2);
    QCOMPARE(h.lastSent().objectId, kControlSubscribe);
    QCOMPARE(readLe32(h.lastSent().payload, 12), quint32(10000));
    QVERIFY(h.lastSent().sequence != h.sent.at(0).sequence);  // a renewal is a new sequence
}

void TestSubscriptionManager::statusVersion2FeedsPerTopicMetrics() {
    Harness h;
    QSignalSpy statusSpy(&h.manager, &SubscriptionManager::statusReceived);

    QByteArray v1;
    appendLe(v1, 1, 2);  // status_version
    appendLe(v1, 0, 2);  // flags
    for (int i = 0; i < 11; ++i) {
        appendLe(v1, 100 + i, 8);
    }
    QCOMPARE(v1.size(), 92);
    h.deliverControl(kControlStatus, v1);
    QCOMPARE(statusSpy.size(), 1);
    QCOMPARE(h.manager.lastStatus().statusVersion, quint16(1));
    QCOMPARE(h.manager.lastStatus().framesRx, quint64(101));
    QVERIFY(h.manager.topicStatuses().isEmpty());  // v1 carries no per-topic data

    QByteArray v2 = v1;
    v2[0] = char(2);
    appendLe(v2, 1, 2);  // topic_status_count
    appendLe(v2, kSourceId, 4);
    appendLe(v2, kTopicId, 2);
    appendLe(v2, 3, 2);       // subscriber_count at the source
    appendLe(v2, 48300, 4);   // effective_rate_millihz
    appendLe(v2, 123456, 8);  // bytes_total
    appendLe(v2, 7, 8);       // samples_dropped_total
    h.deliverControl(kControlStatus, v2);

    QCOMPARE(statusSpy.size(), 2);
    QCOMPARE(h.manager.lastStatus().statusVersion, quint16(2));
    const QVector<StatusTopicRecord> topics = h.manager.topicStatuses();
    QCOMPARE(topics.size(), 1);
    QCOMPARE(topics.at(0).sourceId, kSourceId);
    QCOMPARE(topics.at(0).topicId, kTopicId);
    QCOMPARE(topics.at(0).subscriberCount, quint16(3));
    QCOMPARE(topics.at(0).effectiveRateMillihz, quint32(48300));
    QCOMPARE(topics.at(0).bytesTotal, quint64(123456));
    QCOMPARE(topics.at(0).samplesDroppedTotal, quint64(7));
}

}  // namespace

QTEST_MAIN(TestSubscriptionManager)
#include "test_subscriptionmanager.moc"
