#include <QtTest>

#include "protocol/telemetryfieldrouter.h"

using traceview::registerBallySoftwareCatalog;
using traceview::TelemetryCatalog;
using traceview::TelemetryFieldBinding;
using traceview::TelemetryFieldRouter;
using traceview::TelemetrySample;

namespace {

constexpr quint32 kSourceId = 0x11223344;

// protocol.test payload, matching BTP's canonical
// test-vectors/v1/valid/protocol_test.json: counter=0x01020304,
// value=float32 bits 0x3F0D0A00 -- deliberately contains 0x00, LF (0x0A) and
// CR (0x0D) inside the body, demonstrating those bytes carry no delimiter
// meaning here (PLANO_GERAL.txt decision 6, Marco 2's stated requirement).
TelemetrySample protocolTestSample(quint64 timestampUs) {
    TelemetrySample sample;
    sample.sourceId = kSourceId;
    sample.bootId = 1;
    sample.sequence = 1;
    sample.timestampUs = timestampUs;
    sample.topicId = 0x0001;
    sample.schemaVersion = 1;
    sample.payload = QByteArray::fromHex(
        "04030201"
        "000a0d3f");
    return sample;
}

class TestTelemetryFieldRouter : public QObject {
    Q_OBJECT

private slots:
    void twoSubscribersBothReceiveTheSameField();
    void timestampTravelsUnmodified();
    void unknownSchemaIsCountedAndNotDelivered();
    void malformedPayloadIsCountedAndNotDelivered();
};

void TestTelemetryFieldRouter::twoSubscribersBothReceiveTheSameField() {
    TelemetryCatalog catalog;
    registerBallySoftwareCatalog(catalog, kSourceId);
    TelemetryFieldRouter router(&catalog);

    // CRITERIO DE ACEITE: "dois widgets podem consumir o mesmo campo" --
    // two independent connections to the same signal, each acting as a
    // stand-in for a separate widget subscribing to field 1 (counter).
    QVector<double> subscriberA;
    QVector<double> subscriberB;
    connect(&router, &TelemetryFieldRouter::fieldSample, &router,
            [&](const TelemetryFieldBinding& binding, quint64, double value) {
                if (binding.fieldId == 1)
                    subscriberA.append(value);
            });
    connect(&router, &TelemetryFieldRouter::fieldSample, &router,
            [&](const TelemetryFieldBinding& binding, quint64, double value) {
                if (binding.fieldId == 1)
                    subscriberB.append(value);
            });

    router.onTelemetrySample(protocolTestSample(1000));

    QCOMPARE(subscriberA.size(), 1);
    QCOMPARE(subscriberB.size(), 1);
    QCOMPARE(subscriberA.first(), subscriberB.first());
    QVERIFY(qFuzzyCompare(subscriberA.first(), double(0x01020304)));
}

void TestTelemetryFieldRouter::timestampTravelsUnmodified() {
    TelemetryCatalog catalog;
    registerBallySoftwareCatalog(catalog, kSourceId);
    TelemetryFieldRouter router(&catalog);

    // CRITERIO DE ACEITE: "o timestamp nao e descartado no roteador" -- the
    // origin timestamp_us on the sample must reach the subscriber exactly,
    // never substituted by e.g. local arrival time (model.md section 4 /
    // PLANO_GERAL.txt decision 11).
    quint64 observedTimestamp = 0;
    connect(&router, &TelemetryFieldRouter::fieldSample, &router,
            [&](const TelemetryFieldBinding& binding, quint64 timestampUs, double) {
                if (binding.fieldId == 1)
                    observedTimestamp = timestampUs;
            });

    router.onTelemetrySample(protocolTestSample(0x0102030405060708ULL));

    QCOMPARE(observedTimestamp, quint64(0x0102030405060708ULL));
}

void TestTelemetryFieldRouter::unknownSchemaIsCountedAndNotDelivered() {
    TelemetryCatalog catalog;  // empty -- nothing registered
    TelemetryFieldRouter router(&catalog);

    bool delivered = false;
    connect(&router, &TelemetryFieldRouter::fieldSample, &router,
            [&](const TelemetryFieldBinding&, quint64, double) { delivered = true; });

    router.onTelemetrySample(protocolTestSample(1000));

    QVERIFY(!delivered);
    QCOMPARE(router.diagnostics().schemaUnknown, quint64(1));
    QCOMPARE(router.diagnostics().samplesDecoded, quint64(0));
}

void TestTelemetryFieldRouter::malformedPayloadIsCountedAndNotDelivered() {
    TelemetryCatalog catalog;
    registerBallySoftwareCatalog(catalog, kSourceId);
    TelemetryFieldRouter router(&catalog);

    bool delivered = false;
    connect(&router, &TelemetryFieldRouter::fieldSample, &router,
            [&](const TelemetryFieldBinding&, quint64, double) { delivered = true; });

    TelemetrySample sample = protocolTestSample(1000);
    sample.payload = QByteArray::fromHex("0102");  // far too short for the schema
    router.onTelemetrySample(sample);

    QVERIFY(!delivered);
    QCOMPARE(router.diagnostics().decodeErrors, quint64(1));
}

}  // namespace

QTEST_MAIN(TestTelemetryFieldRouter)
#include "test_telemetryfieldrouter.moc"
