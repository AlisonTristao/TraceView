#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "dashboard/widgets/chartdata.h"

using traceview::appendFieldSample;
using traceview::chartBufferCapacity;
using traceview::ChartConfig;
using traceview::ChartSeriesStyle;
using traceview::ChartXAxisMode;
using traceview::ChartYAxisMode;
using traceview::GaugeConfig;
using traceview::parseChartConfig;
using traceview::parseGaugeConfig;
using traceview::resizeChartBuffers;
using traceview::TelemetrySeriesBuffer;

namespace {

QJsonObject seriesJson(int fieldId, const QString& style = "solid") {
    QJsonObject series;
    series["fieldId"] = fieldId;
    series["style"] = style;
    return series;
}

ChartConfig configWithFields(const QVector<int>& fieldIds) {
    QJsonObject json;
    QJsonArray series;
    for (int fieldId : fieldIds) {
        series.append(seriesJson(fieldId));
    }
    json["series"] = series;
    return parseChartConfig(json);
}

class TestChartData : public QObject {
    Q_OBJECT

private slots:
    void parsesDefaultsFromEmptyConfig();
    void parsesExplicitConfig();
    void parsesSourceAndTopicIdsFromHexOrDecimalStrings();

    void bufferCapacityInSamplesMode();
    void bufferCapacityInTimeMode();

    void appendFieldSampleRoutesByFieldIdAndTrimsToCapacity();
    void appendFieldSampleIgnoresUnboundFieldId();
    void appendFieldSampleFeedsMultipleSeriesWithSameFieldId();

    void resizeCarriesOverByPositionAndTrims();

    void gaugeConfigParsesSeries();
    void gaugeConfigMigratesLegacyFieldId();
    void gaugeConfigDefaultsToOneSeriesWhenUnconfigured();
    void gaugeConfigEmptySeriesArrayStaysEmpty();
};

void TestChartData::parsesDefaultsFromEmptyConfig() {
    const ChartConfig config = parseChartConfig(QJsonObject());

    QCOMPARE(config.sourceId, quint32(0));
    QCOMPARE(config.topicId, quint16(0));
    QCOMPARE(config.xAxisMode, ChartXAxisMode::Samples);
    QCOMPARE(config.sampleTimeMs, 100.0);
    QCOMPARE(config.xLimit, 500);
    QCOMPARE(config.yAxisMode, ChartYAxisMode::Auto);
    QCOMPARE(config.yMin, 0.0);
    QCOMPARE(config.yMax, 100.0);
    QCOMPARE(config.showGrid, true);
    QVERIFY(config.series.isEmpty());
}

void TestChartData::parsesExplicitConfig() {
    QJsonObject json;
    json["sourceId"] = "0x11223344";
    json["topicId"] = "0x0101";

    QJsonObject xAxis;
    xAxis["mode"] = "time";
    xAxis["sampleTimeMs"] = 50.0;
    xAxis["limit"] = 10;
    json["xAxis"] = xAxis;

    QJsonObject yAxis;
    yAxis["mode"] = "fixed";
    yAxis["min"] = -5.0;
    yAxis["max"] = 5.0;
    yAxis["unit"] = "V";
    yAxis["grid"] = false;
    json["yAxis"] = yAxis;

    QJsonObject one;
    one["name"] = "Accel X";
    one["fieldId"] = 3;
    one["color"] = "#ff0000";
    one["style"] = "dashed";
    QJsonArray series;
    series.append(one);
    json["series"] = series;

    const ChartConfig config = parseChartConfig(json);
    QCOMPARE(config.sourceId, quint32(0x11223344));
    QCOMPARE(config.topicId, quint16(0x0101));
    QCOMPARE(config.xAxisMode, ChartXAxisMode::Time);
    QCOMPARE(config.sampleTimeMs, 50.0);
    QCOMPARE(config.xLimit, 10);
    QCOMPARE(config.yAxisMode, ChartYAxisMode::Fixed);
    QCOMPARE(config.yMin, -5.0);
    QCOMPARE(config.yMax, 5.0);
    QCOMPARE(config.yUnit, QStringLiteral("V"));
    QCOMPARE(config.showGrid, false);
    QCOMPARE(config.series.size(), 1);
    QCOMPARE(config.series[0].name, QStringLiteral("Accel X"));
    QCOMPARE(config.series[0].fieldId, quint16(3));
    QCOMPARE(config.series[0].color, QColor("#ff0000"));
    QCOMPARE(config.series[0].style, ChartSeriesStyle::Dashed);
}

void TestChartData::parsesSourceAndTopicIdsFromHexOrDecimalStrings() {
    QJsonObject json;
    json["sourceId"] = "287454020";  // decimal for 0x11223344
    json["topicId"] = "257";         // decimal for 0x0101
    const ChartConfig config = parseChartConfig(json);
    QCOMPARE(config.sourceId, quint32(0x11223344));
    QCOMPARE(config.topicId, quint16(0x0101));
}

void TestChartData::bufferCapacityInSamplesMode() {
    ChartConfig config;
    config.xAxisMode = ChartXAxisMode::Samples;
    config.xLimit = 250;
    QCOMPARE(chartBufferCapacity(config), 250);
}

void TestChartData::bufferCapacityInTimeMode() {
    ChartConfig config;
    config.xAxisMode = ChartXAxisMode::Time;
    config.xLimit = 2; // seconds
    config.sampleTimeMs = 100.0;
    QCOMPARE(chartBufferCapacity(config), 20); // 2000ms / 100ms

    config.xLimit = 1;
    config.sampleTimeMs = 3.0;
    QCOMPARE(chartBufferCapacity(config), 334); // ceil(1000/3)
}

void TestChartData::appendFieldSampleRoutesByFieldIdAndTrimsToCapacity() {
    ChartConfig config = configWithFields({1, 2});
    config.xAxisMode = ChartXAxisMode::Samples;
    config.xLimit = 2;

    QVector<TelemetrySeriesBuffer> buffers(2);
    for (TelemetrySeriesBuffer& buffer : buffers) buffer.setCapacity(chartBufferCapacity(config));

    appendFieldSample(buffers, config, 1, 100, 1.0);
    appendFieldSample(buffers, config, 2, 100, 10.0);
    appendFieldSample(buffers, config, 1, 200, 3.0);
    appendFieldSample(buffers, config, 1, 300, 4.0);  // pushes series 0 past capacity 2

    QCOMPARE(buffers[0].values(), (QVector<double>{3.0, 4.0}));
    QCOMPARE(buffers[1].values(), (QVector<double>{10.0}));
    QCOMPARE(buffers[0].samples().first().timestampUs, quint64(200));
}

void TestChartData::appendFieldSampleIgnoresUnboundFieldId() {
    ChartConfig config = configWithFields({1});
    QVector<TelemetrySeriesBuffer> buffers(1);

    appendFieldSample(buffers, config, 99, 100, 42.0);  // no series bound to field 99

    QVERIFY(buffers[0].values().isEmpty());
}

void TestChartData::appendFieldSampleFeedsMultipleSeriesWithSameFieldId() {
    // Nothing stops two series (e.g. differently styled) from binding the
    // same field id -- both must receive the sample.
    ChartConfig config = configWithFields({5, 5});
    QVector<TelemetrySeriesBuffer> buffers(2);

    appendFieldSample(buffers, config, 5, 100, 7.0);

    QCOMPARE(buffers[0].values(), (QVector<double>{7.0}));
    QCOMPARE(buffers[1].values(), (QVector<double>{7.0}));
}

void TestChartData::resizeCarriesOverByPositionAndTrims() {
    ChartConfig config = configWithFields({1, 2, 3});
    config.xLimit = 100;

    QVector<TelemetrySeriesBuffer> previous(3);
    previous[0].append(1, 1.0);
    previous[0].append(2, 2.0);
    previous[0].append(3, 3.0);
    previous[1].append(1, 9.0);
    // previous[2] stays empty

    QVector<TelemetrySeriesBuffer> resized = resizeChartBuffers(previous, config);
    QCOMPARE(resized.size(), 3);
    QCOMPARE(resized[0].values(), (QVector<double>{1.0, 2.0, 3.0}));
    QCOMPARE(resized[1].values(), (QVector<double>{9.0}));
    QVERIFY(resized[2].values().isEmpty());

    // Shrinking capacity trims from the front (oldest first).
    config.xLimit = 2;
    resized = resizeChartBuffers(previous, config);
    QCOMPARE(resized[0].values(), (QVector<double>{2.0, 3.0}));
}

void TestChartData::gaugeConfigParsesSeries() {
    QJsonObject json;
    json["sourceId"] = "0x11223344";
    json["topicId"] = "0x0001";
    json["min"] = -1.0;
    json["max"] = 1.0;
    json["unit"] = "g";
    json["decimals"] = 3;

    QJsonArray series;
    series.append(seriesJson(2));
    QJsonObject ring2;
    ring2["name"] = "Y";
    ring2["fieldId"] = 3;
    ring2["color"] = "#22C55E";
    series.append(ring2);
    json["series"] = series;

    const GaugeConfig config = parseGaugeConfig(json);
    QCOMPARE(config.sourceId, quint32(0x11223344));
    QCOMPARE(config.topicId, quint16(0x0001));
    QCOMPARE(config.min, -1.0);
    QCOMPARE(config.max, 1.0);
    QCOMPARE(config.unit, QStringLiteral("g"));
    QCOMPARE(config.decimals, 3);
    QCOMPARE(config.series.size(), 2);
    QCOMPARE(config.series[0].fieldId, quint16(2));
    QCOMPARE(config.series[1].fieldId, quint16(3));
    QCOMPARE(config.series[1].name, QStringLiteral("Y"));
    QCOMPARE(config.series[1].color, QColor("#22C55E"));
}

void TestChartData::gaugeConfigMigratesLegacyFieldId() {
    // Pre-multi-ring save: a bare top-level fieldId instead of a series
    // array. Must still load as a single ring so old saved dashboards don't
    // go blank.
    QJsonObject json;
    json["fieldId"] = 5;

    const GaugeConfig config = parseGaugeConfig(json);
    QCOMPARE(config.series.size(), 1);
    QCOMPARE(config.series[0].fieldId, quint16(5));
}

void TestChartData::gaugeConfigDefaultsToOneSeriesWhenUnconfigured() {
    // A freshly-added gauge (config still the default empty object) gets one
    // placeholder ring instead of rendering blank.
    const GaugeConfig config = parseGaugeConfig(QJsonObject());
    QCOMPARE(config.series.size(), 1);
}

void TestChartData::gaugeConfigEmptySeriesArrayStaysEmpty() {
    // Distinct from the "never configured" case above: an explicit empty
    // array (e.g. the user removed every ring in the editor) must stay
    // empty, not silently regain a placeholder ring.
    QJsonObject json;
    json["series"] = QJsonArray();

    const GaugeConfig config = parseGaugeConfig(json);
    QVERIFY(config.series.isEmpty());
}

} // namespace

QTEST_MAIN(TestChartData)
#include "test_chartdata.moc"
