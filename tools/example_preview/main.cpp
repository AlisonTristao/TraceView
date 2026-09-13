// Standalone visual harness for a whole .tvproj dashboard -- loads a real
// project file's "Default" workspace through the same DashboardGrid/
// WidgetRegistry code the app itself uses, then feeds synthetic samples into
// every chart-family widget it finds (line/bar/gauge) on a timer, exactly
// like chart_preview/main.cpp does for its own hardcoded 3 widgets. Lets any
// .tvproj dashboard -- including example.tvproj, which has more widgets than
// chart_preview covers -- be eyeballed/screenshotted without a real device.

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVector>
#include <QtMath>
#include <memory>

#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/chartwidgets.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "traceview/fontmanager.h"
#include "traceview/thememanager.h"

using namespace traceview;

namespace {

// One sine-wave feed per (widget, fieldId) pair, discovered from each
// widget's own series config as it's created -- see main()'s widgetCreated
// handler. Frequencies/phases are just spread out by index so series sharing
// one chart don't all move in lockstep.
struct FieldFeed {
    ChartWidgetBase* chartWidget = nullptr;
    DummyGaugeWidget* gaugeWidget = nullptr;
    quint16 fieldId = 0;
    double freq = 0.02;
    double phase = 0.0;
    double amplitude = 35.0;
    double center = 50.0;
};

const QStringList& fakeTerminalLines() {
    static const QStringList lines = {
        QStringLiteral("user@dongle:~$ status"),
        QStringLiteral("OK  boot=12.3s  temp=42.1C  batt=87%"),
        QStringLiteral("[INFO] Configuracao carregada: baud=115200"),
        QStringLiteral("> ping 192.168.0.42 -c 3"),
        QStringLiteral("64 bytes de 192.168.0.42: tempo=3.2ms"),
    };
    return lines;
}

// Finds the workspace to preview: the one named by "workspaces.activeId",
// or the first entry if that id isn't found (empty project, stale id, ...).
QJsonObject activeDashboard(const QJsonObject& root) {
    const QJsonObject workspaces = root["workspaces"].toObject();
    const QString activeId = workspaces["activeId"].toString();
    const QJsonArray list = workspaces["list"].toArray();

    for (const QJsonValue& value : list) {
        const QJsonObject workspace = value.toObject();
        if (workspace["id"].toString() == activeId) {
            return workspace["dashboard"].toObject();
        }
    }
    if (!list.isEmpty()) {
        return list.first().toObject()["dashboard"].toObject();
    }
    return QJsonObject();
}

}  // namespace

int main(int argc, char** argv) {
    QApplication::setStyle("Fusion");
    QApplication app(argc, argv);

    const QString projectPath =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("example.tvproj");
    QFile file(projectPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("example_preview: could not open '%s'", qPrintable(projectPath));
        return 1;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject dashboard = activeDashboard(root);

    traceview::ThemeManager::instance().applyCurrentTheme();
    traceview::FontManager::instance().applyCurrentFont();

    auto* grid = new DashboardGrid();
    grid->setWindowTitle("Example project preview -- synthetic data, no serial");
    grid->resize(1400, 900);

    auto feeds = std::make_shared<QVector<FieldFeed>>();
    auto serialMonitor = std::make_shared<SerialMonitorWidget*>(nullptr);
    auto feedIndex = std::make_shared<int>(0);

    // dynamic_cast, not qobject_cast: ChartWidgetBase/DummyGaugeWidget have no
    // Q_OBJECT of their own (see chartwidgets.h), so they carry no distinct
    // QMetaObject for qobject_cast to match against.
    QObject::connect(
        grid, &DashboardGrid::widgetCreated, grid,
        [grid, feeds, serialMonitor, feedIndex](DashboardWidget* widget) {
            if (auto* monitor = dynamic_cast<SerialMonitorWidget*>(widget)) {
                *serialMonitor = monitor;
                return;
            }

            auto* chartWidget = dynamic_cast<ChartWidgetBase*>(widget);
            auto* gaugeWidget = dynamic_cast<DummyGaugeWidget*>(widget);
            if (!chartWidget && !gaugeWidget) {
                return;
            }

            const QJsonArray series = grid->configForWidget(widget)["series"].toArray();
            for (const QJsonValue& seriesValue : series) {
                FieldFeed feed;
                feed.chartWidget = chartWidget;
                feed.gaugeWidget = gaugeWidget;
                feed.fieldId = quint16(seriesValue.toObject()["fieldId"].toInt());
                ++*feedIndex;
                feed.freq = 0.01 + 0.008 * (*feedIndex % 6);
                feed.phase = *feedIndex * 0.9;
                feed.amplitude = 20.0 + 8.0 * (*feedIndex % 4);
                feed.center = 50.0;
                feeds->append(feed);
            }
        });

    grid->fromJson(dashboard);
    grid->setEditMode(false);
    grid->show();

    auto tick = std::make_shared<qint64>(0);
    auto* timer = new QTimer(grid);
    QObject::connect(timer, &QTimer::timeout, grid, [feeds, serialMonitor, tick]() {
        ++*tick;
        const double t = double(*tick);
        const quint64 timestampUs = quint64(*tick) * 50000;  // matches the 50ms timer below

        for (const FieldFeed& feed : *feeds) {
            const double value = feed.center + feed.amplitude * qSin(t * feed.freq + feed.phase);
            if (feed.chartWidget) {
                feed.chartWidget->appendFieldSample(feed.fieldId, timestampUs, value);
            } else if (feed.gaugeWidget) {
                feed.gaugeWidget->appendFieldSample(feed.fieldId, timestampUs, value);
            }
        }

        constexpr int kTerminalLinePeriodTicks = 20;
        if (*serialMonitor && *tick % kTerminalLinePeriodTicks == 0) {
            const QStringList& lines = fakeTerminalLines();
            const QString& line = lines.at((*tick / kTerminalLinePeriodTicks) % lines.size());
            (*serialMonitor)->appendData((line + QStringLiteral("\r\n")).toUtf8());
        }
    });
    timer->start(50);

    return app.exec();
}
