#include <QtTest>
#include <cmath>
#include <limits>

#include "dashboard/widgets/chartscale.h"

using traceview::formatTick;
using traceview::maxTicksFor;
using traceview::NiceScale;
using traceview::niceScale;
using traceview::tickDecimals;

class TestChartScale : public QObject {
    Q_OBJECT

private slots:
    void picksRoundTicksInsideTheRange();
    void expandSnapsTheRangeToTheOuterTicks();
    void neverExceedsTheTickBudget();
    void usesTwoAndAHalfSteps();
    void handlesNegativeAndSymmetricRanges();
    void widensADegenerateRange();
    void survivesNonFiniteInput();
    void tickDecimalsMatchTheStep();
    void formatTickFoldsNegativeZero();
    void maxTicksForClampsToReadableRange();
};

void TestChartScale::picksRoundTicksInsideTheRange() {
    // The raw auto-range that used to be printed as 0.37 / 49.81 / 99.25.
    const NiceScale scale = niceScale(0.37, 99.25, 6, /*expand=*/false);
    QCOMPARE(scale.step, 20.0);
    QCOMPARE(scale.ticks, (QVector<double>{20, 40, 60, 80}));
    QCOMPARE(scale.lo, 0.37);  // not widened
    QCOMPARE(scale.hi, 99.25);
    QCOMPARE(scale.decimals, 0);
}

void TestChartScale::expandSnapsTheRangeToTheOuterTicks() {
    const NiceScale scale = niceScale(0.37, 99.25, 6, /*expand=*/true);
    QCOMPARE(scale.lo, 0.0);
    QCOMPARE(scale.hi, 100.0);
    QCOMPARE(scale.ticks, (QVector<double>{0, 20, 40, 60, 80, 100}));
}

void TestChartScale::neverExceedsTheTickBudget() {
    const double ranges[][2] = {{0, 1}, {-3.3, 7.9}, {0.001, 0.0093}, {-1e6, 3e6}, {12, 13}};
    for (const auto& range : ranges) {
        for (int maxTicks = 2; maxTicks <= 11; ++maxTicks) {
            for (bool expand : {false, true}) {
                const NiceScale scale = niceScale(range[0], range[1], maxTicks, expand);
                QVERIFY2(!scale.ticks.isEmpty(), "at least one tick");
                QVERIFY2(scale.ticks.size() <= maxTicks,
                         qPrintable(QStringLiteral("%1..%2 max %3 gave %4")
                                        .arg(range[0])
                                        .arg(range[1])
                                        .arg(maxTicks)
                                        .arg(scale.ticks.size())));
                QVERIFY(scale.hi > scale.lo);
                if (expand) {
                    QVERIFY(scale.lo <= range[0]);
                    QVERIFY(scale.hi >= range[1]);
                }
            }
        }
    }
}

void TestChartScale::usesTwoAndAHalfSteps() {
    const NiceScale scale = niceScale(0.0, 10.0, 5, /*expand=*/false);
    QCOMPARE(scale.step, 2.5);
    QCOMPARE(scale.ticks, (QVector<double>{0, 2.5, 5, 7.5, 10}));
    QCOMPARE(scale.decimals, 1);
}

void TestChartScale::handlesNegativeAndSymmetricRanges() {
    const NiceScale scale = niceScale(-1.0, 1.0, 5, /*expand=*/true);
    QCOMPARE(scale.ticks, (QVector<double>{-1, -0.5, 0, 0.5, 1}));
    // Zero must be exactly zero, not a -1e-17 rounding leftover.
    QCOMPARE(scale.ticks[2], 0.0);
    QVERIFY(!std::signbit(scale.ticks[2]));
}

void TestChartScale::widensADegenerateRange() {
    const NiceScale flatZero = niceScale(0.0, 0.0, 5, true);
    QVERIFY(flatZero.lo < 0.0 && flatZero.hi > 0.0);
    const NiceScale flat = niceScale(50.0, 50.0, 5, true);
    QVERIFY(flat.lo < 50.0 && flat.hi > 50.0);
    QVERIFY(!flat.ticks.isEmpty());
    const NiceScale reversed = niceScale(10.0, 0.0, 6, true);
    QCOMPARE(reversed.lo, 0.0);
    QCOMPARE(reversed.hi, 10.0);
}

void TestChartScale::survivesNonFiniteInput() {
    const double inf = std::numeric_limits<double>::infinity();
    const NiceScale scale = niceScale(qQNaN(), inf, 5, true);
    QVERIFY(std::isfinite(scale.lo));
    QVERIFY(std::isfinite(scale.hi));
    QVERIFY(scale.hi > scale.lo);
}

void TestChartScale::tickDecimalsMatchTheStep() {
    QCOMPARE(tickDecimals(20.0), 0);
    QCOMPARE(tickDecimals(1.0), 0);
    QCOMPARE(tickDecimals(2.5), 1);
    QCOMPARE(tickDecimals(0.5), 1);
    QCOMPARE(tickDecimals(0.25), 2);
    QCOMPARE(tickDecimals(0.002), 3);
    QCOMPARE(tickDecimals(0.1 + 0.2), 1);  // 0.30000000000000004
}

void TestChartScale::formatTickFoldsNegativeZero() {
    QCOMPARE(formatTick(-0.0, 0), QString("0"));
    QCOMPARE(formatTick(-1e-12, 2), QString("0.00"));
    QCOMPARE(formatTick(-0.5, 1), QString("-0.5"));
    QCOMPARE(formatTick(7.5, 1), QString("7.5"));
}

void TestChartScale::maxTicksForClampsToReadableRange() {
    QCOMPARE(maxTicksFor(0, 40), 2);
    QCOMPARE(maxTicksFor(100, 0), 2);
    QCOMPARE(maxTicksFor(200, 40), 6);
    QCOMPARE(maxTicksFor(5000, 40), 11);
}

QTEST_MAIN(TestChartScale)
#include "test_chartscale.moc"
