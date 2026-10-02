#include <QAction>
#include <QCheckBox>
#include <QGroupBox>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "core/mainwindow.h"
#include "core/settingspage.h"
#include "dashboard/dashboardwidget.h"
#include "dashboard/widgetregistry.h"
#include "modules/moduleregistry.h"
#include "preferences/appsettings.h"

using namespace traceview;

namespace {

// Registers one widget type and one ribbon tab named after its id, and keeps
// the tab's content so the test can find it in the window.
class FakeModule final : public AppModule {
public:
    explicit FakeModule(const QString& id) : m_id(id) {}

    QString id() const override {
        return m_id;
    }
    QString displayName() const override {
        return m_id + QStringLiteral(" Lab");
    }
    QString description() const override {
        return m_id + QStringLiteral(" description");
    }

    void install(ModuleHost& host) override {
        ++m_installCount;
        host.widgetRegistry().registerType(
            {m_id + QStringLiteral("_widget"), displayName(),
             [](QWidget* parent) { return new DashboardWidget(parent); }});
        m_content = new QWidget;
        host.addRibbonTab(m_id + QStringLiteral(" tab"), {new QAction(m_content)}, m_content);
    }

    int installCount() const {
        return m_installCount;
    }
    QWidget* content() const {
        return m_content;
    }

private:
    QString m_id;
    int m_installCount = 0;
    QWidget* m_content = nullptr;
};

QGroupBox* modulesSection(const QWidget& page) {
    for (QGroupBox* box : page.findChildren<QGroupBox*>()) {
        if (box->title() == QLatin1String("Modules")) {
            return box;
        }
    }
    return nullptr;
}

int tabIndex(const QTabBar& tabs, const QString& label) {
    for (int i = 0; i < tabs.count(); ++i) {
        if (tabs.tabText(i) == label) {
            return i;
        }
    }
    return -1;
}

}  // namespace

class TestMainWindowModules : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    // Runs first, while ModuleRegistry::instance() is still empty.
    void settingsHidesModulesWhenNoneRegistered();
    void mainWindowInstallsEnabledModulesAndTakesModeIdentity();
    void settingsListsModulesAndSavesToggle();

private:
    QTemporaryDir m_settingsDir;
    FakeModule* m_lab = nullptr;
    FakeModule* m_off = nullptr;
};

void TestMainWindowModules::initTestCase() {
    QVERIFY(m_settingsDir.isValid());
    QCoreApplication::setOrganizationName("TraceViewTest");
    QCoreApplication::setApplicationName("test_mainwindowmodules");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    AppSettings::instance().setUpdateAutoCheckEnabled(false);
}

void TestMainWindowModules::settingsHidesModulesWhenNoneRegistered() {
    SettingsPage page;
    QVERIFY(modulesSection(page) == nullptr);
}

void TestMainWindowModules::mainWindowInstallsEnabledModulesAndTakesModeIdentity() {
    ModuleRegistry& registry = ModuleRegistry::instance();
    auto lab = std::make_unique<FakeModule>("fake");
    auto off = std::make_unique<FakeModule>("off");
    m_lab = lab.get();
    m_off = off.get();
    registry.registerModule(std::move(lab));
    registry.registerModule(std::move(off));
    QCOMPARE(registry.applyModeArgument({"TraceView", "--mode", "fake"}), m_lab);

    MainWindow window;
    window.show();

    QCOMPARE(m_lab->installCount(), 1);
    QCOMPARE(m_off->installCount(), 0);
    QVERIFY(window.windowTitle().startsWith("fake Lab v"));
    QCOMPARE(WidgetRegistry::instance().displayName("fake_widget"), QString("fake Lab"));
    QVERIFY(WidgetRegistry::instance().displayName("off_widget").isEmpty());

    auto* tabs = window.findChild<QTabBar*>();
    QVERIFY(tabs);
    QVERIFY(tabIndex(*tabs, "off tab") < 0);
    const int index = tabIndex(*tabs, "fake tab");
    QVERIFY(index >= 0);

    // Selecting the tab swaps its content into the main content area.
    auto* stack = qobject_cast<QStackedWidget*>(m_lab->content()->parentWidget());
    QVERIFY(stack);
    QVERIFY(stack->currentWidget() != m_lab->content());
    tabs->setCurrentIndex(index);
    QCOMPARE(stack->currentWidget(), m_lab->content());

    // --mode never changes the saved choice.
    QVERIFY(!registry.isEnabledInSettings("fake"));
}

void TestMainWindowModules::settingsListsModulesAndSavesToggle() {
    SettingsPage page;
    QGroupBox* section = modulesSection(page);
    QVERIFY(section);
    const QList<QCheckBox*> checks = section->findChildren<QCheckBox*>();
    QCOMPARE(checks.size(), 2);
    QVERIFY(!checks[0]->isChecked());

    checks[1]->setChecked(true);
    QVERIFY(ModuleRegistry::instance().isEnabledInSettings("off"));
    QVERIFY(!ModuleRegistry::instance().isEnabledInSettings("fake"));
    checks[1]->setChecked(false);
    QVERIFY(!ModuleRegistry::instance().isEnabledInSettings("off"));
}

QTEST_MAIN(TestMainWindowModules)
#include "test_mainwindowmodules.moc"
