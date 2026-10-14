// The line chart with series from several sources (lib/dashboard/widgets/
// multisourcechartwidget.h): each series' own device/topic/field under
// "bind", the topics it asks MainWindow for (one request per topic, however
// many series read it), samples routed to the series bound to their field
// and to no other, and its config editor writing the same shape back.

#include <QComboBox>
#include <QJsonArray>
#include <QtTest>
#include <memory>

#include "dashboard/widgetregistry.h"
#include "dashboard/widgets/multisourcechartconfigeditor.h"
#include "dashboard/widgets/multisourcechartwidget.h"

using namespace traceview;

namespace {

QJsonObject binding(const char* device, const char* source, const char* topic, int field,
                    const char* topicName, const char* fieldName) {
    return QJsonObject{{"deviceId", device}, {"sourceId", source},   {"topicId", topic},
                       {"fieldId", field},   {"topicName", topicName}, {"fieldName", fieldName}};
}

// The real speed from a plant, its estimate and the raw wheel speed from a
// controller's topic.
QJsonObject kalmanChart() {
    return QJsonObject{
        {"series", QJsonArray{QJsonObject{{"name", "speed"}, {"color", "#1f77b4"}},
                              QJsonObject{{"name", "v_kf"}, {"color", "#d62728"}},
                              QJsonObject{{"name", "v_sigma"}, {"color", "#2ca02c"}}}},
        {"bind",
         QJsonObject{{"s0", binding("plant", "0xA1B70FC4", "0x0004", 3, "sim.states", "speed")},
                     {"s1", binding("ctl", "0x42ABDC76", "0x0001", 3, "ctl.diag", "v_kf")},
                     {"s2", binding("ctl", "0x42ABDC76", "0x0001", 7, "ctl.diag", "v_sigma")}}},
        {"xAxis", QJsonObject{{"mode", "samples"}, {"limit", 100}, {"sampleTimeMs", 12.5}}}};
}

}  // namespace

class TestMultiSourceChart : public QObject {
    Q_OBJECT

private slots:
    void requestsEveryTopicOnce() {
        MultiSourceLineChartWidget chart;
        chart.setConfig(kalmanChart());
        QCOMPARE(chart.bindings().size(), 3);
        const QVector<TelemetryTopicRequest> requests = chart.topicRequests();
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requests[0].deviceId, QStringLiteral("plant"));
        QCOMPARE(requests[0].sourceId, 0xA1B70FC4u);
        QCOMPARE(requests[0].topicId, quint16(4));
        QCOMPARE(requests[1].deviceId, QStringLiteral("ctl"));
        QCOMPARE(requests[1].rateMillihz, 80'000u);  // 12.5 ms
    }

    void routesSamplesToTheirSeries() {
        MultiSourceLineChartWidget chart;
        chart.setConfig(kalmanChart());
        // Field 3 on both topics: each goes to its own series only.
        chart.onFieldSample({0xA1B70FC4u, 4, 3}, 1000, 1.5);
        chart.onFieldSample({0x42ABDC76u, 1, 3}, 1000, 1.4);
        chart.onFieldSample({0x42ABDC76u, 1, 7}, 1000, 0.01);
        chart.onFieldSample({0x42ABDC76u, 1, 9}, 1000, 99.0);  // bound to nothing
        chart.onFieldSample({0x12345678u, 4, 3}, 1000, 77.0);  // another source
        QCOMPARE(chart.seriesValues(0), QVector<double>{1.5});
        QCOMPARE(chart.seriesValues(1), QVector<double>{1.4});
        QCOMPARE(chart.seriesValues(2), QVector<double>{0.01});
    }

    void unboundSeriesAskForNothing() {
        QJsonObject config = kalmanChart();
        QJsonObject bind = config.value("bind").toObject();
        bind.remove("s1");
        bind.remove("s2");
        config.insert("bind", bind);
        MultiSourceLineChartWidget chart;
        chart.setConfig(config);
        QCOMPARE(chart.topicRequests().size(), 1);
        chart.onFieldSample({0x42ABDC76u, 1, 3}, 1000, 1.4);
        QVERIFY(chart.seriesValues(1).isEmpty());
    }

    void editorKeepsTheBindings() {
        MultiSourceChartConfigEditor editor;
        // The plant's catalog only: the controller offline.
        DeviceOption plant;
        plant.id = "plant";
        plant.name = "Plant";
        CatalogTopicInfo states;
        states.sourceId = 0xA1B70FC4u;
        states.topicId = 4;
        states.name = "sim.states";
        states.fields = {{1, "x", "float64", "m"}, {3, "speed", "float64", "m/s"}};
        plant.catalogTopics = {states};
        DeviceOption ctl;
        ctl.id = "ctl";
        ctl.name = "Controller";
        editor.setAvailableDevices({plant, ctl});
        editor.setConfig(kalmanChart());
        QSignalSpy changed(&editor, &WidgetConfigEditor::configChanged);
        QCOMPARE(changed.count(), 0);  // setConfig() is one way

        const QJsonObject out = editor.config();
        QCOMPARE(out.value("series").toArray().size(), 3);
        const QJsonObject bind = out.value("bind").toObject();
        QCOMPARE(bind.value("s0").toObject().value("fieldName").toString(), QStringLiteral("speed"));
        QCOMPARE(bind.value("s0").toObject().value("deviceId").toString(), QStringLiteral("plant"));
        // Offline, the controller's binding stays as it was, by name.
        QCOMPARE(bind.value("s1").toObject().value("fieldName").toString(), QStringLiteral("v_kf"));
        QCOMPARE(bind.value("s1").toObject().value("topicName").toString(),
                 QStringLiteral("ctl.diag"));
        QCOMPARE(out.value("xAxis").toObject().value("limit").toInt(), 100);
        // Its field combo offers the catalog's fields, the bound one picked.
        const QList<QComboBox*> combos = editor.findChildren<QComboBox*>();
        bool foundSpeed = false;
        for (QComboBox* combo : combos) {
            if (combo->currentText() == QLatin1String("speed [m/s]")) {
                foundSpeed = true;
            }
        }
        QVERIFY(foundSpeed);
    }

    void registered() {
        std::unique_ptr<DashboardWidget> widget(
            WidgetRegistry::instance().create(QStringLiteral("multi_line"), nullptr));
        QVERIFY(widget);
        QVERIFY(dynamic_cast<TelemetryConsumer*>(widget.get()));
    }
};

QTEST_MAIN(TestMultiSourceChart)
#include "test_multisourcechart.moc"
