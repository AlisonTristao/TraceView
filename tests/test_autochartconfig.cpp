#include <QComboBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgets/chartconfigeditor.h"
#include "dashboard/widgets/gaugeconfigeditor.h"
#include "telemetry/catalogtopicinfo.h"

using traceview::CatalogTopicField;
using traceview::CatalogTopicInfo;
using traceview::ChartConfigEditor;
using traceview::DeviceOption;
using traceview::GaugeConfigEditor;

namespace {

constexpr quint32 kSource = 0x11111111;
constexpr quint16 kTopic = 0x0101;
constexpr int kFieldIdColumn = 1;  // same column in the chart and gauge series tables

CatalogTopicField field(quint16 id, const QString& name, const QString& type,
                        const QString& unit = QString(), double minValue = qQNaN(),
                        double maxValue = qQNaN()) {
    CatalogTopicField out;
    out.fieldId = id;
    out.name = name;
    out.type = type;
    out.unit = unit;
    out.minValue = minValue;
    out.maxValue = maxValue;
    return out;
}

DeviceOption deviceWith(const QVector<CatalogTopicField>& fields) {
    CatalogTopicInfo topic;
    topic.sourceId = kSource;
    topic.topicId = kTopic;
    topic.schemaVersion = 1;
    topic.name = "robot.sensors";
    topic.encoding = "PACKED_LE";
    topic.fields = fields;

    DeviceOption device;
    device.id = "robot";
    device.name = "Robot";
    device.selfSourceId = kSource;
    device.catalogTopics = {topic};
    return device;
}

QJsonObject boundConfig(const QJsonArray& series = {}, const QJsonObject& yAxis = {}) {
    QJsonObject config{{"deviceId", "robot"},
                       {"sourceId", "0x11111111"},
                       {"topicId", "0x0101"},
                       {"series", series}};
    if (!yAxis.isEmpty()) {
        config["yAxis"] = yAxis;
    }
    return config;
}

QJsonObject seriesAt(const QJsonObject& config, int index) {
    return config.value("series").toArray().at(index).toObject();
}

QJsonObject yAxisOf(const QJsonObject& config) {
    return config.value("yAxis").toObject();
}

// The Field ID combo of series row `row`, in either editor's table.
QComboBox* fieldCombo(QWidget& editor, int row) {
    auto* table = editor.findChild<QTableWidget*>();
    return table ? qobject_cast<QComboBox*>(table->cellWidget(row, kFieldIdColumn)) : nullptr;
}

// What picking `fieldId` in the dropdown does.
void pickField(QComboBox* combo, int fieldId) {
    const int index = combo->findData(fieldId);
    QVERIFY(index >= 0);
    combo->setCurrentIndex(index);
    emit combo->activated(index);
}

void clickAddAllFields(ChartConfigEditor& editor) {
    auto* button = editor.findChild<QPushButton*>("addAllFieldsButton");
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    button->click();
}

}  // namespace

class TestAutoChartConfig : public QObject {
    Q_OBJECT

private slots:
    void addAllFieldsMixedUnitsUsesAutomaticAxes();
    void addAllFieldsSingleUnitPresetsYRange();
    void addAllFieldsSingleUnitWithoutRangesKeepsAutoRange();
    void addAllFieldsSkipsDuplicates();
    void addAllFieldsKeepsAUsersFixedRange();
    void addAllFieldsNeedsFieldsInTheCatalog();
    void pickingAChartFieldPrefillsUnitAndRange();
    void pickingAChartFieldKeepsWhatTheUserSet();
    void pickingAGaugeFieldPrefillsUnitAndRange();
    void pickingAGaugeFieldKeepsWhatTheUserSet();
};

void TestAutoChartConfig::addAllFieldsMixedUnitsUsesAutomaticAxes() {
    ChartConfigEditor editor;
    editor.setAvailableDevices({deviceWith({
        field(1, "vbat", "float32", "V", 0.0, 5.0),
        field(2, "temp", "float32", "degC", -40.0, 125.0),
        field(3, "rpm", "uint16", "rpm"),
        field(4, "samples", "float32[8]", "V"),  // fixed array: not one number
        field(5, "burst", "uint8[..32]"),        // variable array: neither
        field(6, "label", "unknown"),            // not a number
        field(7, "state", "enum8"),
    })});
    editor.setConfig(boundConfig());

    clickAddAllFields(editor);

    const QJsonObject config = editor.config();
    const QJsonArray series = config.value("series").toArray();
    QCOMPARE(series.size(), 4);
    QCOMPARE(seriesAt(config, 0).value("name").toString(), QString("vbat"));
    QCOMPARE(seriesAt(config, 0).value("fieldName").toString(), QString("vbat"));
    QCOMPARE(seriesAt(config, 0).value("fieldId").toInt(), 1);
    QCOMPARE(seriesAt(config, 0).value("unit").toString(), QString("V"));
    QCOMPARE(seriesAt(config, 0).value("min").toDouble(), 0.0);
    QCOMPARE(seriesAt(config, 0).value("max").toDouble(), 5.0);
    QCOMPARE(seriesAt(config, 1).value("fieldName").toString(), QString("temp"));
    QCOMPARE(seriesAt(config, 1).value("min").toDouble(), -40.0);
    QCOMPARE(seriesAt(config, 2).value("fieldName").toString(), QString("rpm"));
    QVERIFY(!seriesAt(config, 2).contains("min"));  // not declared: key absent
    QCOMPARE(seriesAt(config, 3).value("fieldName").toString(), QString("state"));
    // The topic is bound by name as well.
    QCOMPARE(config.value("topicName").toString(), QString("robot.sensors"));
    // Several units: one axis per unit, the shared Y axis stays out of it.
    QVERIFY(yAxisOf(config).value("autoAxis").toBool());
    QCOMPARE(yAxisOf(config).value("mode").toString(), QString("auto"));
}

void TestAutoChartConfig::addAllFieldsSingleUnitPresetsYRange() {
    ChartConfigEditor editor;
    editor.setAvailableDevices({deviceWith({
        field(1, "left", "float32", "V", 0.0, 5.0),
        field(2, "right", "float32", "V", -1.0, 3.3),
    })});
    editor.setConfig(boundConfig());

    clickAddAllFields(editor);

    const QJsonObject yAxis = yAxisOf(editor.config());
    QVERIFY(!yAxis.value("autoAxis").toBool());
    QCOMPARE(yAxis.value("mode").toString(), QString("fixed"));
    QCOMPARE(yAxis.value("min").toDouble(), -1.0);  // the union of the two ranges
    QCOMPARE(yAxis.value("max").toDouble(), 5.0);
    QCOMPARE(yAxis.value("unit").toString(), QString("V"));
}

void TestAutoChartConfig::addAllFieldsSingleUnitWithoutRangesKeepsAutoRange() {
    ChartConfigEditor editor;
    editor.setAvailableDevices({deviceWith({
        field(1, "left", "float32", "V", 0.0, 5.0),
        field(2, "right", "float32", "V"),  // declares no range: the union would clip it
    })});
    editor.setConfig(boundConfig());

    clickAddAllFields(editor);

    const QJsonObject yAxis = yAxisOf(editor.config());
    QCOMPARE(editor.config().value("series").toArray().size(), 2);
    QCOMPARE(yAxis.value("mode").toString(), QString("auto"));
    QVERIFY(!yAxis.value("autoAxis").toBool());
    QCOMPARE(yAxis.value("unit").toString(), QString("V"));
}

void TestAutoChartConfig::addAllFieldsSkipsDuplicates() {
    ChartConfigEditor editor;
    editor.setAvailableDevices({deviceWith({
        field(1, "left", "float32", "V", 0.0, 5.0),
        field(2, "right", "float32", "V", 0.0, 5.0),
        field(3, "gain", "float32", "V", 0.0, 5.0),
    })});
    // "left" is plotted by name -- under a stale id, as after a firmware
    // that numbered its fields differently -- and "right" by id alone, as in
    // a project saved before names existed.
    editor.setConfig(boundConfig(QJsonArray{
        QJsonObject{{"name", "mine"}, {"fieldId", 40}, {"fieldName", "left"}},
        QJsonObject{{"name", "old"}, {"fieldId", 2}},
    }));

    clickAddAllFields(editor);

    QJsonObject config = editor.config();
    QCOMPARE(config.value("series").toArray().size(), 3);
    QCOMPARE(seriesAt(config, 0).value("name").toString(), QString("mine"));
    QCOMPARE(seriesAt(config, 1).value("name").toString(), QString("old"));
    QCOMPARE(seriesAt(config, 2).value("fieldName").toString(), QString("gain"));

    // Nothing left to add: a second click changes nothing.
    clickAddAllFields(editor);
    QCOMPARE(editor.config(), config);
}

void TestAutoChartConfig::addAllFieldsKeepsAUsersFixedRange() {
    ChartConfigEditor editor;
    editor.setAvailableDevices({deviceWith({
        field(1, "left", "float32", "V", 0.0, 5.0),
        field(2, "right", "float32", "V", 0.0, 12.0),
    })});
    editor.setConfig(boundConfig(
        {}, QJsonObject{{"mode", "fixed"}, {"min", 2.0}, {"max", 4.0}, {"unit", "volts"}}));

    clickAddAllFields(editor);

    const QJsonObject yAxis = yAxisOf(editor.config());
    QCOMPARE(yAxis.value("mode").toString(), QString("fixed"));
    QCOMPARE(yAxis.value("min").toDouble(), 2.0);
    QCOMPARE(yAxis.value("max").toDouble(), 4.0);
    QCOMPARE(yAxis.value("unit").toString(), QString("volts"));
    QCOMPARE(editor.config().value("series").toArray().size(), 2);
}

void TestAutoChartConfig::addAllFieldsNeedsFieldsInTheCatalog() {
    ChartConfigEditor editor;
    editor.setConfig(boundConfig());
    auto* button = editor.findChild<QPushButton*>("addAllFieldsButton");
    QVERIFY(button);
    QVERIFY(!button->isEnabled());  // no device has reported anything

    // A topic with only arrays has nothing a line or bar chart can plot.
    editor.setAvailableDevices({deviceWith({field(1, "samples", "float32[8]", "V")})});
    editor.setConfig(boundConfig());
    QVERIFY(!button->isEnabled());

    editor.setAvailableDevices(
        {deviceWith({field(1, "samples", "float32[8]", "V"), field(2, "level", "float32", "V")})});
    editor.setConfig(boundConfig());
    QVERIFY(button->isEnabled());
}

void TestAutoChartConfig::pickingAChartFieldPrefillsUnitAndRange() {
    ChartConfigEditor editor;
    editor.setAvailableDevices({deviceWith(
        {field(1, "vbat", "float32", "V", 0.0, 5.0), field(2, "rpm", "uint16", "rpm")})});
    // One series on a field the device doesn't know (id 99).
    editor.setConfig(boundConfig(QJsonArray{QJsonObject{{"name", "s"}, {"fieldId", 99}}}));
    QVERIFY(yAxisOf(editor.config()).value("unit").toString().isEmpty());

    QSignalSpy changed(&editor, &ChartConfigEditor::configChanged);
    QComboBox* combo = fieldCombo(editor, 0);
    QVERIFY(combo);
    pickField(combo, 1);

    // One configChanged() for the whole pick, carrying everything it filled.
    QCOMPARE(changed.count(), 1);
    QJsonObject config = editor.config();
    QCOMPARE(seriesAt(config, 0).value("unit").toString(), QString("V"));
    QCOMPARE(seriesAt(config, 0).value("max").toDouble(), 5.0);
    QCOMPARE(yAxisOf(config).value("unit").toString(), QString("V"));
    // The range is ready for "Fixed" without changing the mode behind the
    // user's back (Auto already honours the declared range).
    QCOMPARE(yAxisOf(config).value("mode").toString(), QString("auto"));
    QCOMPARE(yAxisOf(config).value("min").toDouble(), 0.0);
    QCOMPARE(yAxisOf(config).value("max").toDouble(), 5.0);

    // Typing the id of a field works the same way.
    ChartConfigEditor typed;
    typed.setAvailableDevices({deviceWith({field(1, "temp", "float32", "degC", -40.0, 125.0)})});
    typed.setConfig(boundConfig(QJsonArray{QJsonObject{{"name", "s"}, {"fieldId", 99}}}));
    QComboBox* typedCombo = fieldCombo(typed, 0);
    QVERIFY(typedCombo);
    typedCombo->lineEdit()->setText("1");
    emit typedCombo->lineEdit()->editingFinished();
    config = typed.config();
    QCOMPARE(yAxisOf(config).value("unit").toString(), QString("degC"));
    QCOMPARE(yAxisOf(config).value("min").toDouble(), -40.0);
    QCOMPARE(yAxisOf(config).value("max").toDouble(), 125.0);
}

void TestAutoChartConfig::pickingAChartFieldKeepsWhatTheUserSet() {
    const QVector<DeviceOption> devices = {
        deviceWith({field(1, "vbat", "float32", "V", 0.0, 5.0)})};
    const QJsonArray series{QJsonObject{{"name", "s"}, {"fieldId", 99}}};

    // A unit typed by hand, and a Fixed range.
    ChartConfigEditor fixed;
    fixed.setAvailableDevices(devices);
    fixed.setConfig(boundConfig(
        series, QJsonObject{{"mode", "fixed"}, {"min", 10.0}, {"max", 20.0}, {"unit", "mV"}}));
    pickField(fieldCombo(fixed, 0), 1);
    QJsonObject yAxis = yAxisOf(fixed.config());
    QCOMPARE(yAxis.value("unit").toString(), QString("mV"));
    QCOMPARE(yAxis.value("mode").toString(), QString("fixed"));
    QCOMPARE(yAxis.value("min").toDouble(), 10.0);
    QCOMPARE(yAxis.value("max").toDouble(), 20.0);

    // A range left behind in Auto mode is still the user's.
    ChartConfigEditor autoMode;
    autoMode.setAvailableDevices(devices);
    autoMode.setConfig(
        boundConfig(series, QJsonObject{{"mode", "auto"}, {"min", 10.0}, {"max", 20.0}}));
    pickField(fieldCombo(autoMode, 0), 1);
    yAxis = yAxisOf(autoMode.config());
    QCOMPARE(yAxis.value("min").toDouble(), 10.0);
    QCOMPARE(yAxis.value("max").toDouble(), 20.0);
    QCOMPARE(yAxis.value("unit").toString(), QString("V"));  // the empty unit did get filled

    // With automatic axes the unit comes from the catalog per series, and the
    // shared Y axis settings don't apply: nothing there is touched.
    ChartConfigEditor perUnit;
    perUnit.setAvailableDevices(devices);
    perUnit.setConfig(boundConfig(series, QJsonObject{{"autoAxis", true}}));
    pickField(fieldCombo(perUnit, 0), 1);
    yAxis = yAxisOf(perUnit.config());
    QVERIFY(yAxis.value("unit").toString().isEmpty());
    QCOMPARE(yAxis.value("max").toDouble(), 100.0);
}

void TestAutoChartConfig::pickingAGaugeFieldPrefillsUnitAndRange() {
    GaugeConfigEditor editor;
    editor.setAvailableDevices({deviceWith({field(1, "vbat", "float32", "V", 0.0, 5.0)})});
    editor.setConfig(boundConfig(QJsonArray{QJsonObject{{"name", "r"}, {"fieldId", 99}}}));

    QSignalSpy changed(&editor, &GaugeConfigEditor::configChanged);
    pickField(fieldCombo(editor, 0), 1);

    QCOMPARE(changed.count(), 1);
    const QJsonObject config = editor.config();
    QCOMPARE(config.value("unit").toString(), QString("V"));
    QCOMPARE(config.value("min").toDouble(), 0.0);
    QCOMPARE(config.value("max").toDouble(), 5.0);
}

void TestAutoChartConfig::pickingAGaugeFieldKeepsWhatTheUserSet() {
    const QVector<DeviceOption> devices = {
        deviceWith({field(1, "vbat", "float32", "V", 0.0, 5.0),
                    field(2, "temp", "float32", "degC", -40.0, 125.0), field(3, "raw", "uint16")})};

    GaugeConfigEditor editor;
    editor.setAvailableDevices(devices);
    QJsonObject config = boundConfig(QJsonArray{QJsonObject{{"name", "r"}, {"fieldId", 99}}});
    config["unit"] = "mV";
    config["min"] = 10.0;
    config["max"] = 20.0;
    editor.setConfig(config);

    pickField(fieldCombo(editor, 0), 1);
    config = editor.config();
    QCOMPARE(config.value("unit").toString(), QString("mV"));
    QCOMPARE(config.value("min").toDouble(), 10.0);
    QCOMPARE(config.value("max").toDouble(), 20.0);

    // A field that declares nothing fills nothing in.
    GaugeConfigEditor bare;
    bare.setAvailableDevices(devices);
    bare.setConfig(boundConfig(QJsonArray{QJsonObject{{"name", "r"}, {"fieldId", 99}}}));
    pickField(fieldCombo(bare, 0), 3);
    config = bare.config();
    QVERIFY(config.value("unit").toString().isEmpty());
    QCOMPARE(config.value("min").toDouble(), 0.0);
    QCOMPARE(config.value("max").toDouble(), 100.0);
}

QTEST_MAIN(TestAutoChartConfig)
#include "test_autochartconfig.moc"
