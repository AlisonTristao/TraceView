// TopicSampleDispatcher (one Backend::fieldSample connection, each sample to
// the receivers of its own (source, topic)) and RepaintClock (one repaint
// tick for every requesting widget).

#include <QWidget>
#include <QtTest>

#include "backend/backend.h"
#include "dashboard/repaintclock.h"
#include "dashboard/topicsampledispatcher.h"

using namespace traceview;

namespace {

class StubBackend : public Backend {
public:
    using Backend::Backend;
    quint64 addSubscriber(quint32, quint16, quint32) override {
        return 0;
    }
    quint64 updateSubscriber(quint64, quint32, quint16, quint32) override {
        return 0;
    }
    void removeSubscriber(quint64) override {}
    QVector<TopicSubscriptionState> subscriptions() const override {
        return {};
    }
    QVector<StatusTopicRecord> topicStatuses() const override {
        return {};
    }
    QVector<CatalogTopicInfo> catalogTopics() const override {
        return {};
    }
    void feedBytes(const QByteArray&) override {}
    void onTransportConnectionChanged(bool) override {}
    void sendTerminalIn(const QByteArray&) override {}
    void sendCommand(const QByteArray&) override {}

    void emitSample(quint32 source, quint16 topic, quint16 field, double value) {
        TelemetryFieldBinding binding;
        binding.sourceId = source;
        binding.topicId = topic;
        binding.fieldId = field;
        emit fieldSample(binding, 1, value);
    }
};

class PaintCounter : public QWidget {
public:
    int paints = 0;

protected:
    void paintEvent(QPaintEvent*) override {
        ++paints;
    }
};

}  // namespace

class TestTopicSampleDispatcher : public QObject {
    Q_OBJECT

private slots:
    void onePerBackend() {
        StubBackend backend;
        TopicSampleDispatcher* a = TopicSampleDispatcher::of(&backend);
        QVERIFY(a != nullptr);
        QCOMPARE(TopicSampleDispatcher::of(&backend), a);
        QVERIFY(TopicSampleDispatcher::of(nullptr) == nullptr);
    }

    void samplesReachOnlyTheirTopic() {
        StubBackend backend;
        QObject chartA;
        QObject chartB;
        QVector<double> gotA;
        QVector<double> gotB;
        TopicSampleDispatcher* dispatcher = TopicSampleDispatcher::of(&backend);
        dispatcher->add(7, 1, &chartA,
                        [&](const TelemetryFieldBinding&, quint64, double v) { gotA.append(v); });
        dispatcher->add(7, 2, &chartB,
                        [&](const TelemetryFieldBinding&, quint64, double v) { gotB.append(v); });
        backend.emitSample(7, 1, 0, 1.0);
        backend.emitSample(7, 2, 0, 2.0);
        backend.emitSample(8, 1, 0, 3.0);  // same topic id, another source
        QCOMPARE(gotA, QVector<double>{1.0});
        QCOMPARE(gotB, QVector<double>{2.0});
    }

    void removeAndDestroyedReceiversStop() {
        StubBackend backend;
        TopicSampleDispatcher* dispatcher = TopicSampleDispatcher::of(&backend);
        int calls = 0;
        QObject kept;
        auto* gone = new QObject;
        dispatcher->add(1, 1, &kept,
                        [&](const TelemetryFieldBinding&, quint64, double) { ++calls; });
        dispatcher->add(1, 1, gone,
                        [&](const TelemetryFieldBinding&, quint64, double) { ++calls; });
        backend.emitSample(1, 1, 0, 0.0);
        QCOMPARE(calls, 2);
        delete gone;
        backend.emitSample(1, 1, 0, 0.0);
        QCOMPARE(calls, 3);
        dispatcher->remove(&kept);
        backend.emitSample(1, 1, 0, 0.0);
        QCOMPARE(calls, 3);
        QCOMPARE(dispatcher->count(), 0);
    }

    void repaintClockCoalescesRequests() {
        RepaintClock::instance().setIntervalMs(5);
        PaintCounter a;
        PaintCounter b;
        a.resize(20, 20);
        b.resize(20, 20);
        a.show();
        b.show();
        QVERIFY(QTest::qWaitForWindowExposed(&a));
        QVERIFY(QTest::qWaitForWindowExposed(&b));
        QTest::qWait(50);
        const int a0 = a.paints;
        const int b0 = b.paints;
        for (int i = 0; i < 10; ++i) {
            RepaintClock::instance().request(&a);
            RepaintClock::instance().request(&b);
        }
        QTRY_VERIFY(a.paints > a0 && b.paints > b0);
        QTest::qWait(50);
        // Ten requests before the tick are one repaint each.
        QCOMPARE(a.paints, a0 + 1);
        QCOMPARE(b.paints, b0 + 1);
    }
};

QTEST_MAIN(TestTopicSampleDispatcher)
#include "test_topicsampledispatcher.moc"
