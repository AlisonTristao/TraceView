// Headless throughput benchmark for the chart widgets -- answers "how many
// paintEvent()s/sec can this widget actually sustain" with the two
// artificial throttles (DebugChartsWindow's 50ms synthetic-data tick,
// ChartWidgetBase::scheduleRepaint()'s 33ms repaint coalescing) removed from
// the measurement entirely. Renders straight into an offscreen QPixmap via
// QWidget::render(), which calls paintEvent() directly and bypasses update()
// (so scheduleRepaint()'s throttle and the window compositor's own vsync
// pacing never enter the number) -- this is the paint code's raw CPU cost,
// not what you'd see onscreen with the throttles active (that's
// DebugChartsWindow's title-bar FPS readout instead). Prints results to
// stdout; no window is shown.

#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QtMath>

#include "dashboard/widgets/chartwidgets.h"

using namespace traceview;

namespace {

// Matches DebugChartsWindow's own dummy configs (debugchartswindow.cpp) so
// this measures the same shape of chart (series count, axis mode) users
// actually see there.
QJsonObject seriesJson(const QString& name, int fieldId, const QString& color, const QString& style) {
    QJsonObject series;
    series["name"] = name;
    series["fieldId"] = fieldId;
    series["color"] = color;
    series["style"] = style;
    return series;
}

QJsonObject lineChartConfig(int xAxisLimit) {
    QJsonObject config;
    config["sourceId"] = "0";
    config["topicId"] = "1";
    QJsonObject xAxis;
    xAxis["mode"] = "samples";
    xAxis["limit"] = xAxisLimit;
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
    config["sourceId"] = "0";
    config["topicId"] = "2";
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

QJsonObject gaugeConfig() {
    QJsonObject config;
    config["sourceId"] = "0";
    config["topicId"] = "3";
    config["min"] = 0.0;
    config["max"] = 100.0;
    config["unit"] = "%";
    config["decimals"] = 1;
    QJsonArray series;
    series.append(seriesJson("Speed", 1, "#3B82F6", "solid"));
    series.append(seriesJson("Load", 2, "#F97316", "solid"));
    series.append(seriesJson("Temp", 3, "#22C55E", "solid"));
    config["series"] = series;
    return config;
}

// Renders `widget` into an offscreen pixmap `iterations` times back to back,
// no event loop turns in between -- QElapsedTimer wraps the loop, so the
// result is pure paintEvent() CPU time (widget setup/config parsing already
// happened before this is called).
double benchmarkPaint(QWidget* widget, int iterations) {
    QPixmap pixmap(widget->size());
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        pixmap.fill(Qt::black);
        QPainter painter(&pixmap);
        widget->render(&painter);
    }
    const qint64 elapsedNs = timer.nsecsElapsed();
    return double(elapsedNs) / 1e6 / iterations;  // ms/frame
}

void report(const QString& label, double msPerFrame) {
    const double fps = 1000.0 / msPerFrame;
    printf("%-28s %8.4f ms/frame   %10.1f fps (single-widget ceiling)\n", qPrintable(label), msPerFrame, fps);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    constexpr int kIterations = 2000;

    // Line chart at the x-axis sample limit used onscreen (150, same as
    // DebugChartsWindow) -- paint cost scales with buffered sample count, so
    // an empty/half-full buffer would understate steady-state cost.
    auto* lineChart = new DummyLineChartWidget();
    lineChart->setConfig(lineChartConfig(150));
    lineChart->resize(900, 300);
    for (int i = 0; i < 150; ++i) {
        const quint64 t = quint64(i) * 50000;
        lineChart->appendFieldSample(1, t, 50.0 + 40.0 * qSin(i * 0.05));
        lineChart->appendFieldSample(2, t, 50.0 + 30.0 * qSin(i * 0.05 + 1.5));
        lineChart->appendFieldSample(3, t, 50.0 + 20.0 * qSin(i * 0.03 + 3.0));
    }

    auto* barChart = new DummyBarChartWidget();
    barChart->setConfig(barChartConfig());
    barChart->resize(300, 300);
    barChart->appendFieldSample(1, 0, 62.0);
    barChart->appendFieldSample(2, 0, 41.0);
    barChart->appendFieldSample(3, 0, 78.0);
    barChart->appendFieldSample(4, 0, 55.0);
    barChart->appendFieldSample(5, 0, 33.0);

    auto* gauge = new DummyGaugeWidget();
    gauge->setConfig(gaugeConfig());
    gauge->resize(300, 300);
    gauge->appendFieldSample(1, 0, 62.0);
    gauge->appendFieldSample(2, 0, 41.0);
    gauge->appendFieldSample(3, 0, 78.0);

    printf("Chart widget paint throughput (offscreen, no throttle, %d iterations each)\n", kIterations);
    printf("-----------------------------------------------------------------------------\n");

    const double lineMs = benchmarkPaint(lineChart, kIterations);
    report("Line chart (3 series, 150 pts)", lineMs);

    const double barMs = benchmarkPaint(barChart, kIterations);
    report("Bar chart (5 series)", barMs);

    const double gaugeMs = benchmarkPaint(gauge, kIterations);
    report("Gauge (3 rings)", gaugeMs);

    // Same GUI thread paints all widgets one after another in real usage, so
    // the combined ceiling is 1 / (sum of per-widget frame times) --
    // compare this to DebugChartsWindow's onscreen title-bar FPS to see how
    // much of the gap is the artificial 20Hz/30Hz throttle vs. real paint cost.
    const double combinedMs = lineMs + barMs + gaugeMs;
    printf("-----------------------------------------------------------------------------\n");
    report("All 3 combined, same thread", combinedMs);

    return 0;
}
