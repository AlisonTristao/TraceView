// Standalone visual harness for the chart widgets -- NOT a Qt Test. It
// injects synthetic CSV payloads straight into onSerialPayload() on a timer
// so the line/bar/gauge designs can be eyeballed without a serial device or
// the SerialDataRouter/protocol plumbing (docs/PROTOCOL.md). See
// tests/test_chartdata.cpp for the actual decode-logic unit tests.

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

QJsonObject seriesJson(const QString& name, int index, const QString& color, const QString& style) {
    QJsonObject series;
    series["name"] = name;
    series["index"] = index;
    series["color"] = color;
    series["style"] = style;
    return series;
}

QJsonObject lineChartConfig() {
    QJsonObject config;
    config["format"] = "csv";

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
    series.append(seriesJson("Temp", 0, "#3B82F6", "solid"));
    series.append(seriesJson("Pressure", 1, "#F97316", "dashed"));
    series.append(seriesJson("Humidity", 2, "#22C55E", "cross"));
    config["series"] = series;
    return config;
}

QJsonObject barChartConfig() {
    QJsonObject config;
    config["format"] = "csv";

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
    series.append(seriesJson("A", 0, "#A855F7", "solid"));
    series.append(seriesJson("B", 1, "#EAB308", "solid"));
    config["series"] = series;
    return config;
}

QJsonObject gaugeConfig() {
    QJsonObject config;
    config["format"] = "csv";
    config["index"] = 0;
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
    // unrelated to any real sample-time/xAxis config.
    auto tick = std::make_shared<int>(0);
    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, &window, [=]() {
        ++*tick;
        const double t = *tick;

        const double v0 = 50.0 + 40.0 * qSin(t * 0.05);
        const double v1 = 50.0 + 30.0 * qSin(t * 0.05 + 1.5);
        const double v2 = 50.0 + 20.0 * qSin(t * 0.03 + 3.0);
        const QByteArray linePayload =
            QString("%1;%2;%3").arg(v0, 0, 'f', 3).arg(v1, 0, 'f', 3).arg(v2, 0, 'f', 3).toUtf8();
        lineChart->onSerialPayload(linePayload);
        gauge->onSerialPayload(linePayload);

        if (*tick % 6 == 0) {
            const double b0 = QRandomGenerator::global()->bounded(20, 100);
            const double b1 = QRandomGenerator::global()->bounded(20, 100);
            const QByteArray barPayload = QString("%1;%2").arg(b0, 0, 'f', 3).arg(b1, 0, 'f', 3).toUtf8();
            barChart->onSerialPayload(barPayload);
        }
    });
    timer->start(50);

    return app.exec();
}
