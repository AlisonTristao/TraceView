#include <QJsonObject>
#include <QtTest>

#include "dashboard/widgets/chartstyle.h"

using traceview::AxisRange;
using traceview::chartStyle;
using traceview::ChartStyleId;
using traceview::chartStyleFromId;
using traceview::chartStyleIdString;
using traceview::ChartConfig;
using traceview::ChartLegendPlacement;
using traceview::ChartLineInterpolation;
using traceview::ChartTickPlacement;
using traceview::ChartViewFeature;
using traceview::ChartViewOptions;
using traceview::chartViewOptionList;
using traceview::chartViewOptionsToJson;
using traceview::effectiveLineWidth;
using traceview::ChartXAxisMode;
using traceview::parseChartViewOptions;
using traceview::seriesDataRange;
using traceview::timeAxisScale;
using traceview::ValueScale;
using traceview::valueScale;
using traceview::WidgetViewOption;
using traceview::withChartViewOption;

class TestChartStyle : public QObject {
    Q_OBJECT

private slots:
    void styleIdsRoundTrip();
    void missingViewKeepsTheOriginalLook();
    void viewOptionsRoundTripThroughJson();
    void pickingAStyleKeepsEveryOtherOption();
    void legendTicksAndLineWidthRoundTrip();
    void outOfMenuValuesSnapToAChoice();
    void yTickCountOverridesTheStyleRule();
    void dashboardKeepsMinMidMaxAndHeadroom();
    void engineeringSnapsDataToTicks();
    void scientificAddsMarginWithoutSnapping();
    void declaredAndFixedRangesAreNeverWidened();
    void dataRangePrefersDeclaredRanges();
    void timeAxisRunsFromOldestToNow();
    void optionListOnlyOffersSupportedFeatures();
    void infoItemsAndMarkersRoundTrip();
};

void TestChartStyle::styleIdsRoundTrip() {
    for (ChartStyleId id : traceview::allChartStyles()) {
        QCOMPARE(chartStyleFromId(chartStyleIdString(id)), id);
        QCOMPARE(chartStyle(id).id, id);
    }
    QCOMPARE(chartStyleFromId("bogus"), ChartStyleId::Dashboard);
}

void TestChartStyle::missingViewKeepsTheOriginalLook() {
    // Every dashboard saved before the gear menu stored anything.
    const ChartViewOptions options = parseChartViewOptions(QJsonObject());
    QVERIFY(options.followAppStyle);  // View > Chart Style decides
    QCOMPARE(options.style, ChartStyleId::Dashboard);
    QVERIFY(!options.showXAxisTitle);
    QVERIFY(options.showYAxisTitle);
    QVERIFY(!options.showXTickLabels);
    QVERIFY(options.showYTickLabels);
    QVERIFY(!options.showScaleLabels);
    QVERIFY(options.showLastValueRow);
    QVERIFY(!options.showGridPointMarkers);
    QVERIFY(!options.showHoverCrosshair);
    QCOMPARE(options.interpolation, ChartLineInterpolation::Linear);

    // A view that only names a style takes that style's axis defaults.
    QJsonObject view;
    view["style"] = "scientific";
    const ChartViewOptions scientific = parseChartViewOptions(view);
    QVERIFY(!scientific.followAppStyle);
    QVERIFY(scientific.showXAxisTitle);
    QVERIFY(scientific.showXTickLabels);
    QVERIFY(scientific.showScaleLabels);
}

void TestChartStyle::viewOptionsRoundTripThroughJson() {
    ChartViewOptions options;
    options.followAppStyle = false;
    options.style = ChartStyleId::Engineering;
    options.showXAxisTitle = false;
    options.showYTickLabels = false;
    options.showHoverCrosshair = true;
    options.interpolation = ChartLineInterpolation::ZeroOrderHold;
    const ChartViewOptions parsed = parseChartViewOptions(chartViewOptionsToJson(options));
    QVERIFY(!parsed.followAppStyle);
    QCOMPARE(parsed.style, ChartStyleId::Engineering);
    QVERIFY(!parsed.showXAxisTitle);

    // "App default" survives the round trip as its own value.
    const ChartViewOptions app = withChartViewOption(options, "style", "app");
    QVERIFY(app.followAppStyle);
    QCOMPARE(chartViewOptionsToJson(app).value("style").toString(), QString("app"));
    QVERIFY(parseChartViewOptions(chartViewOptionsToJson(app)).followAppStyle);
    QVERIFY(!parsed.showYTickLabels);
    QVERIFY(parsed.showHoverCrosshair);
    QCOMPARE(parsed.interpolation, ChartLineInterpolation::ZeroOrderHold);
}

void TestChartStyle::pickingAStyleKeepsEveryOtherOption() {
    // Like switching the app theme: the style changes how things are
    // drawn, never which things are shown.
    ChartViewOptions options;
    options.showYTickLabels = false;
    options.showXTickLabels = false;
    options.showHoverCrosshair = true;
    options.legendPlacement = ChartLegendPlacement::TopRight;
    options = withChartViewOption(options, "style", "engineering");
    QVERIFY(!options.followAppStyle);
    QCOMPARE(options.style, ChartStyleId::Engineering);
    QVERIFY(!options.showYTickLabels);
    QVERIFY(!options.showXTickLabels);
    QVERIFY(options.showHoverCrosshair);
    QCOMPARE(options.legendPlacement, ChartLegendPlacement::TopRight);

    options = withChartViewOption(options, "xTickLabels", true);
    QVERIFY(options.showXTickLabels);
    options = withChartViewOption(options, "interpolation", "stem");
    QCOMPARE(options.interpolation, ChartLineInterpolation::Stem);
}

void TestChartStyle::legendTicksAndLineWidthRoundTrip() {
    ChartViewOptions options;
    options = withChartViewOption(options, "legend", "bottomLeft");
    options = withChartViewOption(options, "legendOpacity", "25");
    options = withChartViewOption(options, "yTicks", "11");
    options = withChartViewOption(options, "lineWidth", "0.5");
    const ChartViewOptions parsed = parseChartViewOptions(chartViewOptionsToJson(options));
    QCOMPARE(parsed.legendPlacement, ChartLegendPlacement::BottomLeft);
    QCOMPARE(parsed.legendOpacity, 25);
    QCOMPARE(parsed.yTickCount, 11);
    QCOMPARE(parsed.lineWidth, 0.5);

    // 0 = Auto: the style's own stroke.
    const auto& scientific = chartStyle(ChartStyleId::Scientific);
    QCOMPARE(effectiveLineWidth(ChartViewOptions(), scientific), scientific.lineWidth);
    QCOMPARE(effectiveLineWidth(parsed, scientific), 0.5);
}

void TestChartStyle::outOfMenuValuesSnapToAChoice() {
    QJsonObject view;
    view["legendOpacity"] = 60;
    view["yTicks"] = 4;
    view["lineWidth"] = 9.0;
    view["legend"] = "middle";
    const ChartViewOptions options = parseChartViewOptions(view);
    QCOMPARE(options.legendOpacity, 50);
    QCOMPARE(options.yTickCount, 3);  // 3 and 5 tie; the first wins
    QCOMPARE(options.lineWidth, 4.0);
    QCOMPARE(options.legendPlacement, ChartLegendPlacement::Outside);
}

void TestChartStyle::yTickCountOverridesTheStyleRule() {
    const AxisRange range{0.0, 100.0, AxisRange::Source::Fixed};
    // Dashboard: every division labeled, not just min/mid/max.
    const ValueScale divisions =
        valueScale(range, chartStyle(ChartStyleId::Dashboard), 6, 2, 0, /*tickCount=*/5);
    QCOMPARE(divisions.ticks, (QVector<double>{0, 25, 50, 75, 100}));
    QCOMPARE(divisions.gridTicks, divisions.ticks);

    // Closer ticks get enough decimals to tell neighbors apart.
    const ValueScale unit = valueScale({-1.0, 1.0, AxisRange::Source::Fixed},
                                       chartStyle(ChartStyleId::Dashboard), 6, 2, 0, 5);
    QCOMPARE(unit.decimals, 1);

    // Nice: the count is the tick budget.
    const ValueScale nice =
        valueScale(range, chartStyle(ChartStyleId::Scientific), 3, 2, 0, /*tickCount=*/11);
    QCOMPARE(nice.ticks.size(), 11);
}

void TestChartStyle::dashboardKeepsMinMidMaxAndHeadroom() {
    const auto& style = chartStyle(ChartStyleId::Dashboard);
    const ValueScale scale = valueScale({0.0, 100.0, AxisRange::Source::Data}, style, 6, 2, 1);
    QCOMPARE(scale.lo, -5.0);
    QCOMPARE(scale.hi, 105.0);
    QCOMPARE(scale.ticks, (QVector<double>{-5.0, 50.0, 105.0}));
    QCOMPARE(scale.gridTicks.size(), 3);
    QCOMPARE(scale.decimals, 1);  // the chart's own decimals

    const ValueScale flat = valueScale({3.0, 3.0, AxisRange::Source::Data}, style, 6, 2, 0);
    QCOMPARE(flat.lo, 2.0);
    QCOMPARE(flat.hi, 4.0);

    const ValueScale empty = valueScale({}, style, 6, 10, 0);
    QCOMPARE(empty.lo, 0.0);
    QCOMPARE(empty.hi, 1.0);
    QCOMPARE(empty.gridTicks.size(), 11);
}

void TestChartStyle::engineeringSnapsDataToTicks() {
    const auto& style = chartStyle(ChartStyleId::Engineering);
    const ValueScale scale = valueScale({0.37, 99.25, AxisRange::Source::Data}, style, 6, 2, 3);
    QCOMPARE(scale.lo, 0.0);
    QCOMPARE(scale.hi, 100.0);
    QCOMPARE(scale.ticks, (QVector<double>{0, 20, 40, 60, 80, 100}));
    QCOMPARE(scale.gridTicks, scale.ticks);
    QCOMPARE(scale.decimals, 0);  // from the step, not the chart's 3
}

void TestChartStyle::scientificAddsMarginWithoutSnapping() {
    const auto& style = chartStyle(ChartStyleId::Scientific);
    const ValueScale scale = valueScale({0.0, 100.0, AxisRange::Source::Data}, style, 6, 2, 0);
    QCOMPARE(scale.lo, -5.0);
    QCOMPARE(scale.hi, 105.0);
    QCOMPARE(scale.ticks, (QVector<double>{0, 20, 40, 60, 80, 100}));
}

void TestChartStyle::declaredAndFixedRangesAreNeverWidened() {
    for (ChartStyleId id : traceview::allChartStyles()) {
        const auto& style = chartStyle(id);
        for (auto source : {AxisRange::Source::Declared, AxisRange::Source::Fixed}) {
            const ValueScale scale = valueScale({0.0, 4095.0, source}, style, 6, 2, 0);
            QCOMPARE(scale.lo, 0.0);
            QCOMPARE(scale.hi, 4095.0);
            for (double tick : scale.ticks) {
                QVERIFY(tick >= 0.0 && tick <= 4095.0);
            }
        }
    }
}

void TestChartStyle::dataRangePrefersDeclaredRanges() {
    const QVector<QVector<double>> buffers = {{1.0, 5.0}, {-2.0}};
    const AxisRange scanned = seriesDataRange(buffers, {}, {});
    QCOMPARE(scanned.source, AxisRange::Source::Data);
    QCOMPARE(scanned.lo, -2.0);
    QCOMPARE(scanned.hi, 5.0);

    const AxisRange declared = seriesDataRange(buffers, {0.0, -10.0}, {10.0, 3.0});
    QCOMPARE(declared.source, AxisRange::Source::Declared);
    QCOMPARE(declared.lo, -10.0);
    QCOMPARE(declared.hi, 10.0);

    // One series without a declared range falls back to the scan.
    const AxisRange partial = seriesDataRange(buffers, {0.0, qQNaN()}, {10.0, qQNaN()});
    QCOMPARE(partial.source, AxisRange::Source::Data);

    QCOMPARE(seriesDataRange({{}, {}}, {}, {}).source, AxisRange::Source::Empty);
}

void TestChartStyle::timeAxisRunsFromOldestToNow() {
    ChartConfig config;
    config.xAxisMode = ChartXAxisMode::Samples;
    config.xLimit = 101;
    const auto& scientific = chartStyle(ChartStyleId::Scientific);
    const ValueScale samples = timeAxisScale(config, scientific, true, 400, 6);
    QCOMPARE(samples.lo, -100.0);
    QCOMPARE(samples.hi, 0.0);
    QCOMPARE(samples.ticks, (QVector<double>{-100, -80, -60, -40, -20, 0}));

    // Dashboard without labels keeps the plain pixel-spaced grid, no ticks.
    const ValueScale plain = timeAxisScale(config, chartStyle(ChartStyleId::Dashboard), false,
                                           400, 6);
    QVERIFY(plain.ticks.isEmpty());
    QCOMPARE(plain.gridTicks.size(), 5);  // 400px / 60px -> 6 bands, 5 inner lines

    // ...but switches to round ticks as soon as they get labels.
    const ValueScale labeled =
        timeAxisScale(config, chartStyle(ChartStyleId::Dashboard), true, 400, 6);
    QVERIFY(!labeled.ticks.isEmpty());

    config.xAxisMode = ChartXAxisMode::Time;
    config.sampleTimeMs = 100.0;
    config.xLimit = 10;  // seconds
    const ValueScale time = timeAxisScale(config, scientific, true, 400, 6);
    QVERIFY(time.lo < -9.0 && time.lo >= -10.0);
    QCOMPARE(time.hi, 0.0);
}

void TestChartStyle::optionListOnlyOffersSupportedFeatures() {
    const ChartViewOptions options;
    const QVector<WidgetViewOption> gauge =
        chartViewOptionList(options, ChartViewFeature::ScaleLabels);
    QCOMPARE(gauge.size(), 2);
    QCOMPARE(gauge[0].id, QString("style"));
    QCOMPARE(gauge[0].kind, WidgetViewOption::Kind::Choice);
    QCOMPARE(gauge[0].choices.size(), 4);  // App default + the three styles
    QCOMPARE(gauge[0].choices.first().first, QString("app"));
    QCOMPARE(gauge[0].value.toString(), QString("app"));
    QCOMPARE(gauge[1].id, QString("scaleLabels"));
    QVERIFY(gauge[1].startsSection);

    const QVector<WidgetViewOption> line = chartViewOptionList(
        options, ChartViewFeature::XAxisTitle | ChartViewFeature::LastValue |
                     ChartViewFeature::Interpolation);
    QCOMPARE(line.size(), 4);
    QCOMPARE(line.last().id, QString("interpolation"));
    QVERIFY(line[1].startsSection);  // xAxisTitle opens the axis group
    QVERIFY(line[2].startsSection);  // lastValue opens the overlay group
    QVERIFY(line[3].startsSection);
}

void TestChartStyle::infoItemsAndMarkersRoundTrip() {
    // Defaults: the window readouts, no markers.
    const ChartViewOptions defaults = parseChartViewOptions(QJsonObject());
    QCOMPARE(defaults.infoItems,
             QStringList({QStringLiteral("rate"), QStringLiteral("samples"),
                          QStringLiteral("span")}));
    QVERIFY(!defaults.rangeMarkers);

    // Picked out of order and with an unknown id: kept in menu order, the
    // unknown one dropped.
    ChartViewOptions options = withChartViewOption(
        defaults, QStringLiteral("infoItems"),
        QStringList({QStringLiteral("rms"), QStringLiteral("bogus"), QStringLiteral("min")}));
    options = withChartViewOption(options, QStringLiteral("markers"), true);
    QCOMPARE(options.infoItems, QStringList({QStringLiteral("min"), QStringLiteral("rms")}));

    const ChartViewOptions back = parseChartViewOptions(chartViewOptionsToJson(options));
    QCOMPARE(back.infoItems, options.infoItems);
    QVERIFY(back.rangeMarkers);

    // Offered as one multi-choice entry, only with the feature.
    const QVector<WidgetViewOption> list =
        chartViewOptionList(options, ChartViewFeature::InfoRow | ChartViewFeature::InfoItems);
    bool found = false;
    for (const WidgetViewOption& option : list) {
        if (option.id == QLatin1String("infoItems")) {
            found = true;
            QCOMPARE(option.kind, WidgetViewOption::Kind::MultiChoice);
            QCOMPARE(option.value.toStringList(), options.infoItems);
            QCOMPARE(option.choices.size(), 10);
        }
    }
    QVERIFY(found);
}

QTEST_MAIN(TestChartStyle)
#include "test_chartstyle.moc"
