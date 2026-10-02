#include <QAction>
#include <QLabel>
#include <QWidget>
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
    QCOMPARE(host.m_actionCounts, QList<int>{0});
    QVERIFY(host.m_contents.front()->findChild<QLabel*>());
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
