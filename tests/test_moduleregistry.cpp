#include <QAction>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <QtTest>
#include <memory>
#include <vector>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgetregistry.h"
#include "modules/moduleregistry.h"

using namespace traceview;

namespace {

// Registers one widget type and one ribbon tab, both named after its id, and
// counts how often it was installed.
class FakeModule final : public AppModule {
public:
    explicit FakeModule(const QString& id) : m_id(id) {}

    QString id() const override {
        return m_id;
    }
    QString displayName() const override {
        return m_id + QStringLiteral(" name");
    }
    QString description() const override {
        return m_id + QStringLiteral(" description");
    }

    void install(ModuleHost& host) override {
        ++m_installCount;
        host.widgetRegistry().registerType(
            {m_id + QStringLiteral("_widget"), displayName(),
             [](QWidget* parent) { return new DashboardWidget(parent); }});
        auto* content = new QWidget;
        host.addRibbonTab(m_id + QStringLiteral(" tab"), {new QAction(content)}, content);
    }

    int installCount() const {
        return m_installCount;
    }

private:
    QString m_id;
    int m_installCount = 0;
};

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

// Registers a FakeModule and keeps a pointer to it for the assertions.
FakeModule* addFake(ModuleRegistry& registry, const QString& id) {
    auto module = std::make_unique<FakeModule>(id);
    FakeModule* raw = module.get();
    registry.registerModule(std::move(module));
    return raw;
}

}  // namespace

class TestModuleRegistry : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void registersInOrderAndIgnoresDuplicateIds();
    void enabledStatePersistsInSettings();
    void modeArgumentEnablesForThisRunOnly();
    void modeArgumentAcceptsEqualsForm();
    void unknownModeIsIgnored();
    void modeWithoutIdIsIgnored();
    void installRunsOnlyEnabledModules();

private:
    QTemporaryDir m_settingsDir;
};

void TestModuleRegistry::initTestCase() {
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName("TraceViewTest");
    QCoreApplication::setApplicationName("test_moduleregistry");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
}

void TestModuleRegistry::init() {
    QSettings().clear();
}

void TestModuleRegistry::registersInOrderAndIgnoresDuplicateIds() {
    ModuleRegistry registry;
    FakeModule* first = addFake(registry, "first");
    FakeModule* second = addFake(registry, "second");
    addFake(registry, "first");

    QCOMPARE(registry.modules(), (QList<AppModule*>{first, second}));
    QCOMPARE(registry.module("second"), second);
    QVERIFY(registry.module("missing") == nullptr);
}

void TestModuleRegistry::enabledStatePersistsInSettings() {
    {
        ModuleRegistry registry;
        addFake(registry, "lab");
        QVERIFY(!registry.isEnabledInSettings("lab"));
        QVERIFY(!registry.isEnabled("lab"));
        registry.setEnabledInSettings("lab", true);
    }

    // A later run (a fresh registry) reads the saved choice back.
    ModuleRegistry registry;
    addFake(registry, "lab");
    QVERIFY(registry.isEnabledInSettings("lab"));
    QVERIFY(registry.isEnabled("lab"));

    registry.setEnabledInSettings("lab", false);
    QVERIFY(!ModuleRegistry().isEnabledInSettings("lab"));
}

void TestModuleRegistry::modeArgumentEnablesForThisRunOnly() {
    ModuleRegistry registry;
    FakeModule* lab = addFake(registry, "lab");
    addFake(registry, "other");

    QCOMPARE(registry.applyModeArgument({"TraceView", "--mode", "lab"}), lab);
    QCOMPARE(registry.modeModule(), lab);
    QVERIFY(registry.isEnabled("lab"));
    QVERIFY(!registry.isEnabled("other"));
    // The saved setting is untouched, so the next run starts without it.
    QVERIFY(!registry.isEnabledInSettings("lab"));
    ModuleRegistry nextRun;
    addFake(nextRun, "lab");
    QVERIFY(!nextRun.isEnabled("lab"));
}

void TestModuleRegistry::modeArgumentAcceptsEqualsForm() {
    ModuleRegistry registry;
    FakeModule* lab = addFake(registry, "lab");
    QCOMPARE(registry.applyModeArgument({"TraceView", "--other", "--mode=lab"}), lab);
    QVERIFY(registry.isEnabled("lab"));
}

void TestModuleRegistry::unknownModeIsIgnored() {
    ModuleRegistry registry;
    addFake(registry, "lab");
    QTest::ignoreMessage(QtWarningMsg, "--mode nope: no such module; ignored");
    QVERIFY(registry.applyModeArgument({"TraceView", "--mode", "nope"}) == nullptr);
    QVERIFY(registry.modeModule() == nullptr);
    QVERIFY(!registry.isEnabled("lab"));
    QVERIFY(!registry.isEnabled("nope"));
}

void TestModuleRegistry::modeWithoutIdIsIgnored() {
    ModuleRegistry registry;
    addFake(registry, "lab");
    QTest::ignoreMessage(QtWarningMsg, "--mode needs a module id; ignored");
    QVERIFY(registry.applyModeArgument({"TraceView", "--mode"}) == nullptr);
    // No --mode at all: nothing to do, nothing logged.
    QVERIFY(registry.applyModeArgument({"TraceView", "project.tvproj"}) == nullptr);
    QVERIFY(registry.modeModule() == nullptr);
}

void TestModuleRegistry::installRunsOnlyEnabledModules() {
    ModuleRegistry registry;
    FakeModule* saved = addFake(registry, "saved");
    FakeModule* disabled = addFake(registry, "disabled");
    FakeModule* mode = addFake(registry, "mode");
    registry.setEnabledInSettings("saved", true);
    registry.applyModeArgument({"TraceView", "--mode", "mode"});

    FakeHost host;
    registry.installEnabled(host);

    QCOMPARE(saved->installCount(), 1);
    QCOMPARE(disabled->installCount(), 0);
    QCOMPARE(mode->installCount(), 1);
    QCOMPARE(host.m_labels, (QStringList{"saved tab", "mode tab"}));
    QCOMPARE(host.m_actionCounts, (QList<int>{1, 1}));
    QCOMPARE(WidgetRegistry::instance().displayName("saved_widget"), QString("saved name"));
    QCOMPARE(WidgetRegistry::instance().displayName("mode_widget"), QString("mode name"));
    QVERIFY(WidgetRegistry::instance().displayName("disabled_widget").isEmpty());
}

QTEST_MAIN(TestModuleRegistry)
#include "test_moduleregistry.moc"
