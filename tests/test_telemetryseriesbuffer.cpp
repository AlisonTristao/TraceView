#include <QtTest>

#include "telemetry/telemetryseriesbuffer.h"

using traceview::TelemetrySeriesBuffer;

namespace {

class TestTelemetrySeriesBuffer : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty();
    void appendKeepsTimestampValuePairs();
    void unboundedByDefault();
    void trimsFromFrontWhenOverCapacity();
    void shrinkingCapacityTrimsImmediately();
    void clearEmptiesBuffer();
};

void TestTelemetrySeriesBuffer::startsEmpty() {
    TelemetrySeriesBuffer buffer;
    QVERIFY(buffer.samples().isEmpty());
    QVERIFY(buffer.values().isEmpty());
    QCOMPARE(buffer.capacity(), 0);
}

void TestTelemetrySeriesBuffer::appendKeepsTimestampValuePairs() {
    TelemetrySeriesBuffer buffer;
    buffer.append(1000, 1.5);
    buffer.append(2000, 2.5);

    QCOMPARE(buffer.samples().size(), 2);
    QCOMPARE(buffer.samples()[0].timestampUs, quint64(1000));
    QCOMPARE(buffer.samples()[0].value, 1.5);
    QCOMPARE(buffer.samples()[1].timestampUs, quint64(2000));
    QCOMPARE(buffer.samples()[1].value, 2.5);
    QCOMPARE(buffer.values(), (QVector<double>{1.5, 2.5}));
}

void TestTelemetrySeriesBuffer::unboundedByDefault() {
    TelemetrySeriesBuffer buffer;
    for (int i = 0; i < 500; ++i) {
        buffer.append(quint64(i), double(i));
    }
    QCOMPARE(buffer.samples().size(), 500);
}

void TestTelemetrySeriesBuffer::trimsFromFrontWhenOverCapacity() {
    TelemetrySeriesBuffer buffer;
    buffer.setCapacity(2);
    buffer.append(1, 1.0);
    buffer.append(2, 2.0);
    buffer.append(3, 3.0);

    QCOMPARE(buffer.values(), (QVector<double>{2.0, 3.0}));
    QCOMPARE(buffer.samples().first().timestampUs, quint64(2));
}

void TestTelemetrySeriesBuffer::shrinkingCapacityTrimsImmediately() {
    TelemetrySeriesBuffer buffer;
    buffer.append(1, 1.0);
    buffer.append(2, 2.0);
    buffer.append(3, 3.0);

    buffer.setCapacity(1);
    QCOMPARE(buffer.values(), (QVector<double>{3.0}));
}

void TestTelemetrySeriesBuffer::clearEmptiesBuffer() {
    TelemetrySeriesBuffer buffer;
    buffer.append(1, 1.0);
    buffer.clear();
    QVERIFY(buffer.samples().isEmpty());
}

} // namespace

QTEST_MAIN(TestTelemetrySeriesBuffer)
#include "test_telemetryseriesbuffer.moc"
