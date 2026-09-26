#include <QtTest>

#include "telemetry/telemetryhistory.h"

using traceview::TelemetryFieldBinding;
using traceview::TelemetryHistory;
using traceview::TelemetryHistoryKey;

namespace {

const TelemetryHistoryKey kKey{"dev-a", 7, 3, 2};

TelemetryFieldBinding bindingFor(const TelemetryHistoryKey& key, quint16 elementIndex = 0) {
    TelemetryFieldBinding binding;
    binding.sourceId = key.sourceId;
    binding.topicId = key.topicId;
    binding.fieldId = key.fieldId;
    binding.elementIndex = elementIndex;
    return binding;
}

class TestTelemetryHistory : public QObject {
    Q_OBJECT

private slots:
    void dropsUnretainedKeys();
    void recordsRetainedKeyUpToCapacity();
    void keyIncludesDevice();
    void shrinkingRetentionTrimsAndKeepsNewest();
    void keyLeavingRetentionForgetsSamples();
    void clearKeepsRecording();
};

void TestTelemetryHistory::dropsUnretainedKeys() {
    TelemetryHistory history;
    history.append(kKey.deviceId, bindingFor(kKey), 1000, 1.0);
    QVERIFY(history.buffer(kKey) == nullptr);
}

void TestTelemetryHistory::recordsRetainedKeyUpToCapacity() {
    TelemetryHistory history;
    history.setRetention({{kKey, 2}});
    history.append(kKey.deviceId, bindingFor(kKey), 1000, 1.0);
    history.append(kKey.deviceId, bindingFor(kKey, 1), 2000, 2.0);
    history.append(kKey.deviceId, bindingFor(kKey), 3000, 3.0);

    const auto* buffer = history.buffer(kKey);
    QVERIFY(buffer != nullptr);
    QCOMPARE(buffer->values(), (QVector<double>{2.0, 3.0}));
}

void TestTelemetryHistory::keyIncludesDevice() {
    TelemetryHistory history;
    history.setRetention({{kKey, 10}});
    history.append("dev-b", bindingFor(kKey), 1000, 1.0);
    QVERIFY(history.buffer(kKey)->samples().isEmpty());
}

void TestTelemetryHistory::shrinkingRetentionTrimsAndKeepsNewest() {
    TelemetryHistory history;
    history.setRetention({{kKey, 10}});
    for (int i = 0; i < 5; ++i) {
        history.append(kKey.deviceId, bindingFor(kKey), quint64(i), double(i));
    }
    history.setRetention({{kKey, 2}});
    QCOMPARE(history.buffer(kKey)->values(), (QVector<double>{3.0, 4.0}));
}

void TestTelemetryHistory::keyLeavingRetentionForgetsSamples() {
    TelemetryHistory history;
    history.setRetention({{kKey, 10}});
    history.append(kKey.deviceId, bindingFor(kKey), 1000, 1.0);
    history.setRetention({});
    QVERIFY(history.buffer(kKey) == nullptr);
    history.setRetention({{kKey, 10}});
    QVERIFY(history.buffer(kKey)->samples().isEmpty());
}

void TestTelemetryHistory::clearKeepsRecording() {
    TelemetryHistory history;
    history.setRetention({{kKey, 10}});
    history.append(kKey.deviceId, bindingFor(kKey), 1000, 1.0);
    history.clear(kKey);
    QVERIFY(history.buffer(kKey)->samples().isEmpty());
    history.append(kKey.deviceId, bindingFor(kKey), 2000, 2.0);
    QCOMPARE(history.buffer(kKey)->values(), (QVector<double>{2.0}));
}

}  // namespace

QTEST_MAIN(TestTelemetryHistory)
#include "test_telemetryhistory.moc"
