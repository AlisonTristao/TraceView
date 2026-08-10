// Standalone visual harness for the chart widgets -- NOT a Qt Test. It
// injects synthetic samples straight into appendFieldSample() on a timer so
// the line/bar/gauge designs can be eyeballed without a serial device or
// the BtpSession/ProtocolRouter/TelemetryFieldRouter plumbing (see
// lib/protocol). See tests/test_chartdata.cpp for the actual config/buffer
// unit tests.

#include <QApplication>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTimer>
#include <QWidget>
#include <QtMath>

#include "dashboard/widgets/chartwidgets.h"

using namespace traceview;

namespace {

constexpr quint32 kSourceId = 0x00000001;
constexpr quint16 kLineTopicId = 0x0001;
constexpr quint16 kBarTopicId = 0x0002;
constexpr quint16 kGaugeTopicId = 0x0001;
constexpr quint16 kGaugeFieldId = 2;  // "value" of protocol.test, see TELEMETRY.md 9.4

QJsonObject seriesJson(const QString& name, int fieldId, const QString& color, const QString& style) {
    QJsonObject series;
    series["name"] = name;
    series["fieldId"] = fieldId;
    series["color"] = color;
    series["style"] = style;
    return series;
}

QJsonObject lineChartConfig() {
    QJsonObject config;
    config["sourceId"] = QString::number(kSourceId);
    config["topicId"] = QString::number(kLineTopicId);

    QJsonObject xAxis;
    xAxis["mode"] = "samples";
    xAxis["limit"] = 150;
    config["xAxis"] = xAxis;

    QJsonObject yAxis;
    yAxis["mode"] = "fixed";
    yAxis["min"] = 0.0;
    yAxis["max"] = 100.0;
    yAxis["unit"] = "V";
    yAxis["grid"] = true;
    config["yAxis"] = yAxis;

    QJsonArray series;
    series.append(seriesJson("Temp", 1, "#3B82F6", "solid"));
    series.append(seriesJson("Pressure", 2, "#F97316", "dashed"));
    series.append(seriesJson("Humidity", 3, "#22C55E", "cross"));
    config["series"] = series;
    return config;
}

QJsonObject barChartConfig() {
    QJsonObject config;
    config["sourceId"] = QString::number(kSourceId);
    config["topicId"] = QString::number(kBarTopicId);

    QJsonObject xAxis;
    xAxis["mode"] = "samples";
    xAxis["limit"] = 16;
    config["xAxis"] = xAxis;

    QJsonObject yAxis;
    yAxis["mode"] = "fixed";
    yAxis["min"] = 0.0;
    yAxis["max"] = 100.0;
    yAxis["unit"] = "%";
    yAxis["grid"] = true;
    config["yAxis"] = yAxis;

    QJsonArray series;
    series.append(seriesJson("A", 1, "#A855F7", "solid"));
    series.append(seriesJson("B", 2, "#EAB308", "solid"));
    config["series"] = series;
    return config;
}

QJsonObject gaugeConfig() {
    QJsonObject config;
    config["sourceId"] = QString::number(kSourceId);
    config["topicId"] = QString::number(kGaugeTopicId);
    config["fieldId"] = kGaugeFieldId;
    config["min"] = 0.0;
    config["max"] = 100.0;
    config["unit"] = "%";
    config["decimals"] = 1;
    return config;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Chart preview -- synthetic data, no serial");
    window.resize(960, 720);

    auto* layout = new QGridLayout(&window);

    auto* lineChart = new DummyLineChartWidget();
    lineChart->setConfig(lineChartConfig());
    layout->addWidget(lineChart, 0, 0, 1, 2);

    auto* barChart = new DummyBarChartWidget();
    barChart->setConfig(barChartConfig());
    layout->addWidget(barChart, 1, 0);

    auto* gauge = new DummyGaugeWidget();
    gauge->setConfig(gaugeConfig());
    layout->addWidget(gauge, 1, 1);

    window.show();

    // Tick counter driving the synthetic waveforms below -- purely cosmetic,
    // unrelated to any real sample-time/xAxis config. timestampUs is a
    // synthetic monotonically increasing microsecond clock, standing in for
    // a real BTP origin timestamp.
    auto tick = std::make_shared<qint64>(0);
    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, &window, [=]() {
        ++*tick;
        const double t = *tick;
        const quint64 timestampUs = quint64(*tick) * 50000;  // matches the 50ms timer below

        const double v0 = 50.0 + 40.0 * qSin(t * 0.05);
        const double v1 = 50.0 + 30.0 * qSin(t * 0.05 + 1.5);
        const double v2 = 50.0 + 20.0 * qSin(t * 0.03 + 3.0);
        lineChart->appendFieldSample(1, timestampUs, v0);
        lineChart->appendFieldSample(2, timestampUs, v1);
        lineChart->appendFieldSample(3, timestampUs, v2);
        gauge->appendFieldSample(kGaugeFieldId, timestampUs, v0);

        if (*tick % 6 == 0) {
            const double b0 = QRandomGenerator::global()->bounded(20, 100);
            const double b1 = QRandomGenerator::global()->bounded(20, 100);
            barChart->appendFieldSample(1, timestampUs, b0);
            barChart->appendFieldSample(2, timestampUs, b1);
        }
    });
    timer->start(50);

    return app.exec();
}
