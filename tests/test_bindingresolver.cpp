#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "dashboard/bindingresolver.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/chartconfigeditor.h"
#include "dashboard/widgets/chartdata.h"
#include "dashboard/widgets/chartwidgets.h"
#include "project/projectstore.h"
#include "telemetry/catalogtopicinfo.h"

using traceview::CatalogTopicField;
using traceview::CatalogTopicInfo;
using traceview::ChartConfigEditor;
using traceview::ChartWidgetBase;
using traceview::DashboardGrid;
using traceview::ProjectStore;
using traceview::resolveBindingNames;

namespace {

constexpr quint32 kSavedSource = 0x11111111;  // the device the dashboard was built on
constexpr quint32 kOtherSource = 0x22222222;  // a simulated twin, another MAC

CatalogTopicInfo topic(quint32 sourceId, quint16 topicId, const QString& name,
                       const QVector<QPair<quint16, QString>>& fields) {
    CatalogTopicInfo info;
    info.sourceId = sourceId;
    info.topicId = topicId;
    info.schemaVersion = 1;
    info.name = name;
    for (const auto& [fieldId, fieldName] : fields) {
        CatalogTopicField field;
        field.fieldId = fieldId;
        field.name = fieldName;
        field.type = QStringLiteral("float32");
        info.fields.append(field);
    }
    return info;
}

QJsonObject series(int fieldId, const QString& fieldName = QString()) {
    QJsonObject object{{"name", QStringLiteral("s%1").arg(fieldId)}, {"fieldId", fieldId}};
    if (!fieldName.isEmpty()) {
        object["fieldName"] = fieldName;
    }
    return object;
}

// A chart bound to "imu".ax / "imu".gz as built on kSavedSource, where the
// firmware numbered imu 0x0101 and its fields 1 and 2.
QJsonObject namedChartConfig() {
    return {{"deviceId", "robot"},
            {"sourceId", "0x11111111"},
            {"topicId", "0x0101"},
            {"topicName", "imu"},
            {"series", QJsonArray{series(1, "ax"), series(2, "gz")}}};
}

QJsonObject chartItem(const QJsonObject& config) {
    return {
        {"id", "chart-1"}, {"type", "dummy_line"}, {"key", "chart"}, {"config", config}, {"x", 0.0},
        {"y", 0.0},        {"width", 0.5},         {"height", 0.5}};
}

QJsonObject dashboardWith(const QJsonObject& config) {
    return {{"items", QJsonArray{chartItem(config)}}};
}

QJsonObject itemConfig(const DashboardGrid& grid) {
    return grid.toJson().value("items").toArray().first().toObject().value("config").toObject();
}

int seriesFieldId(const QJsonObject& config, int index) {
    return config.value("series").toArray().at(index).toObject().value("fieldId").toInt();
}

QString seriesFieldName(const QJsonObject& config, int index) {
    return config.value("series").toArray().at(index).toObject().value("fieldName").toString();
}

class TestBindingResolver : public QObject {
    Q_OBJECT

private slots:
    void persistenceRoundTripKeepsNamesAndIds();
    void chartEditorRoundTripKeepsNames();
    void namesRebindAgainstCatalogWithOtherIds();
    void gridBindsWidgetByNameToTheNewIds();
    void ownSourceWinsWhenSeveralPublishTheName();
    void idsOnlyBindingGainsNames();
    void unresolvableNamesFallBackToIds();
    void resolveBindingsRepointsLiveItemsOffTheUndoStack();
    void declaredRangeFollowsTheCatalog();
};

void TestBindingResolver::persistenceRoundTripKeepsNamesAndIds() {
    DashboardGrid source;
    source.fromJson(dashboardWith(namedChartConfig()));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("names.tvproj");
    ProjectStore& store = ProjectStore::instance();
    store.reset();
    store.setSection("dashboard", source.toJson());
    QVERIFY(store.saveAs(path));
    store.reset();
    QVERIFY(store.load(path));

    DashboardGrid loaded;
    loaded.fromJson(store.section("dashboard"));
    const QJsonObject config = itemConfig(loaded);
    QCOMPARE(config.value("topicName").toString(), QString("imu"));
    QCOMPARE(config.value("sourceId").toString(), QString("0x11111111"));
    QCOMPARE(config.value("topicId").toString(), QString("0x0101"));
    QCOMPARE(seriesFieldId(config, 0), 1);
    QCOMPARE(seriesFieldName(config, 0), QString("ax"));
    QCOMPARE(seriesFieldId(config, 1), 2);
    QCOMPARE(seriesFieldName(config, 1), QString("gz"));
    store.reset();
}

void TestBindingResolver::chartEditorRoundTripKeepsNames() {
    // The properties panel rebuilds a config from its own widgets on every
    // edit -- the names must survive that even with no catalog to look at.
    ChartConfigEditor editor;
    editor.setConfig(namedChartConfig());
    const QJsonObject config = editor.config();
    QCOMPARE(config.value("topicName").toString(), QString("imu"));
    QCOMPARE(seriesFieldName(config, 0), QString("ax"));
    QCOMPARE(seriesFieldName(config, 1), QString("gz"));
}

void TestBindingResolver::namesRebindAgainstCatalogWithOtherIds() {
    // Same topic and field names, every id different: another MAC, and a
    // firmware that declared things in another order.
    const QVector<CatalogTopicInfo> catalog = {
        topic(kOtherSource, 0x0101, "battery", {{1, "voltage"}}),
        topic(kOtherSource, 0x0105, "imu", {{3, "gz"}, {4, "ax"}}),
    };
    const QJsonObject resolved = resolveBindingNames(namedChartConfig(), catalog, kOtherSource);
    QCOMPARE(resolved.value("sourceId").toString(), QString("0x22222222"));
    QCOMPARE(resolved.value("topicId").toString(), QString("0x0105"));
    QCOMPARE(resolved.value("topicName").toString(), QString("imu"));
    QCOMPARE(seriesFieldId(resolved, 0), 4);
    QCOMPARE(seriesFieldName(resolved, 0), QString("ax"));
    QCOMPARE(seriesFieldId(resolved, 1), 3);
    QCOMPARE(seriesFieldName(resolved, 1), QString("gz"));
    // Everything that isn't the binding is left alone.
    QCOMPARE(resolved.value("deviceId").toString(), QString("robot"));
    QCOMPARE(resolved.value("series").toArray().at(0).toObject().value("name").toString(),
             QString("s1"));
}

void TestBindingResolver::gridBindsWidgetByNameToTheNewIds() {
    const QVector<CatalogTopicInfo> catalog = {
        topic(kOtherSource, 0x0105, "imu", {{3, "gz"}, {4, "ax"}}),
    };
    DashboardGrid grid;
    grid.setBindingResolver([&catalog](const QJsonObject& config) {
        return resolveBindingNames(config, catalog, kOtherSource);
    });
    grid.fromJson(dashboardWith(namedChartConfig()));

    auto* chart = dynamic_cast<ChartWidgetBase*>(grid.keyedWidgets().value("chart"));
    QVERIFY(chart);
    QCOMPARE(chart->config().sourceId, kOtherSource);
    QCOMPARE(chart->config().topicId, quint16(0x0105));

    // A sample under the new ids lands in the series that names that field
    // (the same appendFieldSample() bookkeeping the widget's own
    // onFieldSample() runs once the source/topic match).
    const traceview::ChartConfig& parsed = chart->config();
    QVector<traceview::TelemetrySeriesBuffer> buffers = traceview::resizeChartBuffers({}, parsed);
    traceview::appendFieldSample(buffers, parsed, 4, 1000, 1.5);
    traceview::appendFieldSample(buffers, parsed, 3, 1000, -2.0);
    traceview::appendFieldSample(buffers, parsed, 1, 2000, 99.0);  // the old "ax" id
    QCOMPARE(buffers.size(), 2);
    QCOMPARE(buffers[0].values(), QVector<double>{1.5});
    QCOMPARE(buffers[1].values(), QVector<double>{-2.0});
}

void TestBindingResolver::ownSourceWinsWhenSeveralPublishTheName() {
    // A hub's catalog can describe several robots running the same firmware.
    const QVector<CatalogTopicInfo> catalog = {
        topic(0x33333333, 0x0101, "imu", {{1, "ax"}, {2, "gz"}}),
        topic(kOtherSource, 0x0102, "imu", {{1, "ax"}, {2, "gz"}}),
    };
    const QJsonObject resolved = resolveBindingNames(namedChartConfig(), catalog, kOtherSource);
    QCOMPARE(resolved.value("sourceId").toString(), QString("0x22222222"));
    QCOMPARE(resolved.value("topicId").toString(), QString("0x0102"));

    // No preference at all between two sources is not ours to guess.
    QCOMPARE(resolveBindingNames(namedChartConfig(), catalog, 0), namedChartConfig());
}

void TestBindingResolver::idsOnlyBindingGainsNames() {
    // A project saved before names existed: ids only, plus a field id the
    // catalog has never heard of.
    const QJsonObject idsOnly{{"deviceId", "robot"},
                              {"sourceId", "0x11111111"},
                              {"topicId", "0x0101"},
                              {"series", QJsonArray{series(1), series(2), series(9)}}};
    const QVector<CatalogTopicInfo> catalog = {
        topic(kSavedSource, 0x0101, "imu", {{1, "ax"}, {2, "gz"}}),
    };
    const QJsonObject migrated = resolveBindingNames(idsOnly, catalog, 0);
    QCOMPARE(migrated.value("topicName").toString(), QString("imu"));
    QCOMPARE(migrated.value("sourceId").toString(), QString("0x11111111"));
    QCOMPARE(migrated.value("topicId").toString(), QString("0x0101"));
    QCOMPARE(seriesFieldName(migrated, 0), QString("ax"));
    QCOMPARE(seriesFieldName(migrated, 1), QString("gz"));
    // Unknown: keeps its id, gains nothing, and isn't dropped.
    QCOMPARE(seriesFieldId(migrated, 2), 9);
    QVERIFY(!migrated.value("series").toArray().at(2).toObject().contains("fieldName"));

    // The audio widget's named field keys and a pre-multi-ring gauge's
    // top-level fieldId get their names the same way.
    const QJsonObject audio{{"sourceId", "0x11111111"},
                            {"topicId", "0x0101"},
                            {"seqFieldId", 1},
                            {"samplesFieldId", 2},
                            {"fieldId", 1}};
    const QJsonObject audioMigrated = resolveBindingNames(audio, catalog, 0);
    QCOMPARE(audioMigrated.value("seqFieldName").toString(), QString("ax"));
    QCOMPARE(audioMigrated.value("samplesFieldName").toString(), QString("gz"));
    QCOMPARE(audioMigrated.value("fieldName").toString(), QString("ax"));

    // Through the grid, as at project open: the next save writes the names.
    DashboardGrid grid;
    grid.setBindingResolver(
        [&catalog](const QJsonObject& config) { return resolveBindingNames(config, catalog, 0); });
    grid.fromJson(dashboardWith(idsOnly));
    QCOMPARE(itemConfig(grid), migrated);
}

void TestBindingResolver::unresolvableNamesFallBackToIds() {
    const QVector<CatalogTopicInfo> catalog = {
        topic(kOtherSource, 0x0105, "imu", {{4, "ax"}}),
        topic(kOtherSource, 0x0106, "odometry", {{1, "x"}}),
    };

    // A topic name the device doesn't publish: the ids stand, untouched.
    QJsonObject missingTopic = namedChartConfig();
    missingTopic["topicName"] = "camera";
    QCOMPARE(resolveBindingNames(missingTopic, catalog, kOtherSource), missingTopic);

    // The topic resolves but one field name doesn't: that series keeps its
    // id (and name) while the other follows its name.
    const QJsonObject partly = resolveBindingNames(namedChartConfig(), catalog, kOtherSource);
    QCOMPARE(partly.value("topicId").toString(), QString("0x0105"));
    QCOMPARE(seriesFieldId(partly, 0), 4);
    QCOMPARE(seriesFieldId(partly, 1), 2);
    QCOMPARE(seriesFieldName(partly, 1), QString("gz"));

    // Ids-only and unknown ids, no catalog at all, a widget with no topic:
    // nothing to do, nothing lost.
    const QJsonObject unknownIds{
        {"sourceId", "0x11111111"}, {"topicId", "0x0777"}, {"series", QJsonArray{series(1)}}};
    QCOMPARE(resolveBindingNames(unknownIds, catalog, kOtherSource), unknownIds);
    QCOMPARE(resolveBindingNames(namedChartConfig(), {}, kOtherSource), namedChartConfig());
    const QJsonObject button{{"deviceId", "robot"}, {"command", "go"}};
    QCOMPARE(resolveBindingNames(button, catalog, kOtherSource), button);
}

void TestBindingResolver::resolveBindingsRepointsLiveItemsOffTheUndoStack() {
    QVector<CatalogTopicInfo> catalog;  // device not connected yet
    DashboardGrid grid;
    grid.setBindingResolver([&catalog](const QJsonObject& config) {
        return resolveBindingNames(config, catalog, kOtherSource);
    });
    grid.fromJson(dashboardWith(namedChartConfig()));
    QCOMPARE(itemConfig(grid), namedChartConfig());
    QVERIFY(!grid.resolveBindings());

    // An edit made while offline: its undo entry remembers the saved ids.
    grid.selectItem("chart-1");
    QJsonObject edited = namedChartConfig();
    edited["xAxis"] = QJsonObject{{"mode", "samples"}, {"limit", 50}};
    grid.changeSelectedConfig(edited);
    QCOMPARE(grid.undoStack()->count(), 1);

    // The device connects and announces its manifest.
    catalog = {topic(kOtherSource, 0x0105, "imu", {{3, "gz"}, {4, "ax"}})};
    QVERIFY(grid.resolveBindings());
    QCOMPARE(grid.undoStack()->count(), 1);  // not an edit
    const QJsonObject config = itemConfig(grid);
    QCOMPARE(config.value("topicId").toString(), QString("0x0105"));
    QCOMPARE(seriesFieldId(config, 0), 4);
    auto* chart = dynamic_cast<ChartWidgetBase*>(grid.keyedWidgets().value("chart"));
    QVERIFY(chart);
    QCOMPARE(chart->config().topicId, quint16(0x0105));
    QVERIFY(!grid.resolveBindings());  // idempotent

    // Undo goes back through the resolver too: the edit is reverted, the
    // stale ids it remembered are not.
    grid.undoStack()->undo();
    QVERIFY(!itemConfig(grid).contains("xAxis"));
    QCOMPARE(itemConfig(grid).value("topicId").toString(), QString("0x0105"));
    QCOMPARE(seriesFieldId(itemConfig(grid), 1), 3);
}

void TestBindingResolver::declaredRangeFollowsTheCatalog() {
    const auto catalogWith = [](double axMin, double axMax, double gzMin, double gzMax) {
        CatalogTopicInfo imu = topic(kSavedSource, 0x0101, "imu", {{1, "ax"}, {2, "gz"}});
        imu.fields[0].unit = "g";
        imu.fields[0].minValue = axMin;
        imu.fields[0].maxValue = axMax;
        imu.fields[1].unit = "g";
        imu.fields[1].minValue = gzMin;
        imu.fields[1].maxValue = gzMax;
        return QVector<CatalogTopicInfo>{imu};
    };
    const auto seriesAt = [](const QJsonObject& config, int index) {
        return config.value("series").toArray().at(index).toObject();
    };

    // A chart made before the device declared any range gains it.
    QVector<CatalogTopicInfo> catalog = catalogWith(-2.0, 2.0, -4.0, 8.0);
    QJsonObject config = resolveBindingNames(namedChartConfig(), catalog, kSavedSource);
    QCOMPARE(seriesAt(config, 0).value("unit").toString(), QString("g"));
    QCOMPARE(seriesAt(config, 0).value("min").toDouble(), -2.0);
    QCOMPARE(seriesAt(config, 1).value("max").toDouble(), 8.0);
    QVERIFY(!config.contains("yAxis"));

    // The range the editor preset from the declaration follows it...
    config["yAxis"] = QJsonObject{{"mode", "fixed"}, {"min", -4.0}, {"max", 8.0}};
    catalog = catalogWith(-2.0, 2.0, -16.0, 16.0);
    DashboardGrid grid;
    grid.setBindingResolver([&catalog](const QJsonObject& in) {
        return resolveBindingNames(in, catalog, kSavedSource);
    });
    catalog = catalogWith(-2.0, 2.0, -4.0, 8.0);
    grid.fromJson(dashboardWith(config));
    QVERIFY(!grid.resolveBindings());
    catalog = catalogWith(-2.0, 2.0, -16.0, 16.0);
    QVERIFY(grid.resolveBindings());
    QVERIFY(!grid.resolveBindings());  // idempotent
    QJsonObject live = itemConfig(grid);
    QCOMPARE(seriesAt(live, 1).value("min").toDouble(), -16.0);
    QCOMPARE(live.value("yAxis").toObject().value("min").toDouble(), -16.0);
    QCOMPARE(live.value("yAxis").toObject().value("max").toDouble(), 16.0);
    auto* chart = dynamic_cast<ChartWidgetBase*>(grid.keyedWidgets().value("chart"));
    QVERIFY(chart);
    QCOMPARE(chart->config().series[1].declaredMax, 16.0);
    QCOMPARE(chart->config().yMax, 16.0);

    // ...a range the user typed does not.
    config["yAxis"] = QJsonObject{{"mode", "fixed"}, {"min", -1.0}, {"max", 1.0}};
    const QJsonObject typed = resolveBindingNames(config, catalog, kSavedSource);
    QCOMPARE(seriesAt(typed, 1).value("max").toDouble(), 16.0);
    QCOMPARE(typed.value("yAxis").toObject().value("max").toDouble(), 1.0);

    // A range the device stops declaring goes away; a gauge's single range
    // (top-level min/max) is never a series' business.
    catalog = catalogWith(-2.0, 2.0, qQNaN(), qQNaN());
    const QJsonObject undeclared = resolveBindingNames(config, catalog, kSavedSource);
    QVERIFY(!seriesAt(undeclared, 1).contains("min"));
    QVERIFY(!seriesAt(undeclared, 1).contains("max"));
    QJsonObject gauge = namedChartConfig();
    gauge["min"] = 0.0;
    gauge["max"] = 100.0;
    QCOMPARE(resolveBindingNames(gauge, catalog, kSavedSource), gauge);
}

}  // namespace

QTEST_MAIN(TestBindingResolver)
#include "test_bindingresolver.moc"
