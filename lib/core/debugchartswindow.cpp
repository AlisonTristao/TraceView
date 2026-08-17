#include "debugchartswindow.h"

#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QtMath>

#include "dashboard/dashboardcell.h"
#include "dashboard/widgets/chartwidgets.h"
#include "dashboard/widgets/controlwidgets.h"
#include "dashboard/widgets/serialmonitorwidget.h"

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

// Configs below mirror the JSON shape controldata.cpp's parse*CommandConfig()
// functions read -- same reasoning as lineChartConfig()/barChartConfig()/
// gaugeConfig() above: a private, hand-built stand-in for what
// PushButtonConfigEditor/ToggleSwitchConfigEditor/SliderConfigEditor would
// normally produce, since there's no properties panel in this debug window.
QJsonObject pushButtonConfig() {
    QJsonObject config;
    config["label"] = "Ping";
    config["mode"] = "momentary";
    config["onPress"] = "PING\r\n";
    return config;
}

QJsonObject toggleSwitchConfig() {
    QJsonObject config;
    config["label"] = "Relay";
    config["onCommand"] = "RELAY:ON\r\n";
    config["offCommand"] = "RELAY:OFF\r\n";
    config["defaultState"] = false;
    return config;
}

QJsonObject sliderConfig() {
    QJsonObject config;
    config["label"] = "Speed";
    config["min"] = 0.0;
    config["max"] = 100.0;
    config["step"] = 1.0;
    config["defaultValue"] = 50.0;
    config["unit"] = "%";
    config["showValue"] = true;
    config["sendMode"] = "onRelease";
    config["commandTemplate"] = "SPEED:{value}\r\n";
    return config;
}

// Cycled into the serial monitor on a timer (see DebugChartsWindow::tick())
// so its terminal font -- fixed at construction (SerialTerminalWidget's own
// setFont() call, not driven by FontManager the way the rest of the app is)
// -- has a constant stream of upper/lowercase, digits, punctuation and
// accented pt-BR text to render, without needing to touch a control widget
// first to see anything at all.
const QStringList& debugTerminalLines() {
    static const QStringList lines = {
        QStringLiteral("user@dongle:~$ status"),
        QStringLiteral("OK  boot=12.3s  temp=42.1C  batt=87%"),
        QStringLiteral("[INFO] Configuracao carregada: baud=115200"),
        QStringLiteral("ERRO: sensor nao respondeu (timeout 250ms)"),
        QStringLiteral("> ping 192.168.0.42 -c 3"),
        QStringLiteral("64 bytes de 192.168.0.42: tempo=3.2ms"),
        QStringLiteral("ABCDEFGHIJ abcdefghij 0123456789 !@#$%^&*()"),
    };
    return lines;
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
    setWindowTitle(tr("Debug -- synthetic chart data"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(980, 640);

    auto* layout = new QGridLayout(this);

    m_lineChart = new DummyLineChartWidget();
    m_lineChart->setConfig(lineChartConfig());
    layout->addWidget(wrapInCell("debug-line", "dummy_line", tr("Line Chart"), m_lineChart, this), 0, 0, 1, 3);

    m_barChart = new DummyBarChartWidget();
    m_barChart->setConfig(barChartConfig());
    layout->addWidget(wrapInCell("debug-bar", "dummy_bar", tr("Bar Chart"), m_barChart, this), 1, 0);

    m_gauge = new DummyGaugeWidget();
    m_gauge->setConfig(gaugeConfig());
    layout->addWidget(wrapInCell("debug-gauge", "dummy_gauge", tr("Gauge"), m_gauge, this), 1, 1);

    m_serialMonitor = new SerialMonitorWidget();
    layout->addWidget(wrapInCell("debug-serial", "serial_monitor", tr("Serial Monitor"), m_serialMonitor, this), 1, 2);

    // One of each control widget too (widgets/controlwidgets.h) -- their
    // sendRequested() is looped straight back into the serial monitor above
    // instead of going nowhere, so exercising a control also produces
    // terminal output, same as it would once SerialWidgetBridge wires this
    // up to a real SerialManager (BACKEND_TODO.txt Task 9/10).
    auto* pushButton = new PushButtonWidget();
    pushButton->setConfig(pushButtonConfig());
    connect(pushButton, &PushButtonWidget::sendRequested, m_serialMonitor, &SerialMonitorWidget::appendData);
    layout->addWidget(wrapInCell("debug-button", "push_button", tr("Push Button"), pushButton, this), 2, 0);

    auto* toggleSwitch = new ToggleSwitchWidget();
    toggleSwitch->setConfig(toggleSwitchConfig());
    connect(toggleSwitch, &ToggleSwitchWidget::sendRequested, m_serialMonitor, &SerialMonitorWidget::appendData);
    layout->addWidget(wrapInCell("debug-toggle", "toggle_switch", tr("Toggle Switch"), toggleSwitch, this), 2, 1);

    auto* slider = new SliderWidget();
    slider->setConfig(sliderConfig());
    connect(slider, &SliderWidget::sendRequested, m_serialMonitor, &SerialMonitorWidget::appendData);
    layout->addWidget(wrapInCell("debug-slider", "slider", tr("Slider"), slider, this), 2, 2);

    // Rows would otherwise split available height evenly -- way more than a
    // single small control (push button/toggle/slider) needs, and much more
    // than they'd ever get on a real dashboard grid. 7:7:1 keeps the chart
    // rows' relative 1:1 split intact while shrinking the control row to 1/5
    // of the equal-split height it had before this (row2 = 1/15 of total vs.
    // the previous 1/3). Reported live 2026-08-17 ("botao, toggle e slider
    // ta muito grande verticalmente").
    layout->setRowStretch(0, 7);
    layout->setRowStretch(1, 7);
    layout->setRowStretch(2, 1);
    // 1/15 of this window's own height alone isn't enough room for
    // ToggleSwitchWidget/SliderWidget's title label + control + margins --
    // without a floor they got clipped at the bottom instead of just
    // shrinking (reported live 2026-08-17, "o botao do relay ta ... ficando
    // meio escondido a parte de baixo"). This floor only kicks in below that
    // point; the 7:7:1 stretch above still governs anything beyond it.
    layout->setRowMinimumHeight(2, 90);

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

    // One synthetic line every ~1s (kTerminalLinePeriodTicks * 50ms), cycled
    // through debugTerminalLines() -- keeps the serial monitor's terminal
    // font visibly rendering text on its own, without needing to click the
    // controls above first.
    constexpr int kTerminalLinePeriodTicks = 20;
    if (m_tick % kTerminalLinePeriodTicks == 0) {
        const QStringList& lines = debugTerminalLines();
        const QString& line = lines.at((m_tick / kTerminalLinePeriodTicks) % lines.size());
        m_serialMonitor->appendData((line + QStringLiteral("\r\n")).toUtf8());
    }
}

} // namespace traceview
