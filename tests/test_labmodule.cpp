#include <QAction>
#include <QFile>
#include <QJsonObject>
#include <QLabel>
#include <QWidget>
#include <QMenu>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>
#include <memory>
#include <vector>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgetregistry.h"
#include "lab/module/labmodule.h"
#include "lab/scenes/diffdrivescene.h"
#include "lab/scenes/sceneconfigeditor.h"
#include "lab/scenes/sceneregistry.h"
#include "lab/scenes/scenewidget.h"
#include "core/moduledevicetype.h"
#include "lab/module/labpanel.h"
#include "modules/moduleregistry.h"

using namespace traceview;
using tracelab::LabModule;

namespace {

class FakeHost final : public ModuleHost {
public:
    WidgetRegistry& widgetRegistry() override {
        return WidgetRegistry::instance();
    }

    void addRibbonTab(const QString& label, const QList<QAction*>& actions,
                      QWidget* content) override {
        m_labels.append(label);
        m_actionCounts.append(actions.size());
        m_contents.emplace_back(content);
    }

    bool registerDeviceType(const ModuleDeviceType& type) override {
        m_deviceTypes.append(type.id);
        return true;
    }
    bool openProject(const QString& path) override {
        m_opened.append(path);
        return true;
    }
    void registerExampleProject(const QString& group, const QString& name,
                                const QString& path) override {
        m_examples.append(group + QLatin1Char('/') + name + QLatin1Char('=') + path);
    }
    QString addModuleDevice(const QString& typeId, const QString& name,
                            const QJsonObject& config) override {
        Q_UNUSED(name);
        m_addedDevices.append(typeId);
        m_addedConfigs.append(config);
        return QStringLiteral("dev%1").arg(m_addedDevices.size());
    }
    bool addDashboardWidget(const QString& typeId, const QJsonObject& config) override {
        m_addedWidgets.append(typeId);
        m_addedWidgetConfigs.append(config);
        return true;
    }

    QStringList m_deviceTypes;
    QStringList m_opened;
    QStringList m_examples;
    QStringList m_addedDevices;
    QList<QJsonObject> m_addedConfigs;
    QStringList m_addedWidgets;
    QList<QJsonObject> m_addedWidgetConfigs;

    QStringList m_labels;
    QList<int> m_actionCounts;
    std::vector<std::unique_ptr<QWidget>> m_contents;
};

bool hasWidgetType(const QString& typeId) {
    for (const WidgetTypeInfo& info : WidgetRegistry::instance().availableTypes()) {
        if (info.typeId == typeId) {
            return true;
        }
    }
    return false;
}

}  // namespace

class TestLabModule : public QObject {
    Q_OBJECT

private slots:
    void identity();
    void installRegistersSceneWidgetAndTab();
    void modeArgumentPicksTheLab();
};

void TestLabModule::identity() {
    LabModule module;
    QCOMPARE(module.id(), QString("lab"));
    QCOMPARE(module.displayName(), QString("TraceLab"));
    QVERIFY(!module.description().isEmpty());
}

void TestLabModule::installRegistersSceneWidgetAndTab() {
    QVERIFY(!hasWidgetType(tracelab::kSceneWidgetTypeId));
    LabModule module;
    FakeHost host;
    module.install(host);

    QVERIFY(hasWidgetType(tracelab::kSceneWidgetTypeId));
    QCOMPARE(WidgetRegistry::instance().displayName(tracelab::kSceneWidgetTypeId),
             QString("Scene"));
    QVERIFY(tracelab::SceneRegistry::instance().contains(tracelab::DiffDriveScene::kTypeId));

    // The widget and its editor come from the registered factories.
    std::unique_ptr<DashboardWidget> widget(
        WidgetRegistry::instance().create(tracelab::kSceneWidgetTypeId, nullptr));
    auto* scene = dynamic_cast<tracelab::SceneWidget*>(widget.get());
    QVERIFY(scene);
    QCOMPARE(scene->sceneType(), QString(tracelab::DiffDriveScene::kTypeId));
    for (const WidgetTypeInfo& info : WidgetRegistry::instance().availableTypes()) {
        if (info.typeId == QLatin1String(tracelab::kSceneWidgetTypeId)) {
            QVERIFY(info.configEditorFactory);
            std::unique_ptr<WidgetConfigEditor> editor(info.configEditorFactory(nullptr));
            QVERIFY(dynamic_cast<tracelab::SceneConfigEditor*>(editor.get()));
        }
    }

    QCOMPARE(host.m_labels, QStringList{"TraceLab"});
    // Add plant, Add controller, Add scene | Play, Pause, Reset, Step.
    QCOMPARE(host.m_actionCounts, QList<int>{8});
    auto* panel = dynamic_cast<tracelab::LabPanel*>(host.m_contents.front().get());
    QVERIFY(panel);
    QVERIFY(panel->ribbonActions().at(3)->isSeparator());
    QCOMPARE(host.m_deviceTypes, (QStringList{"tracelab.plant", "tracelab.controller",
                                              "tracelab.linefollower.plant",
                                              "tracelab.linefollower.controller"}));
    // The benchmark is a project (File > Open Example), not a tab button.
    QCOMPARE(host.m_examples,
             (QStringList{"TraceLab benchmarks/Line follower=" +
                              tracelab::LabPanel::lineFollowerProjectPath(),
                          "TraceLab benchmarks/Furuta pendulum=" +
                              tracelab::LabPanel::furutaProjectPath()}));
    QVERIFY(QFile::exists(tracelab::LabPanel::lineFollowerProjectPath()));
    QVERIFY(QFile::exists(tracelab::LabPanel::furutaProjectPath()));
    // The line follower: the car's noise on, the Kalman filter on the
    // controller, and a workspace of charts of several sources bound by name
    // to what the blocks publish.
    {
        QFile file(tracelab::LabPanel::lineFollowerProjectPath());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject project = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonArray devices =
            project.value("devices").toObject().value("devices").toArray();
        QStringList filters;
        double noise = 0;
        for (const QJsonValue& device : devices) {
            const QJsonObject config = device.toObject().value("moduleConfig").toObject();
            for (const QJsonValue& filter : config.value("filters").toArray()) {
                filters.append(filter.toObject().value("template").toString());
            }
            noise = qMax(noise, config.value("plant").toObject().value("params").toObject()
                                    .value("noise").toDouble());
        }
        QCOMPARE(filters, QStringList{QStringLiteral("linefollower/kalman")});
        QCOMPARE(noise, 1.0);
        QStringList fields;
        const QJsonArray workspaces = project.value("workspaces").toObject().value("list").toArray();
        QCOMPARE(workspaces.size(), 2);
        QCOMPARE(workspaces.at(1).toObject().value("name").toString(),
                 QStringLiteral("Kalman filter"));
        for (const QJsonValue& item :
             workspaces.at(1).toObject().value("dashboard").toObject().value("items").toArray()) {
            if (item.toObject().value("type").toString() != QLatin1String("multi_line")) {
                continue;
            }
            const QJsonObject bind = item.toObject().value("config").toObject().value("bind").toObject();
            for (const QJsonValue& binding : bind) {
                fields.append(binding.toObject().value("topicName").toString() + '.' +
                              binding.toObject().value("fieldName").toString());
            }
        }
        for (const char* expected : {"sim.states.speed", "ctl.diag.v_kf", "sim.states.omega",
                                     "ctl.diag.omega_kf", "loop.state.gyro",
                                     "sim.states.gyro_bias_true", "ctl.diag.gyro_bias_kf"}) {
            QVERIFY2(fields.contains(QLatin1String(expected)), expected);
        }
    }
    QVERIFY(host.m_opened.isEmpty());

    // The build tools: a plant first; a controller or a scene needs one.
    const QList<QAction*> actions = panel->ribbonActions();
    actions[1]->trigger();  // Add controller, no plant
    actions[2]->trigger();  // Add scene, no plant
    QVERIFY(host.m_addedDevices.isEmpty());
    QVERIFY(host.m_addedWidgets.isEmpty());
    actions[0]->trigger();  // Add plant: the line follower's car in C, on its track
    QCOMPARE(host.m_addedDevices, QStringList{"tracelab.plant"});
    QCOMPARE(host.m_addedConfigs.first().value("plant").toObject().value("template").toString(),
             QString("linefollower/car"));
    QCOMPARE(host.m_addedConfigs.first().value("environment").toObject().value("id").toString(),
             QString("linefollower.track"));
    // Its menu has every kind of plant; the Furuta pendulum's has no
    // environment.
    QMenu* plants = actions[0]->menu();
    QVERIFY(plants);
    emit plants->aboutToShow();
    QAction* furuta = nullptr;
    for (QAction* action : plants->actions()) {
        if (action->text() == QLatin1String("Furuta pendulum")) {
            furuta = action;
        }
    }
    QVERIFY(furuta);
    furuta->trigger();
    QCOMPARE(host.m_addedDevices.size(), 2);
    QCOMPARE(host.m_addedConfigs.last().value("plant").toObject().value("template").toString(),
             QString("furuta/pendulum"));
    QVERIFY(!host.m_addedConfigs.last().contains("environment"));
}

void TestLabModule::modeArgumentPicksTheLab() {
    ModuleRegistry registry;
    registry.registerModule(std::make_unique<LabModule>());
    AppModule* lab = registry.applyModeArgument({"TraceView", "--mode", "lab"});
    QVERIFY(lab);
    QCOMPARE(lab->id(), QString("lab"));
    QVERIFY(registry.isEnabled("lab"));
}

QTEST_MAIN(TestLabModule)
#include "test_labmodule.moc"
