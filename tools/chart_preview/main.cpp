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
constexpr quint16 kGaugeFieldId2 = 3;
constexpr quint16 kGaugeFieldId3 = 4;

QJsonObject seriesJson(const QString& name, int fieldId, const QString& color,
                       const QString& style, const QString& unit = QString()) {
    QJsonObject series;
    series["name"] = name;
    series["fieldId"] = fieldId;
    series["color"] = color;
    series["style"] = style;
    if (!unit.isEmpty()) {
        series["unit"] = unit;
    }
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

// Exercises ChartConfig::autoAxis: three series bound to three different
// units (m/s, rad/s, %) on the same chart -- with autoAxis off, plotting
// these together on one shared axis would squash the smaller-magnitude
// series flat; with it on, each unit gets its own stacked, auto-ranged Y
// axis (see resolveYAxes()/paintYAxes() in chartwidgets.cpp), tinted to that
// axis's own series color.
QJsonObject autoAxisLineChartConfig() {
    QJsonObject config;
    config["sourceId"] = QString::number(kSourceId);
    config["topicId"] = QString::number(kLineTopicId);

    QJsonObject xAxis;
    xAxis["mode"] = "samples";
    xAxis["limit"] = 150;
    config["xAxis"] = xAxis;

    QJsonObject yAxis;
    yAxis["autoAxis"] = true;
    config["yAxis"] = yAxis;

    QJsonArray series;
    series.append(seriesJson("Speed", 1, "#3B82F6", "solid", "m/s"));
    series.append(seriesJson("Turn rate", 2, "#F97316", "dashed", "rad/s"));
    series.append(seriesJson("Duty cycle", 3, "#22C55E", "cross", "%"));
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
    series.append(seriesJson("C", 3, "#EC4899", "solid"));
    series.append(seriesJson("D", 4, "#06B6D4", "solid"));
    series.append(seriesJson("E", 5, "#10B981", "solid"));
    config["series"] = series;
    return config;
}

// Three concentric rings -- exercises DummyGaugeWidget's multi-series
// rendering (nested arcs, per-ring ruler ticks/pointer, legend) instead of
// just the single-ring case.
QJsonObject gaugeConfig() {
    QJsonObject config;
    config["sourceId"] = QString::number(kSourceId);
    config["topicId"] = QString::number(kGaugeTopicId);
    config["min"] = 0.0;
    config["max"] = 100.0;
    config["unit"] = "%";
    config["decimals"] = 1;

    QJsonArray series;
    series.append(seriesJson("Speed", kGaugeFieldId, "#3B82F6", "solid"));
    series.append(seriesJson("Load", kGaugeFieldId2, "#F97316", "solid"));
    series.append(seriesJson("Temp", kGaugeFieldId3, "#22C55E", "solid"));
    config["series"] = series;
    return config;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle("Chart preview -- synthetic data, no serial");
    window.resize(960, 960);

    auto* layout = new QGridLayout(&window);

    auto* lineChart = new DummyLineChartWidget();
    lineChart->setConfig(lineChartConfig());
    layout->addWidget(lineChart, 0, 0, 1, 2);

    auto* autoAxisChart = new DummyLineChartWidget();
    autoAxisChart->setConfig(autoAxisLineChartConfig());
    layout->addWidget(autoAxisChart, 1, 0, 1, 2);

    auto* barChart = new DummyBarChartWidget();
    barChart->setConfig(barChartConfig());
    layout->addWidget(barChart, 2, 0);

    auto* gauge = new DummyGaugeWidget();
    gauge->setConfig(gaugeConfig());
    layout->addWidget(gauge, 2, 1);

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
        gauge->appendFieldSample(kGaugeFieldId2, timestampUs, v1);
        gauge->appendFieldSample(kGaugeFieldId3, timestampUs, v2);

        // Three wildly different magnitudes/units on one autoAxis chart --
        // m/s and rad/s stay small, "%" swings across the full 0-100 range --
        // so it's obvious at a glance that each is scaled against its own
        // stacked axis rather than one shared one (which would flatten the
        // first two to a barely-visible sliver next to the third).
        const double speed = 1.5 * qSin(t * 0.04);
        const double turnRate = 4.0 * qSin(t * 0.04 + 1.0);
        const double duty = 50.0 + 45.0 * qSin(t * 0.04 + 2.0);
        autoAxisChart->appendFieldSample(1, timestampUs, speed);
        autoAxisChart->appendFieldSample(2, timestampUs, turnRate);
        autoAxisChart->appendFieldSample(3, timestampUs, duty);

        // Five sines at increasing frequencies, fed every tick -- see
        // debugchartswindow.cpp's tick() (this tool's configs are kept in
        // sync with that one, per the comment above).
        const double b0 = 50.0 + 45.0 * qSin(t * 0.01);
        const double b1 = 50.0 + 45.0 * qSin(t * 0.02);
        const double b2 = 50.0 + 45.0 * qSin(t * 0.05);
        const double b3 = 50.0 + 45.0 * qSin(t * 0.1);
        const double b4 = 50.0 + 45.0 * qSin(t * 0.2);
        barChart->appendFieldSample(1, timestampUs, b0);
        barChart->appendFieldSample(2, timestampUs, b1);
        barChart->appendFieldSample(3, timestampUs, b2);
        barChart->appendFieldSample(4, timestampUs, b3);
        barChart->appendFieldSample(5, timestampUs, b4);
    });
    timer->start(50);

    return app.exec();
}
