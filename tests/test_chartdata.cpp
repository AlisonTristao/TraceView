#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QPair>

#include "dashboard/widgets/chartdata.h"

using traceview::appendChartSample;
using traceview::ChartByteType;
using traceview::chartBufferCapacity;
using traceview::ChartConfig;
using traceview::ChartPayloadFormat;
using traceview::ChartSeriesStyle;
using traceview::ChartXAxisMode;
using traceview::ChartYAxisMode;
using traceview::decodeChartPayload;
using traceview::parseChartConfig;
using traceview::resizeChartBuffers;

namespace {

QJsonObject seriesJson(int index, const QString& byteType = "float32") {
    QJsonObject series;
    series["index"] = index;
    series["byteType"] = byteType;
    return series;
}

ChartConfig csvConfig(const QVector<int>& indices) {
    QJsonObject json;
    json["format"] = "csv";
    QJsonArray series;
    for (int index : indices) {
        series.append(seriesJson(index));
    }
    json["series"] = series;
    return parseChartConfig(json);
}

// Not named "slots": that identifier collides with Qt's `slots` macro (see
// the comment in chartdata.cpp's decodeChartPayload).
ChartConfig bytesConfig(const QVector<QPair<int, QString>>& byteSlots) {
    QJsonObject json;
    json["format"] = "bytes";
    QJsonArray series;
    for (const auto& slot : byteSlots) {
        series.append(seriesJson(slot.first, slot.second));
    }
    json["series"] = series;
    return parseChartConfig(json);
}

class TestChartData : public QObject {
    Q_OBJECT

private slots:
    void parsesDefaultsFromEmptyConfig();
    void parsesExplicitConfig();

    void bufferCapacityInSamplesMode();
    void bufferCapacityInTimeMode();

    void decodesCsvSlotsByIndex();
    void csvMissingOrMalformedSlotYieldsNaN();

    void decodesBytesSlotsLittleEndianPerType();
    void bytesWrongWidthOrNonHexYieldsNaN();

    void appendSkipsNaNAndTrimsToCapacity();

    void resizeCarriesOverByPositionAndTrims();
};

void TestChartData::parsesDefaultsFromEmptyConfig() {
    const ChartConfig config = parseChartConfig(QJsonObject());

    QCOMPARE(config.format, ChartPayloadFormat::Csv);
    QCOMPARE(config.count, 1);
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
    json["format"] = "bytes";
    json["count"] = 3;

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
    one["index"] = 2;
    one["color"] = "#ff0000";
    one["style"] = "dashed";
    one["byteType"] = "int16";
    QJsonArray series;
    series.append(one);
    json["series"] = series;

    const ChartConfig config = parseChartConfig(json);
    QCOMPARE(config.format, ChartPayloadFormat::Bytes);
    QCOMPARE(config.count, 3);
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
    QCOMPARE(config.series[0].index, 2);
    QCOMPARE(config.series[0].color, QColor("#ff0000"));
    QCOMPARE(config.series[0].style, ChartSeriesStyle::Dashed);
    QCOMPARE(config.series[0].byteType, ChartByteType::Int16);
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

void TestChartData::decodesCsvSlotsByIndex() {
    const ChartConfig config = csvConfig({0, 2, 5});
    const QVector<double> values = decodeChartPayload("1;2.5;-3", config);

    QCOMPARE(values.size(), 3);
    QCOMPARE(values[0], 1.0);
    QCOMPARE(values[1], -3.0);
    QVERIFY(qIsNaN(values[2])); // index 5 doesn't exist in this payload
}

void TestChartData::csvMissingOrMalformedSlotYieldsNaN() {
    const ChartConfig config = csvConfig({0});
    QVERIFY(qIsNaN(decodeChartPayload("abc", config)[0]));
    QVERIFY(qIsNaN(decodeChartPayload("", config)[0]));
}

void TestChartData::decodesBytesSlotsLittleEndianPerType() {
    // Same examples as docs/PROTOCOL.md "Inbound: payload encoding".
    {
        const ChartConfig config = bytesConfig({{0, "int16"}, {1, "int16"}});
        const QVector<double> values = decodeChartPayload("4a3f;00c8", config);
        QCOMPARE(values[0], 16202.0);
        QCOMPARE(values[1], -14336.0);
    }
    {
        const ChartConfig config = bytesConfig({{0, "uint8"}});
        QCOMPARE(decodeChartPayload("ff", config)[0], 255.0);
    }
    {
        const ChartConfig config = bytesConfig({{0, "float32"}});
        const QVector<double> values = decodeChartPayload("0000c03f", config);
        QVERIFY(qFuzzyCompare(values[0], 1.5));
    }
}

void TestChartData::bytesWrongWidthOrNonHexYieldsNaN() {
    const ChartConfig config = bytesConfig({{0, "int16"}});
    QVERIFY(qIsNaN(decodeChartPayload("4a", config)[0]));   // too short for int16 (needs 4 hex chars)
    QVERIFY(qIsNaN(decodeChartPayload("zzzz", config)[0])); // not hex
}

void TestChartData::appendSkipsNaNAndTrimsToCapacity() {
    ChartConfig config = csvConfig({0, 1});
    config.xAxisMode = ChartXAxisMode::Samples;
    config.xLimit = 2;

    QVector<QVector<double>> buffers(2);
    appendChartSample(buffers, config, "1;10");
    appendChartSample(buffers, config, "abc;20"); // slot 0 malformed -> skipped for series 0 only
    appendChartSample(buffers, config, "3;30");
    appendChartSample(buffers, config, "4;40"); // pushes both buffers past capacity 2

    QCOMPARE(buffers[0], (QVector<double>{3.0, 4.0}));
    QCOMPARE(buffers[1], (QVector<double>{30.0, 40.0}));
}

void TestChartData::resizeCarriesOverByPositionAndTrims() {
    ChartConfig config = csvConfig({0, 1, 2});
    config.xLimit = 100;

    const QVector<QVector<double>> previous = {{1.0, 2.0, 3.0}, {9.0}, {}};
    QVector<QVector<double>> resized = resizeChartBuffers(previous, config);
    QCOMPARE(resized.size(), 3);
    QCOMPARE(resized[0], previous[0]);
    QCOMPARE(resized[1], previous[1]);
    QVERIFY(resized[2].isEmpty());

    // Shrinking capacity trims from the front (oldest first).
    config.xLimit = 2;
    resized = resizeChartBuffers(previous, config);
    QCOMPARE(resized[0], (QVector<double>{2.0, 3.0}));
}

} // namespace

QTEST_MAIN(TestChartData)
#include "test_chartdata.moc"
