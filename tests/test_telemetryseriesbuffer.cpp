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
    void windowStaysCorrectAcrossManyTrims();
    void valuesTracksAppendsAndClear();
    void valuesIsStableBetweenAppends();
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

void TestTelemetrySeriesBuffer::windowStaysCorrectAcrossManyTrims() {
    // The window has to keep reporting the last kCapacity samples no matter
    // how many have passed through it -- Qt's QList reclaims and reallocates
    // its front free space on its own schedule underneath this, and the
    // observable contents must not depend on when that happens.
    constexpr int kCapacity = 8;
    constexpr int kAppends = kCapacity * 20;

    TelemetrySeriesBuffer buffer;
    buffer.setCapacity(kCapacity);
    for (int i = 0; i < kAppends; ++i) {
        buffer.append(quint64(i), double(i));

        // Checked on every append, not just at the end: a stale window edge
        // would otherwise only be visible on the one iteration that happened
        // to be inspected.
        const int expectedSize = qMin(i + 1, kCapacity);
        QCOMPARE(buffer.samples().size(), expectedSize);
        QCOMPARE(buffer.samples().first().timestampUs, quint64(i + 1 - expectedSize));
        QCOMPARE(buffer.samples().last().timestampUs, quint64(i));
    }

    QVector<double> expected;
    for (int i = kAppends - kCapacity; i < kAppends; ++i) {
        expected.append(double(i));
    }
    QCOMPARE(buffer.values(), expected);
}

void TestTelemetrySeriesBuffer::valuesTracksAppendsAndClear() {
    // values() is cached, so what matters is that every mutation
    // invalidates it -- a chart reading a stale cache would freeze its plot
    // while telemetry kept arriving.
    TelemetrySeriesBuffer buffer;
    buffer.setCapacity(2);

    buffer.append(1, 1.0);
    QCOMPARE(buffer.values(), (QVector<double>{1.0}));

    buffer.append(2, 2.0);
    QCOMPARE(buffer.values(), (QVector<double>{1.0, 2.0}));

    buffer.append(3, 3.0);  // past capacity: oldest leaves the window
    QCOMPARE(buffer.values(), (QVector<double>{2.0, 3.0}));

    buffer.setCapacity(1);
    QCOMPARE(buffer.values(), (QVector<double>{3.0}));

    buffer.clear();
    QVERIFY(buffer.values().isEmpty());
}

void TestTelemetrySeriesBuffer::valuesIsStableBetweenAppends() {
    // A caller may hold the returned vector across a read (a paint frame
    // copies one per series). QVector is implicitly shared, so that copy
    // must keep the values it was given even after the buffer moves on.
    TelemetrySeriesBuffer buffer;
    buffer.setCapacity(2);
    buffer.append(1, 1.0);
    buffer.append(2, 2.0);

    const QVector<double> held = buffer.values();
    buffer.append(3, 3.0);

    QCOMPARE(held, (QVector<double>{1.0, 2.0}));
    QCOMPARE(buffer.values(), (QVector<double>{2.0, 3.0}));
}

} // namespace

QTEST_MAIN(TestTelemetrySeriesBuffer)
#include "test_telemetryseriesbuffer.moc"
