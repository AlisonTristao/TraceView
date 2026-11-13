#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "dashboard/scenes/diffdrivescene.h"
#include "dashboard/scenes/furutascene.h"
#include "dashboard/scenes/sceneconfigeditor.h"
#include "dashboard/scenes/sceneregistry.h"
#include "dashboard/scenes/scenewidget.h"
#include "project/projectstore.h"

using namespace traceview;
using traceview::CatalogTopicField;
using traceview::CatalogTopicInfo;
using traceview::DashboardGrid;
using traceview::DeviceOption;
using traceview::ProjectStore;

namespace {

constexpr quint32 kSource = 0x1A2B3C4D;
constexpr quint16 kTruth = 0x0003;
constexpr quint16 kTrack = 0x0009;

QJsonObject truthBinding(int fieldId, const QString& fieldName) {
    return {{"deviceId", "plant"},      {"sourceId", "0x1A2B3C4D"}, {"topicId", "0x0003"},
            {"topicName", "sim.truth"}, {"fieldId", fieldId},       {"fieldName", fieldName}};
}

QJsonObject sceneConfig() {
    QJsonObject bind{{"x", truthBinding(1, "x")},
                     {"y", truthBinding(2, "y")},
                     {"theta", truthBinding(3, "theta")},
                     {"track", QJsonObject{{"deviceId", "plant"},
                                           {"sourceId", "0x1A2B3C4D"},
                                           {"topicId", "0x0009"},
                                           {"topicName", "sim.track"}}}};
    QJsonObject params;
    DiffDriveScene defaults;
    for (const SceneParam& param : defaults.params()) {
        params[param.name] = param.defaultValue;
    }
    params["scale_px_per_m"] = 250.0;
    params["render_radius_m"] = 3.0;
    return {{"scene", "diffdrive"}, {"bind", bind}, {"params", params}};
}

CatalogTopicField field(quint16 id, const QString& name, const QString& type = "float32") {
    CatalogTopicField f;
    f.fieldId = id;
    f.name = name;
    f.type = type;
    return f;
}

QVector<DeviceOption> plantDevice() {
    CatalogTopicInfo truth;
    truth.sourceId = kSource;
    truth.topicId = kTruth;
    truth.name = "sim.truth";
    truth.encoding = "PACKED_LE";
    truth.fields = {field(0, "tick", "uint32"), field(1, "x"), field(2, "y"), field(3, "theta")};
    CatalogTopicInfo preview;
    preview.sourceId = kSource;
    preview.topicId = 0x0004;
    preview.name = "sim.preview";
    preview.encoding = "PACKED_LE";
    preview.fields = {field(0, "tick", "uint32"), field(1, "px", "float32[10]"),
                      field(2, "py", "float32[10]")};
    CatalogTopicInfo track;
    track.sourceId = kSource;
    track.topicId = kTrack;
    track.name = "sim.track";
    track.encoding = "UTF8";
    CatalogTopicInfo other;
    other.sourceId = kSource;
    other.topicId = 0x0007;
    other.name = "telemetry";
    other.encoding = "PACKED_LE";
    other.fields = {field(4, "qps", "uint16")};
    DeviceOption device;
    device.id = "plant";
    device.name = "TraceLab plant";
    device.catalogTopics = {truth, preview, track, other};
    device.selfSourceId = kSource;
    return {device};
}

}  // namespace

class TestSceneWidget : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void editorRoundTripKeepsTypeBindingsAndParams();
    void editorSurvivesItsOwnToggle();
    void widgetAppliesConfig();
    void projectRoundTripStoresNoCode();
    void bindByNameMatchesTheCatalog();
    void furutaAnglesBindFromOneTopic();
    void undeclaredInputsAreNotSubscribed();
    void samplesReachTheScene();
    void topicRequestsAreOnePerTopic();
    void keysEmitTheAction();

private:
    QTemporaryDir m_settingsDir;
};

void TestSceneWidget::initTestCase() {
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName("TraceViewTest");
    QCoreApplication::setApplicationName("test_scenewidget");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    // Built in: WidgetRegistry registers the Scene widget and its scenes.
    QVERIFY(!traceview::WidgetRegistry::instance()
                 .displayName(QString::fromLatin1(kSceneWidgetTypeId))
                 .isEmpty());
    QVERIFY(SceneRegistry::instance().contains(QString::fromLatin1(FurutaScene::kTypeId)));
    QVERIFY(SceneRegistry::instance().contains(QString::fromLatin1(DiffDriveScene::kTypeId)));
}

void TestSceneWidget::editorRoundTripKeepsTypeBindingsAndParams() {
    SceneConfigEditor editor;
    QSignalSpy changed(&editor, &traceview::WidgetConfigEditor::configChanged);
    editor.setConfig(sceneConfig());
    // Offline (no devices): the bindings are kept as they are.
    QCOMPARE(editor.config(), sceneConfig());
    editor.setAvailableDevices(plantDevice());
    QCOMPARE(editor.config(), sceneConfig());
    QCOMPARE(changed.count(), 0);
}

// The properties panel answers configChanged() by setting the config back,
// synchronously: a toggle's check box is rebuilt inside its own click and
// must not be deleted under it (this crashed in QCheckBox::checkStateSet).
void TestSceneWidget::editorSurvivesItsOwnToggle() {
    SceneConfigEditor editor;
    QJsonObject config = sceneConfig();
    config["scene"] = QString::fromLatin1(FurutaScene::kTypeId);
    editor.setConfig(config);
    connect(&editor, &traceview::WidgetConfigEditor::configChanged, &editor,
            [&editor] { editor.setConfig(editor.config()); });
    editor.show();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // the first rows
    const QList<QCheckBox*> boxes = editor.findChildren<QCheckBox*>();
    QVERIFY(!boxes.isEmpty());
    int before = 0;
    for (QCheckBox* box : boxes) {
        before += box->isChecked() ? 1 : 0;
    }
    QCheckBox* first = boxes.first();
    const bool was = first->isChecked();
    first->click();  // nextCheckState() -> checkStateSet(), where it crashed
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    // The rebuilt rows carry the toggled value.
    int after = 0;
    const QList<QCheckBox*> rebuilt = editor.findChildren<QCheckBox*>();
    QCOMPARE(rebuilt.size(), boxes.size());
    for (QCheckBox* box : rebuilt) {
        after += box->isChecked() ? 1 : 0;
    }
    QCOMPARE(after, before + (was ? -1 : 1));
}

void TestSceneWidget::widgetAppliesConfig() {
    SceneWidget widget;
    widget.setConfig(sceneConfig());
    QCOMPARE(widget.sceneType(), QString("diffdrive"));
    auto* scene = dynamic_cast<DiffDriveScene*>(widget.renderer());
    QVERIFY(scene);
    QCOMPARE(scene->param("scale_px_per_m"), 250.0);
    QCOMPARE(scene->param("render_radius_m"), 3.0);
    // A param left out goes back to its default.
    QJsonObject config = sceneConfig();
    config["params"] = QJsonObject{};
    widget.setConfig(config);
    QCOMPARE(scene->param("scale_px_per_m"), 300.0);
    QCOMPARE(widget.renderer(), scene);  // same type: same renderer, track kept

    widget.setConfig({{"scene", "no_such_scene"}});
    QVERIFY(!widget.renderer());
    widget.resize(200, 100);
    QVERIFY(!widget.grab().isNull());  // draws its "unknown" note, no crash
}

void TestSceneWidget::projectRoundTripStoresNoCode() {
    const QJsonObject item{
        {"id", "scene-1"}, {"type", "scene"}, {"key", ""},    {"config", sceneConfig()},
        {"x", 0.0},        {"y", 0.0},        {"width", 0.5}, {"height", 0.5}};
    DashboardGrid source;
    source.fromJson({{"items", QJsonArray{item}}});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("scene.tvproj");
    ProjectStore& store = ProjectStore::instance();
    store.reset();
    store.setSection("dashboard", source.toJson());
    QVERIFY(store.saveAs(path));
    store.reset();
    QVERIFY(store.load(path));

    DashboardGrid loaded;
    loaded.fromJson(store.section("dashboard"));
    const QJsonObject saved = loaded.toJson().value("items").toArray().first().toObject();
    QCOMPARE(saved.value("type").toString(), QString("scene"));
    const QJsonObject config = saved.value("config").toObject();
    QCOMPARE(config, sceneConfig());
    // Only type, bindings and parameters: no script, no code of any kind.
    QStringList keys = config.keys();
    keys.sort();
    QCOMPARE(keys, (QStringList{"bind", "params", "scene"}));
    for (const QJsonValue& param : config.value("params").toObject()) {
        QVERIFY(param.isDouble());
    }
    store.reset();
}

void TestSceneWidget::bindByNameMatchesTheCatalog() {
    DiffDriveScene scene;
    const QJsonObject kept{{"theta", truthBinding(9, "heading")}};
    const QJsonObject bind = autoBindSceneInputs(scene.inputs(), kept, plantDevice());

    QCOMPARE(bind.value("x").toObject(), truthBinding(1, "x"));
    QCOMPARE(bind.value("y").toObject(), truthBinding(2, "y"));
    QCOMPARE(bind.value("tick").toObject(), truthBinding(0, "tick"));
    QCOMPARE(bind.value("theta").toObject(), truthBinding(9, "heading"));  // left alone
    // Array inputs take array fields, the text input the UTF8 topic.
    QCOMPARE(bind.value("preview_x").toObject().value("fieldName").toString(), QString("px"));
    QCOMPARE(bind.value("preview_tick").toObject().value("topicName").toString(),
             QString("sim.preview"));
    const QJsonObject track = bind.value("track").toObject();
    QCOMPARE(track.value("topicName").toString(), QString("sim.track"));
    QVERIFY(!track.contains("fieldId"));
    // No default topic, but one field named like the input elsewhere.
    QCOMPARE(bind.value("qps").toObject().value("topicName").toString(), QString("telemetry"));
    // Nothing reported for these.
    QVERIFY(!bind.contains("pred_x"));
    QVERIFY(!bind.contains("next_point"));
}

void TestSceneWidget::furutaAnglesBindFromOneTopic() {
    // A real robot reporting theta1/theta2 in two topics: both angles come
    // from the same one -- the first reported, or the one an angle already
    // reads.
    CatalogTopicInfo control;
    control.sourceId = kSource;
    control.topicId = 0x0101;
    control.name = "controle";
    control.encoding = "PACKED_LE";
    control.fields = {field(1, "t"), field(2, "theta1"), field(3, "theta2"), field(6, "u")};
    CatalogTopicInfo sensors;
    sensors.sourceId = kSource;
    sensors.topicId = 0x0102;
    sensors.name = "sensores";
    sensors.encoding = "PACKED_LE";
    sensors.fields = {field(1, "theta1"), field(2, "theta2"), field(3, "theta2_topo")};
    DeviceOption robot;
    robot.id = "furuta";
    robot.name = "furuta";
    robot.catalogTopics = {control, sensors};
    robot.selfSourceId = kSource;

    FurutaScene scene;
    const QJsonObject bind = autoBindSceneInputs(scene.inputs(), {}, {robot});
    QCOMPARE(bind.size(), 2);
    QCOMPARE(bind.value("theta1").toObject().value("topicName").toString(), QString("controle"));
    QCOMPARE(bind.value("theta2").toObject().value("topicName").toString(), QString("controle"));
    QCOMPARE(bind.value("theta2").toObject().value("fieldId").toInt(), 3);

    const QJsonObject kept{{"theta1", QJsonObject{{"deviceId", "furuta"},
                                                   {"sourceId", "0x1A2B3C4D"},
                                                   {"topicId", "0x0102"},
                                                   {"topicName", "sensores"},
                                                   {"fieldId", 1},
                                                   {"fieldName", "theta1"}}}};
    const QJsonObject follow = autoBindSceneInputs(scene.inputs(), kept, {robot});
    QCOMPARE(follow.value("theta2").toObject().value("topicName").toString(), QString("sensores"));
    QCOMPARE(follow.value("theta2").toObject().value("fieldId").toInt(), 2);
}

void TestSceneWidget::undeclaredInputsAreNotSubscribed() {
    // A project saved when the Furuta scene also read u and t (topic 0x0101):
    // only the angles (topic 0x0102) are subscribed now.
    const auto binding = [](const char* topicId, const char* topicName, int fieldId,
                            const char* fieldName) {
        return QJsonObject{{"deviceId", "furuta"}, {"sourceId", "0x1A2B3C4D"},
                           {"topicId", topicId},   {"topicName", topicName},
                           {"fieldId", fieldId},   {"fieldName", fieldName}};
    };
    const QJsonObject bind{{"theta1", binding("0x0102", "sensores", 1, "theta1")},
                           {"theta2", binding("0x0102", "sensores", 2, "theta2")},
                           {"u", binding("0x0101", "controle", 6, "u")},
                           {"t", binding("0x0101", "controle", 1, "t")}};
    SceneWidget widget;
    widget.setConfig({{"scene", QString::fromLatin1(FurutaScene::kTypeId)}, {"bind", bind}});
    const QVector<traceview::TelemetryTopicRequest> requests = widget.topicRequests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests.first().topicId, quint16(0x0102));
}

void TestSceneWidget::samplesReachTheScene() {
    SceneWidget widget;
    widget.resize(640, 360);
    widget.setConfig(sceneConfig());
    widget.onFieldSample({kSource, kTruth, 1, 0}, 1000, 1.5);
    widget.onFieldSample({kSource, kTruth, 2, 0}, 1000, -0.5);
    widget.onFieldSample({kSource, kTruth, 3, 0}, 1000, 0.25);
    widget.onFieldSample({kSource, 0x0099, 1, 0}, 1000, 99.0);  // not bound
    widget.onTextSample(kSource, kTrack, 1000,
                        R"({"points": [[0, 0], [0.01, 0], [0.02, 0]], "closed": false})");
    widget.grab();  // one frame: update() + paint()

    auto* scene = dynamic_cast<DiffDriveScene*>(widget.renderer());
    QVERIFY(scene);
    QCOMPARE(scene->pose().x, 1.5);
    QCOMPARE(scene->pose().y, -0.5);
    QCOMPARE(scene->pose().theta, 0.25);
    QCOMPARE(scene->trackPointCount(), 3);
}

void TestSceneWidget::topicRequestsAreOnePerTopic() {
    SceneWidget widget;
    widget.setConfig(sceneConfig());
    const QVector<traceview::TelemetryTopicRequest> requests = widget.topicRequests();
    QCOMPARE(requests.size(), 2);  // sim.truth (x, y, theta) and sim.track
    for (const traceview::TelemetryTopicRequest& request : requests) {
        QCOMPARE(request.deviceId, QString("plant"));
        QCOMPARE(request.sourceId, kSource);
        QVERIFY(request.topicId == kTruth || request.topicId == kTrack);
    }
}

void TestSceneWidget::keysEmitTheAction() {
    SceneWidget widget;
    widget.setConfig(sceneConfig());
    QSignalSpy spy(&widget, &SceneWidget::actionRequested);
    QTest::keyClick(&widget, Qt::Key_P);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("sim.disturb"));
    QCOMPARE(spy.at(0).at(1).toDouble(), 5.0);
}

QTEST_MAIN(TestSceneWidget)
#include "test_scenewidget.moc"
