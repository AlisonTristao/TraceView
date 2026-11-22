// Standalone visual harness for the chart widgets -- NOT a Qt Test. It
// injects synthetic samples straight into appendFieldSample() on a timer so
// the line/bar/gauge designs can be eyeballed without a serial device or
// the BtpSession/ProtocolRouter/TelemetryFieldRouter plumbing (see
// lib/protocol). See tests/test_chartdata.cpp for the actual config/buffer
// unit tests.

#include <QApplication>
#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QFileInfo>
#include <QFile>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <QtMath>
#include <cstdio>
#include <memory>

#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/appearancecatalog.h"
#include "dashboard/dashboardcell.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/layoutadvisor.h"
#include "dashboard/widgets/textboardwidget.h"
#include "dashboard/telemetryconsumer.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "dashboard/widgets/chartwidgets.h"
#include "dashboard/widgets/controlwidgets.h"
#include "traceview/thememanager.h"

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
                       const QString& style, const QString& unit = QString(),
                       double declaredMin = qQNaN(), double declaredMax = qQNaN()) {
    QJsonObject series;
    series["name"] = name;
    series["fieldId"] = fieldId;
    series["color"] = color;
    series["style"] = style;
    if (!unit.isEmpty()) {
        series["unit"] = unit;
    }
    if (!qIsNaN(declaredMin)) {
        series["min"] = declaredMin;
    }
    if (!qIsNaN(declaredMax)) {
        series["max"] = declaredMax;
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
    yAxis["unit"] = "Valor lido [V]";
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
//
// "Speed" also carries a declared range (-1..1) narrower than what it
// actually swings (+-1.5, see the timer tick below) -- exercises
// autoYRange()'s other new behavior: a device-declared range wins outright
// over the buffer scan, so this one axis should visibly clip/touch its own
// edges instead of auto-stretching to fit the sine's real amplitude the way
// "Turn rate" and "Duty cycle" (no declared range) still do.
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
    series.append(seriesJson("Speed", 1, "#3B82F6", "solid", "m/s", -1.0, 1.0));
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
    yAxis["unit"] = "Valor lido [%]";
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

struct PreviewWidgets {
    DummyLineChartWidget* lineChart = nullptr;
    DummyLineChartWidget* autoAxisChart = nullptr;
    DummyBarChartWidget* barChart = nullptr;
    DummyGaugeWidget* gauge = nullptr;
};

PreviewWidgets makeWidgets() {
    PreviewWidgets widgets;
    widgets.lineChart = new DummyLineChartWidget();
    widgets.lineChart->setConfig(lineChartConfig());
    widgets.autoAxisChart = new DummyLineChartWidget();
    widgets.autoAxisChart->setConfig(autoAxisLineChartConfig());
    widgets.barChart = new DummyBarChartWidget();
    widgets.barChart->setConfig(barChartConfig());
    widgets.gauge = new DummyGaugeWidget();
    widgets.gauge->setConfig(gaugeConfig());
    return widgets;
}

// One tick of the synthetic waveforms -- purely cosmetic, unrelated to any
// real sample-time/xAxis config. timestampUs is a synthetic monotonically
// increasing microsecond clock, standing in for a real BTP origin timestamp.
void feedTick(const PreviewWidgets& w, qint64 tick) {
    const double t = double(tick);
    const quint64 timestampUs = quint64(tick) * 50000;  // matches the 50ms timer in main()

    const double v0 = 50.0 + 40.0 * qSin(t * 0.05);
    const double v1 = 50.0 + 30.0 * qSin(t * 0.05 + 1.5);
    const double v2 = 50.0 + 20.0 * qSin(t * 0.03 + 3.0);
    w.lineChart->appendFieldSample(1, timestampUs, v0);
    w.lineChart->appendFieldSample(2, timestampUs, v1);
    w.lineChart->appendFieldSample(3, timestampUs, v2);
    w.gauge->appendFieldSample(kGaugeFieldId, timestampUs, v0);
    w.gauge->appendFieldSample(kGaugeFieldId2, timestampUs, v1);
    w.gauge->appendFieldSample(kGaugeFieldId3, timestampUs, v2);

    // Three wildly different magnitudes/units on one autoAxis chart --
    // m/s and rad/s stay small, "%" swings across the full 0-100 range --
    // so it's obvious at a glance that each is scaled against its own
    // stacked axis rather than one shared one (which would flatten the
    // first two to a barely-visible sliver next to the third).
    const double speed = 1.5 * qSin(t * 0.04);
    const double turnRate = 4.0 * qSin(t * 0.04 + 1.0);
    const double duty = 50.0 + 45.0 * qSin(t * 0.04 + 2.0);
    w.autoAxisChart->appendFieldSample(1, timestampUs, speed);
    w.autoAxisChart->appendFieldSample(2, timestampUs, turnRate);
    w.autoAxisChart->appendFieldSample(3, timestampUs, duty);

    // Five sines at increasing frequencies, fed every tick -- see
    // debugchartswindow.cpp's tick() (this tool's configs are kept in
    // sync with that one, per the comment above).
    const double b0 = 50.0 + 45.0 * qSin(t * 0.01);
    const double b1 = 50.0 + 45.0 * qSin(t * 0.02);
    const double b2 = 50.0 + 45.0 * qSin(t * 0.05);
    const double b3 = 50.0 + 45.0 * qSin(t * 0.1);
    const double b4 = 50.0 + 45.0 * qSin(t * 0.2);
    w.barChart->appendFieldSample(1, timestampUs, b0);
    w.barChart->appendFieldSample(2, timestampUs, b1);
    w.barChart->appendFieldSample(3, timestampUs, b2);
    w.barChart->appendFieldSample(4, timestampUs, b3);
    w.barChart->appendFieldSample(5, timestampUs, b4);
}

// --snapshot <dir>: renders every chart kind in every chart style
// (widgets/chartstyle.h) and every theme into one contact sheet per theme,
// <dir>/charts-<theme>.png, without opening a window -- the quickest way to
// compare styles side by side, or to check a new chart kind against the
// existing ones (docs/CHART_STYLE.md). Works headless with
// QT_QPA_PLATFORM=offscreen.
int writeSnapshots(const QString& dir) {
    QDir().mkpath(dir);
    const PreviewWidgets w = makeWidgets();
    for (qint64 tick = 1; tick <= 200; ++tick) {
        feedTick(w, tick);
    }

    struct Cell {
        DashboardWidget* widget;
        QSize size;
        QString gaugeShape;  // empty: the row's own view
    };
    const QVector<Cell> cells = {{w.lineChart, QSize(560, 280), {}},
                                 {w.autoAxisChart, QSize(560, 280), {}},
                                 {w.barChart, QSize(320, 280), {}},
                                 {w.gauge, QSize(280, 280), {}},
                                 {w.gauge, QSize(280, 280), QStringLiteral("meter")},
                                 {w.gauge, QSize(280, 280), QStringLiteral("display")},
                                 {w.gauge, QSize(280, 280), QStringLiteral("segments")},
                                 {w.gauge, QSize(280, 280), QStringLiteral("dial")},
                                 {w.gauge, QSize(280, 280), QStringLiteral("vu")},
                                 {w.gauge, QSize(280, 280), QStringLiteral("edgewise")},
                                 {w.gauge, QSize(280, 280), QStringLiteral("counter")}};
    constexpr int kGap = 16;
    constexpr int kLabelHeight = 28;
    constexpr int kCellHeight = 280;
    int sheetWidth = kGap;
    for (const Cell& cell : cells) {
        sheetWidth += cell.size.width() + kGap;
    }
    // One row per style at its defaults, plus one with every axis label
    // switched off from the gear menu, to exercise that path too.
    struct Row {
        QString label;
        QJsonObject view;
    };
    QVector<Row> rows;
    for (ChartStyleId style : allChartStyles()) {
        QJsonObject view;
        view["style"] = chartStyleIdString(style);
        rows.append({chartStyleDisplayName(style), view});
    }
    QJsonObject bare;
    bare["style"] = chartStyleIdString(ChartStyleId::Engineering);
    bare["xAxisTitle"] = false;
    bare["yAxisTitle"] = false;
    bare["xTickLabels"] = false;
    bare["yTickLabels"] = false;
    bare["scaleLabels"] = false;
    rows.append({QStringLiteral("Engineering, axis labels off"), bare});
    QJsonObject inset;
    inset["style"] = chartStyleIdString(ChartStyleId::Dashboard);
    inset["legend"] = "topRight";
    inset["legendOpacity"] = 75;
    inset["yTicks"] = 5;
    inset["lineWidth"] = 1.0;
    inset["xTickLabels"] = true;
    rows.append({QStringLiteral("Dashboard, legend top right, 5 Y ticks, 1 px lines"), inset});

    const int rowHeight = kLabelHeight + kCellHeight + kGap;
    const int sheetHeight = kGap + rowHeight * int(rows.size());

    for (const ThemePalette& theme : ThemeManager::instance().availableThemes()) {
        ThemeManager::instance().setTheme(theme.id);
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        QImage sheet(sheetWidth, sheetHeight, QImage::Format_ARGB32);
        sheet.fill(palette.background);
        QPainter painter(&sheet);
        painter.setPen(palette.textPrimary);

        int y = kGap;
        for (const Row& row : rows) {
            painter.drawText(QRect(kGap, y, sheetWidth, kLabelHeight),
                             Qt::AlignLeft | Qt::AlignVCenter, row.label);
            int x = kGap;
            for (const Cell& cell : cells) {
                QJsonObject view = row.view;
                if (!cell.gaugeShape.isEmpty()) {
                    view["gaugeShape"] = cell.gaugeShape;
                }
                cell.widget->setViewConfig(view);
                cell.widget->resize(cell.size);
                painter.drawPixmap(x, y + kLabelHeight, cell.widget->grab());
                x += cell.size.width() + kGap;
            }
            y += rowHeight;
        }
        painter.end();
        const QString path = QDir(dir).filePath(QStringLiteral("charts-%1.png").arg(theme.id));
        if (!sheet.save(path)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("%s\n", qPrintable(path));
    }
    return 0;
}

// Second sheet of --snapshot: every frame style (traceview/framestyle.h) in
// real DashboardCells -- idle, selected in edit mode, a gauge -- plus plain
// QSS controls, one sheet per theme, <dir>/frames-<theme>.png.
int writeFrameSnapshots(const QString& dir) {
    const QString originalFrame =
        frameStyleIdString(ThemeManager::instance().currentFrameStyle().id);
    constexpr int kGap = 16;
    constexpr int kLabelHeight = 28;
    const QSize cellSize(420, 220);
    const QSize gaugeSize(240, 220);
    const QSize controlsSize(200, 220);
    const QSize serialSize(300, 220);
    const int sheetWidth = kGap * 6 + cellSize.width() * 2 + gaugeSize.width() +
                           serialSize.width() + controlsSize.width();
    const QVector<FrameStyleId> frames = allFrameStyles();
    const int rowHeight = kLabelHeight + cellSize.height() + kGap;
    const int sheetHeight = kGap + rowHeight * int(frames.size());

    for (const ThemePalette& theme : ThemeManager::instance().availableThemes()) {
        ThemeManager::instance().setTheme(theme.id);
        const ThemePalette& palette = ThemeManager::instance().currentTheme();
        QImage sheet(sheetWidth, sheetHeight, QImage::Format_ARGB32);
        sheet.fill(palette.background);
        QPainter painter(&sheet);
        painter.setPen(palette.textPrimary);

        int y = kGap;
        for (FrameStyleId frame : frames) {
            ThemeManager::instance().setFrameStyle(frameStyleIdString(frame));
            painter.drawText(QRect(kGap, y, sheetWidth, kLabelHeight),
                             Qt::AlignLeft | Qt::AlignVCenter, frameStyleDisplayName(frame));

            const PreviewWidgets w = makeWidgets();
            for (qint64 tick = 1; tick <= 200; ++tick) {
                feedTick(w, tick);
            }
            DashboardCell lineCell("a", "dummy_line", "Line chart", w.lineChart);
            DashboardCell barCell("b", "dummy_bar", "Selected (edit mode)", w.barChart);
            DashboardCell gaugeCell("c", "dummy_gauge", "Gauge", w.gauge);
            delete w.autoAxisChart;
            auto* serial = new SerialMonitorWidget();
            serial->appendData(QByteArrayLiteral("boot ok\r\n> status\r\nbattery 12.4 V\r\n"));
            DashboardCell serialCell("d", "serial_monitor", "Serial monitor", serial);
            barCell.setEditMode(true);
            barCell.setSelected(true);
            // Let the selection fade-in (DashboardCell, ~150ms) finish.
            QElapsedTimer fade;
            fade.start();
            while (fade.elapsed() < 250) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            }

            QWidget controls;
            controls.setAttribute(Qt::WA_StyledBackground, true);
            auto* controlsLayout = new QVBoxLayout(&controls);
            controlsLayout->addWidget(new QPushButton(QStringLiteral("Button")));
            controlsLayout->addWidget(new QLineEdit(QStringLiteral("Text field")));
            controlsLayout->addStretch();

            int x = kGap;
            const QVector<QPair<QWidget*, QSize>> cells = {{&lineCell, cellSize},
                                                           {&barCell, cellSize},
                                                           {&gaugeCell, gaugeSize},
                                                           {&serialCell, serialSize},
                                                           {&controls, controlsSize}};
            for (const auto& [widget, size] : cells) {
                widget->resize(size);
                painter.drawPixmap(x, y + kLabelHeight, widget->grab());
                x += size.width() + kGap;
            }
            y += rowHeight;
        }
        painter.end();
        const QString path = QDir(dir).filePath(QStringLiteral("frames-%1.png").arg(theme.id));
        if (!sheet.save(path)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("%s\n", qPrintable(path));
    }
    ThemeManager::instance().setFrameStyle(originalFrame);
    return 0;
}

QJsonObject gridItem(const QString& id, const QString& type, const QString& name,
                     const QJsonObject& config, double x, double y, double width,
                     double height) {
    QJsonObject item;
    item["id"] = id;
    item["type"] = type;
    item["name"] = name;
    item["config"] = config;
    item["x"] = x;
    item["y"] = y;
    item["width"] = width;
    item["height"] = height;
    return item;
}

QJsonObject withGaugeShape(QJsonObject config, const QString& shape, int seriesCount) {
    QJsonObject view;
    view["gaugeShape"] = shape;
    view["scaleLabels"] = true;
    config["view"] = view;
    QJsonArray series = config["series"].toArray();
    while (series.size() > seriesCount) {
        series.removeLast();
    }
    config["series"] = series;
    return config;
}

// Third sheet of --snapshot: a real DashboardGrid (Run mode) holding every
// widget kind, rendered once per built-in appearance preset
// (core/appearancecatalog.h) -- <dir>/preset-<id>.png. Shows palette,
// frame, chart style, data colors, density, card header and canvas working
// together, and the gauge shapes.
int writePresetSnapshots(const QString& dir) {
    const AppearanceSnapshot original = currentAppearance();

    QJsonArray items;
    // Laid out in grid cells (60 x 40 on the Large layout) after the layout
    // hints of docs/LAYOUT.md, and checked by reviewLayout() below: three
    // bands -- wide charts and round gauges, then bar-shaped widgets, then a
    // strip of controls at one shared height -- plus two overlays in free
    // zones (a button in the line chart's upper left, an LED in the ring's
    // free corner).
    auto cells = [](double col, double row, double cols, double rows) {
        return QRectF(col / 60.0, row / 40.0, cols / 60.0, rows / 40.0);
    };
    auto place = [&items, &cells](const QString& id, const QString& type, const QString& name,
                                  const QJsonObject& config, double col, double row, double cols,
                                  double rows) {
        const QRectF r = cells(col, row, cols, rows);
        items.append(gridItem(id, type, name, config, r.x(), r.y(), r.width(), r.height()));
    };
    const QJsonObject ledBinding{{"sourceId", "0x00000001"}, {"topicId", "0x0001"}};
    auto led = [&ledBinding](const QString& label, int fieldId, const QJsonObject& extra) {
        QJsonObject config = ledBinding;
        config["label"] = label;
        config["fieldId"] = fieldId;
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            config[it.key()] = it.value();
        }
        return config;
    };

    // Band 1 (rows 0-20): the wide chart, a ring, a meter over its display.
    place("line", "dummy_line", "Line chart", lineChartConfig(), 0, 0, 30, 20);
    place("dial", "dummy_gauge", "Analog dial", withGaugeShape(gaugeConfig(), "dial", 1), 30, 0,
          14, 20);
    place("meter", "dummy_gauge", "Analog meter", withGaugeShape(gaugeConfig(), "meter", 1), 44,
          0, 16, 13);
    place("counter", "dummy_gauge", "Analog counter", withGaugeShape(gaugeConfig(), "counter", 1),
          44, 13, 16, 7);
    // Band 2 (rows 20-32): bar-shaped widgets.
    place("segments", "dummy_gauge", "LED bar", withGaugeShape(gaugeConfig(), "segments", 3), 0,
          20, 18, 12);
    place("edgewise", "dummy_gauge", "Analog edgewise",
          withGaugeShape(gaugeConfig(), "edgewise", 3), 18, 20, 14, 12);
    place("vu", "dummy_gauge", "Analog VU", withGaugeShape(gaugeConfig(), "vu", 2), 32, 20, 14,
          12);
    place("serial", "serial_monitor", "Serial monitor", QJsonObject(), 46, 20, 14, 12);
    // Band 3 (rows 32-40): controls, two rows of 4 cells each.
    place("start", "push_button", "Start", QJsonObject{{"label", "Start"}, {"variant", "success"}},
          0, 32, 7, 4);
    place("stop", "push_button", "Stop", QJsonObject{{"label", "Stop"}, {"variant", "danger"}}, 7,
          32, 7, 4);
    place("home", "push_button", "Home", QJsonObject{{"label", "Home"}}, 0, 36, 7, 4);
    place("reset", "push_button", "Reset", QJsonObject{{"label", "Reset"}, {"variant", "warning"}},
          7, 36, 7, 4);
    place("auto", "toggle_switch", "Auto mode",
          QJsonObject{{"label", "Auto mode"},
                      {"onLabel", "Auto"},
                      {"offLabel", "Manual"},
                      {"defaultState", true}},
          14, 32, 9, 4);
    place("lights", "toggle_switch", "Lights",
          QJsonObject{{"label", "Lights"}, {"onLabel", "On"}, {"offLabel", "Off"}}, 14, 36, 9, 4);
    place("setpoint", "slider", "Setpoint",
          QJsonObject{{"label", "Setpoint"}, {"unit", "%"}, {"defaultValue", 62}}, 23, 32, 16, 4);
    place("gain", "slider", "Gain",
          QJsonObject{{"label", "Gain"}, {"min", 0}, {"max", 10}, {"step", 0.5},
                      {"defaultValue", 4}},
          23, 36, 16, 4);
    place("led-net", "led", "Network",
          led("Network", 1, {{"onLabel", "Online"}, {"offLabel", "Offline"}}), 39, 32, 5, 8);
    place("led-motor", "led", "Motor", led("Motor", 4, {{"onLabel", "Run"}, {"offLabel", "Stop"}}),
          44, 32, 5, 8);
    place("led-alarm", "led", "Alarm",
          led("Alarm", 2, {{"color", "#EF4444"}, {"onLabel", "Alarm"}, {"offLabel", "OK"}}), 49,
          32, 5, 8);
    place("led-rgb", "led", "Status", led("Status", 3, {{"colorMode", "rgb"}, {"onLabel", "RGB"}}),
          54, 32, 6, 8);
    // An overlay, last so it stacks on top, inside the line chart's free zone.
    place("tare", "push_button", "Tare", QJsonObject{{"label", "Tare"}}, 4, 4, 6, 3);
    QJsonObject dashboard;
    dashboard["items"] = items;
    dashboard["breakpoint"] = "large";

    for (const AppearancePreset& preset : appearancePresets()) {
        if (!preset.builtIn) {
            continue;
        }
        applyAppearance(preset.values);

        DashboardGrid grid;
        grid.resize(1280, 720);
        grid.fromJson(dashboard);
        grid.setEditMode(false);
        // Figure numbers (printed charts) arrive on the next event loop pass.
        QCoreApplication::processEvents();
        qint64 tick = 0;
        for (DashboardCell* cell : grid.findChildren<DashboardCell*>()) {
            DashboardWidget* content = cell->content();
            for (tick = 1; tick <= 200; ++tick) {
                const double t = double(tick);
                const quint64 us = quint64(tick) * 50000;
                const double v0 = 50.0 + 40.0 * qSin(t * 0.05);
                const double v1 = 50.0 + 30.0 * qSin(t * 0.05 + 1.5);
                const double v2 = 50.0 + 20.0 * qSin(t * 0.03 + 3.0);
                if (auto* chart = dynamic_cast<ChartWidgetBase*>(content)) {
                    chart->appendFieldSample(1, us, v0);
                    chart->appendFieldSample(2, us, v1);
                    chart->appendFieldSample(3, us, v2);
                    chart->appendFieldSample(4, us, 50.0 + 45.0 * qSin(t * 0.1));
                    chart->appendFieldSample(5, us, 50.0 + 45.0 * qSin(t * 0.2));
                } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(content)) {
                    gauge->appendFieldSample(kGaugeFieldId, us, v0);
                    gauge->appendFieldSample(kGaugeFieldId2, us, v1);
                    gauge->appendFieldSample(kGaugeFieldId3, us, v2);
                } else if (auto* lamp = dynamic_cast<LedWidget*>(content)) {
                    if (tick == 200) {
                        lamp->onFieldSample({1, 1, 1, 0}, us, 1.0);
                        lamp->onFieldSample({1, 1, 2, 0}, us, 0.0);
                        lamp->onFieldSample({1, 1, 3, 0}, us, double(0x00B4FF));
                        lamp->onFieldSample({1, 1, 4, 0}, us, 1.0);
                        lamp->onFieldSample({1, 1, 5, 0}, us, 1.0);
                    }
                } else if (auto* serial = dynamic_cast<SerialMonitorWidget*>(content)) {
                    if (tick == 1) {
                        serial->appendData(
                            QByteArrayLiteral("boot ok\r\n> status\r\nbattery 12.4 V\r\n"));
                    }
                }
            }
        }
        if (preset.id == appearancePresets().first().id) {
            QVector<LayoutReviewItem> review;
            const QSizeF canvas(grid.width(), grid.height());
            for (const DashboardItem& item : grid.items()) {
                const DashboardItem::Geometry& g = item.geometry(DashboardBreakpoint::Large);
                review.append({item.id, item.name, item.typeId, item.config,
                               QRectF(g.x * canvas.width(), g.y * canvas.height(),
                                      g.width * canvas.width(), g.height * canvas.height())});
            }
            const LayoutReview result = reviewLayout(review, canvas);
            for (const LayoutIssue& issue : result.issues) {
                std::printf("layout %s: %s\n", qPrintable(layoutSeverityString(issue.severity)),
                            qPrintable(issue.message));
            }
            std::printf("layout: %d issue(s), %d free overlay spot(s)\n",
                        int(result.issues.size()), int(result.overlaySpots.size()));
        }
        const QString id = preset.id.section(QLatin1Char(':'), 1);
        const QString path = QDir(dir).filePath(QStringLiteral("preset-%1.png").arg(id));
        // The grid paints only its canvas; the window background shows
        // through behind it in the app, so lay it down first here.
        QImage image(grid.size(), QImage::Format_ARGB32);
        image.fill(ThemeManager::instance().currentTheme().background);
        QPainter painter(&image);
        painter.drawPixmap(0, 0, grid.grab());
        painter.end();
        if (!image.save(path)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("%s\n", qPrintable(path));
    }
    applyAppearance(original);
    return 0;
}

// --render-project <file.tvproj> <dir> [preset]: renders every workspace of
// a saved project (Large layout, Run mode, 1280x720) to
// <dir>/<project>-<n>.png with synthetic data on every bound field, in the
// given built-in appearance preset ("traceview", "console"...; the app-wide
// default otherwise), and prints each workspace's layout review
// (dashboard/layoutadvisor.h). For checking a real dashboard's layout
// without its devices.
QString renderMatrixText() {
    QString text = QStringLiteral("FRAME 24x24\n");
    for (int r = 0; r < 24; ++r) {
        for (int c = 0; c < 24; ++c) {
            const double dx = c - 11.5;
            const double dy = r - 11.5;
            const bool line = qAbs(c - 9 - r / 6) < 2;
            text += (line || dx * dx + dy * dy < 12) ? QLatin1Char('1') : QLatin1Char('0');
        }
        text += QLatin1Char('\n');
    }
    return text;
}

void feedRenderWidget(DashboardWidget* content, const QJsonObject& config) {
    QVector<int> fields;
    for (const QJsonValue& series : config.value("series").toArray()) {
        fields.append(series.toObject().value("fieldId").toInt());
    }
    const quint32 sourceId =
        quint32(config.value("sourceId").toString().toULongLong(nullptr, 0));
    const quint16 topicId = quint16(config.value("topicId").toString().toUInt(nullptr, 0));
    for (qint64 tick = 1; tick <= 200; ++tick) {
        const double t = double(tick);
        const quint64 us = quint64(tick) * 50000;
        for (int i = 0; i < fields.size(); ++i) {
            const double v = 50.0 + 35.0 * qSin(t * 0.05 + i * 1.3) + 8.0 * qSin(t * 0.23 + i);
            if (auto* chart = dynamic_cast<ChartWidgetBase*>(content)) {
                chart->appendFieldSample(quint16(fields[i]), us, v);
            } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(content)) {
                gauge->appendFieldSample(quint16(fields[i]), us, v);
            }
        }
        if (auto* consumer = dynamic_cast<TelemetryConsumer*>(content)) {
            const QJsonObject bind = config.value("bind").toObject();
            int k = 0;
            for (auto it = bind.begin(); it != bind.end(); ++it, ++k) {
                const QJsonObject b = it.value().toObject();
                consumer->onFieldSample(
                    {quint32(b.value("sourceId").toString().toULongLong(nullptr, 0)),
                     quint16(b.value("topicId").toString().toUInt(nullptr, 0)),
                     quint16(b.value("fieldId").toInt()), 0},
                    us, k == 0 ? 0.6 * qSin(t * 0.04) : 3.1 + 0.15 * qSin(t * 0.09));
            }
            if (config.contains("fieldId")) {
                consumer->onFieldSample({sourceId, topicId,
                                         quint16(config.value("fieldId").toInt()), 0},
                                        us, 1.0);
            }
        }
    }
    if (auto* board = dynamic_cast<TextBoardWidget*>(content)) {
        const QString name = config.value("topicName").toString();
        board->setText(name.contains(QLatin1String("matrix"), Qt::CaseInsensitive)
                           ? renderMatrixText()
                           : QStringLiteral("uptime     01:12:44\nheap free  142 kB\n"
                                            "battery    7.62 V\ntemp       41.3 C\n"
                                            "imu        ok\ncamera     ok\nlink       usb"));
    } else if (auto* serial = dynamic_cast<SerialMonitorWidget*>(content)) {
        serial->appendData(QByteArrayLiteral("boot ok\r\n> status\r\nmode RUN\r\n"
                                             "battery 7.6 V\r\n> "));
    }
}

int renderProject(const QString& path, const QString& dir, const QString& presetId) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "could not read %s\n", qPrintable(path));
        return 1;
    }
    const QJsonObject project = QJsonDocument::fromJson(file.readAll()).object();
    if (!presetId.isEmpty()) {
        for (const AppearancePreset& preset : appearancePresets()) {
            if (preset.id.section(QLatin1Char(':'), 1) == presetId) {
                applyAppearance(preset.values);
            }
        }
    }
    QDir().mkpath(dir);
    const QString base = QFileInfo(path).completeBaseName();
    const QJsonArray workspaces = project.value("workspaces").toObject().value("list").toArray();
    for (int w = 0; w < workspaces.size(); ++w) {
        const QJsonObject workspace = workspaces[w].toObject();
        QJsonObject dashboard = workspace.value("dashboard").toObject();
        dashboard["breakpoint"] = "large";
        DashboardGrid grid;
        grid.resize(1280, 720);
        grid.fromJson(dashboard);
        grid.setEditMode(false);
        // Figure numbers (printed charts) arrive on the next event loop pass.
        QCoreApplication::processEvents();

        QHash<QString, QJsonObject> configs;
        QVector<LayoutReviewItem> review;
        const QSizeF canvas(grid.width(), grid.height());
        for (const DashboardItem& item : grid.items()) {
            configs.insert(item.id, item.config);
            const DashboardItem::Geometry& g = item.geometry(DashboardBreakpoint::Large);
            review.append({item.id, item.name, item.typeId, item.config,
                           QRectF(g.x * canvas.width(), g.y * canvas.height(),
                                  g.width * canvas.width(), g.height * canvas.height())});
        }
        for (DashboardCell* cell : grid.findChildren<DashboardCell*>()) {
            feedRenderWidget(cell->content(), configs.value(cell->itemId()));
        }
        QCoreApplication::processEvents();

        const QString name = workspace.value("name").toString();
        const LayoutReview result = reviewLayout(review, canvas);
        std::printf("== %s / %s: %d issue(s), %d free overlay spot(s)\n", qPrintable(base),
                    qPrintable(name), int(result.issues.size()),
                    int(result.overlaySpots.size()));
        for (const LayoutIssue& issue : result.issues) {
            std::printf("   %s: %s\n", qPrintable(layoutSeverityString(issue.severity)),
                        qPrintable(issue.message));
        }
        QImage image(grid.size(), QImage::Format_ARGB32);
        image.fill(ThemeManager::instance().currentTheme().background);
        QPainter painter(&image);
        painter.drawPixmap(0, 0, grid.grab());
        painter.end();
        const QString out = QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(base).arg(w + 1));
        if (!image.save(out)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(out));
            return 1;
        }
        std::printf("   %s\n", qPrintable(out));
    }
    return 0;
}

}  // namespace

// --paper-sheet <dir>: the Scientific look "printed on the page" -- charts
// without a card and with a "Fig. N" caption, hatched bars, protractor
// gauges, the checkbox form rows and the ruler sliders -- under the Paper
// preset and under the default (dark) one: <dir>/paper-<preset>.png.
int writePaperSheet(const QString& dir) {
    const AppearanceSnapshot original = currentAppearance();
    QJsonArray items;
    auto place = [&items](const QString& id, const QString& type, const QString& name,
                          QJsonObject config, double col, double row, double cols, double rows) {
        items.append(gridItem(id, type, name, config, col / 60.0, row / 40.0, cols / 60.0,
                              rows / 40.0));
    };
    auto scientificChart = [](QJsonObject config) {
        QJsonObject view = config["view"].toObject();
        view["style"] = "scientific";
        config["view"] = view;
        return config;
    };
    auto control = [](QJsonObject config) {
        config["style"] = "scientific";
        return config;
    };
    place("line", "dummy_line", QString::fromUtf8("Pendulum angle \xce\xb8 (rad)"),
          scientificChart(lineChartConfig()), 0, 0, 32, 22);
    place("bars", "dummy_bar", "Motor current (A)", scientificChart(barChartConfig()), 32, 0, 28,
          22);
    place("battery", "dummy_gauge", "Battery (%)",
          scientificChart(withGaugeShape(gaugeConfig(), "halfCircle", 1)), 0, 22, 16, 18);
    place("speed", "dummy_gauge", "Wheel speed",
          scientificChart(withGaugeShape(gaugeConfig(), "arc", 2)), 16, 22, 14, 18);
    place("motors", "toggle_switch", "Motors",
          control({{"label", "Motors"}, {"onLabel", "on"}, {"offLabel", "off"},
                   {"defaultState", true}}),
          31, 23, 14, 4);
    place("recovery", "toggle_switch", "Recovery",
          control({{"label", "Recovery"}, {"onLabel", "active"}, {"offLabel", "idle"}}), 31,
          27, 14, 4);
    place("log", "toggle_switch", "Log",
          control({{"label", "Log"}, {"onLabel", "recording"}, {"offLabel", "stopped"},
                   {"defaultState", true}}),
          31, 31, 14, 4);
    place("kp", "slider", "Kp",
          control({{"label", "Gain Kp"}, {"min", 0}, {"max", 5}, {"step", 0.01},
                   {"defaultValue", 2.92}}),
          46, 23, 14, 6);
    place("ki", "slider", "Ki",
          control({{"label", "Gain Ki"}, {"min", 0}, {"max", 1}, {"step", 0.01},
                   {"defaultValue", 0.35}}),
          46, 29, 14, 6);
    place("start", "push_button", "Start", control({{"label", "Start"}}), 31, 35, 7, 5);
    place("stop", "push_button", "Stop", control({{"label", "Stop"}, {"variant", "danger"}}),
          38, 35, 7, 5);
    place("save", "push_button", "Save", control({{"label", "Save"}}), 46, 35, 14, 5);
    QJsonObject dashboard;
    dashboard["items"] = items;
    dashboard["breakpoint"] = "large";

    for (const QString& presetId : {QStringLiteral("paper"), QStringLiteral("traceview")}) {
        for (const AppearancePreset& preset : appearancePresets()) {
            if (preset.id.section(QLatin1Char(':'), 1) == presetId) {
                applyAppearance(preset.values);
            }
        }
        DashboardGrid grid;
        grid.resize(1280, 720);
        grid.fromJson(dashboard);
        grid.setEditMode(false);
        QCoreApplication::processEvents();
        for (DashboardCell* cell : grid.findChildren<DashboardCell*>()) {
            DashboardWidget* content = cell->content();
            for (qint64 tick = 1; tick <= 200; ++tick) {
                const double t = double(tick);
                const quint64 us = quint64(tick) * 50000;
                if (auto* chart = dynamic_cast<ChartWidgetBase*>(content)) {
                    chart->appendFieldSample(1, us, 50.0 + 40.0 * qSin(t * 0.05));
                    chart->appendFieldSample(2, us, 50.0 + 30.0 * qSin(t * 0.05 + 1.5));
                    chart->appendFieldSample(3, us, 50.0 + 20.0 * qSin(t * 0.03 + 3.0));
                    chart->appendFieldSample(4, us, 50.0 + 45.0 * qSin(t * 0.1));
                    chart->appendFieldSample(5, us, 50.0 + 45.0 * qSin(t * 0.2));
                } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(content)) {
                    gauge->appendFieldSample(kGaugeFieldId, us, 50.0 + 40.0 * qSin(t * 0.05));
                    gauge->appendFieldSample(kGaugeFieldId2, us, 50.0 + 30.0 * qSin(t * 0.05 + 1.5));
                }
            }
        }
        const QString path = QDir(dir).filePath(QStringLiteral("paper-%1.png").arg(presetId));
        QImage image(grid.size(), QImage::Format_ARGB32);
        image.fill(ThemeManager::instance().currentTheme().background);
        QPainter painter(&image);
        painter.drawPixmap(0, 0, grid.grab());
        painter.end();
        if (!image.save(path)) {
            std::fprintf(stderr, "could not write %s\n", qPrintable(path));
            return 1;
        }
        std::printf("%s\n", qPrintable(path));
    }
    applyAppearance(original);
    return 0;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // Its own settings store: the appearance choices the snapshots switch
    // through are persisted by their managers, and must neither touch the
    // app's own settings nor silently go nowhere (QSettings needs a name).
    QCoreApplication::setOrganizationName(QStringLiteral("TraceViewTools"));
    QCoreApplication::setApplicationName(QStringLiteral("chart_preview"));
    ThemeManager::instance().applyCurrentTheme();

    const QStringList args = app.arguments();
    const int renderArg = int(args.indexOf(QStringLiteral("--render-project")));
    if (renderArg >= 0 && renderArg + 2 < args.size()) {
        return renderProject(args[renderArg + 1], args[renderArg + 2],
                             renderArg + 3 < args.size() ? args[renderArg + 3] : QString());
    }
    const int paperArg = int(args.indexOf(QStringLiteral("--paper-sheet")));
    if (paperArg >= 0) {
        return writePaperSheet(paperArg + 1 < args.size() ? args[paperArg + 1]
                                                          : QStringLiteral("."));
    }
    const int snapshotArg = int(args.indexOf(QStringLiteral("--snapshot")));
    if (snapshotArg >= 0) {
        const QString dir =
            snapshotArg + 1 < args.size() ? args[snapshotArg + 1] : QStringLiteral(".");
        if (const int charts = writeSnapshots(dir); charts != 0) {
            return charts;
        }
        if (const int frames = writeFrameSnapshots(dir); frames != 0) {
            return frames;
        }
        return writePresetSnapshots(dir);
    }

    QWidget window;
    window.setWindowTitle("Chart preview -- synthetic data, no serial");
    window.resize(960, 960);

    auto* layout = new QGridLayout(&window);
    const PreviewWidgets widgets = makeWidgets();
    layout->addWidget(widgets.lineChart, 0, 0, 1, 2);
    layout->addWidget(widgets.autoAxisChart, 1, 0, 1, 2);
    layout->addWidget(widgets.barChart, 2, 0);
    layout->addWidget(widgets.gauge, 2, 1);

    window.show();

    auto tick = std::make_shared<qint64>(0);
    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, &window, [=]() { feedTick(widgets, ++*tick); });
    timer->start(50);

    return app.exec();
}
