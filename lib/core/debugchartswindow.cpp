#include "debugchartswindow.h"

#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QtMath>

#include "dashboard/dashboardcell.h"
#include "dashboard/widgets/chartwidgets.h"

namespace traceview {

namespace {

constexpr quint16 kTempFieldId = 1;
constexpr quint16 kPressureFieldId = 2;
constexpr quint16 kHumidityFieldId = 3;
constexpr quint16 kBarAFieldId = 1;
constexpr quint16 kBarBFieldId = 2;
constexpr quint16 kBarCFieldId = 3;
constexpr quint16 kBarDFieldId = 4;
constexpr quint16 kBarEFieldId = 5;
constexpr quint16 kGaugeFieldId = 1;
constexpr quint16 kGaugeFieldId2 = 2;
constexpr quint16 kGaugeFieldId3 = 3;
// Matches the 50ms tick timer below, so the synthetic samples' own
// timestamps read as a real elapsed-time clock instead of jumping in lockstep
// with the tick counter.
constexpr int kTickIntervalMs = 50;

QJsonObject seriesJson(const QString& name, int fieldId, const QString& color, const QString& style) {
    QJsonObject series;
    series["name"] = name;
    series["fieldId"] = fieldId;
    series["color"] = color;
    series["style"] = style;
    return series;
}

// Same synthetic configs as tools/chart_preview -- kept as a private copy
// here rather than shared, since that tool is a standalone executable with
// no library of its own to share this from and the configs are a handful of
// lines each.
QJsonObject lineChartConfig() {
    QJsonObject config;
    config["sourceId"] = "0";
    config["topicId"] = "1";

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
    series.append(seriesJson("Temp", kTempFieldId, "#3B82F6", "solid"));
    series.append(seriesJson("Pressure", kPressureFieldId, "#F97316", "dashed"));
    series.append(seriesJson("Humidity", kHumidityFieldId, "#22C55E", "cross"));
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
    series.append(seriesJson("A", kBarAFieldId, "#A855F7", "solid"));
    series.append(seriesJson("B", kBarBFieldId, "#EAB308", "solid"));
    series.append(seriesJson("C", kBarCFieldId, "#EC4899", "solid"));
    series.append(seriesJson("D", kBarDFieldId, "#06B6D4", "solid"));
    series.append(seriesJson("E", kBarEFieldId, "#10B981", "solid"));
    config["series"] = series;
    return config;
}

// Three concentric rings -- exercises DummyGaugeWidget's multi-series
// rendering (nested arcs, per-ring ruler ticks/pointer, legend) instead of
// just the single-ring case.
QJsonObject gaugeConfig() {
    QJsonObject config;
    config["sourceId"] = "0";
    config["topicId"] = "3";
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

// Wraps `content` in a DashboardCell with the exact chrome/interaction a
// real dashboard grid cell has -- header, pause/clear/gear, and (in edit mode
// off) live mouse events reaching `content` for things like the line chart's
// hover crosshair. See DashboardCell::setEditMode()'s WA_TransparentForMouseEvents
// toggle -- Run mode (false) is what makes the gear menu and hover both work.
DashboardCell* wrapInCell(const QString& itemId, const QString& typeId, const QString& title,
                           DashboardWidget* content, QWidget* parent) {
    auto* cell = new DashboardCell(itemId, typeId, title, content, parent);
    cell->setEditMode(false);
    cell->setConnected(true);
    return cell;
}

} // namespace

DebugChartsWindow::DebugChartsWindow(QWidget* parent) : QDialog(parent) {
    setWindowTitle("Debug -- synthetic chart data");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(960, 720);

    auto* layout = new QGridLayout(this);

    m_lineChart = new DummyLineChartWidget();
    m_lineChart->setConfig(lineChartConfig());
    layout->addWidget(wrapInCell("debug-line", "dummy_line", "Line Chart", m_lineChart, this), 0, 0, 1, 2);

    m_barChart = new DummyBarChartWidget();
    m_barChart->setConfig(barChartConfig());
    layout->addWidget(wrapInCell("debug-bar", "dummy_bar", "Bar Chart", m_barChart, this), 1, 0);

    m_gauge = new DummyGaugeWidget();
    m_gauge->setConfig(gaugeConfig());
    layout->addWidget(wrapInCell("debug-gauge", "dummy_gauge", "Gauge", m_gauge, this), 1, 1);

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DebugChartsWindow::tick);
    timer->start(kTickIntervalMs);
}

void DebugChartsWindow::tick() {
    ++m_tick;
    const double t = m_tick;
    const quint64 timestampUs = quint64(m_tick) * kTickIntervalMs * 1000;

    const double v0 = 50.0 + 40.0 * qSin(t * 0.05);
    const double v1 = 50.0 + 30.0 * qSin(t * 0.05 + 1.5);
    const double v2 = 50.0 + 20.0 * qSin(t * 0.03 + 3.0);
    m_lineChart->appendFieldSample(kTempFieldId, timestampUs, v0);
    m_lineChart->appendFieldSample(kPressureFieldId, timestampUs, v1);
    m_lineChart->appendFieldSample(kHumidityFieldId, timestampUs, v2);
    m_gauge->appendFieldSample(kGaugeFieldId, timestampUs, v0);
    m_gauge->appendFieldSample(kGaugeFieldId2, timestampUs, v1);
    m_gauge->appendFieldSample(kGaugeFieldId3, timestampUs, v2);

    // Five sines at increasing frequencies, all fed every tick so the bars
    // visibly move together like a live equalizer -- exercises the
    // fixed-bar snapshot render (paintBarSnapshot(), chartwidgets.cpp) with
    // more bars than the Y grid's 10 bands (see kBarYGridDivisions) so
    // heights read clearly against the 10%-step grid.
    const double b0 = 50.0 + 45.0 * qSin(t * 0.01);
    const double b1 = 50.0 + 45.0 * qSin(t * 0.02);
    const double b2 = 50.0 + 45.0 * qSin(t * 0.05);
    const double b3 = 50.0 + 45.0 * qSin(t * 0.1);
    const double b4 = 50.0 + 45.0 * qSin(t * 0.2);
    m_barChart->appendFieldSample(kBarAFieldId, timestampUs, b0);
    m_barChart->appendFieldSample(kBarBFieldId, timestampUs, b1);
    m_barChart->appendFieldSample(kBarCFieldId, timestampUs, b2);
    m_barChart->appendFieldSample(kBarDFieldId, timestampUs, b3);
    m_barChart->appendFieldSample(kBarEFieldId, timestampUs, b4);
}

} // namespace traceview
