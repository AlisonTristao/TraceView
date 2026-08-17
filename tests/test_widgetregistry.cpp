#include <QtTest>

#include <memory>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgetregistry.h"

using traceview::DashboardWidget;
using traceview::WidgetRegistry;

namespace {

class TestWidgetRegistry : public QObject {
    Q_OBJECT

private slots:
    void knownBuiltinTypesAreRegistered();
    void createReturnsNullptrForUnknownType();
    void displayNameEmptyForUnknownType();
    void registerTypeIsNoOpForDuplicateId();
};

void TestWidgetRegistry::knownBuiltinTypesAreRegistered() {
    WidgetRegistry& registry = WidgetRegistry::instance();
    const QStringList knownIds = {"dummy_line",  "dummy_bar",    "dummy_gauge", "serial_monitor",
                                   "push_button", "toggle_switch", "slider"};
    for (const QString& typeId : knownIds) {
        QVERIFY2(!registry.displayName(typeId).isEmpty(), qPrintable(typeId));
        std::unique_ptr<DashboardWidget> widget(registry.create(typeId, nullptr));
        QVERIFY2(widget != nullptr, qPrintable(typeId));
    }
}

void TestWidgetRegistry::createReturnsNullptrForUnknownType() {
    QVERIFY(WidgetRegistry::instance().create("does_not_exist", nullptr) == nullptr);
}

void TestWidgetRegistry::displayNameEmptyForUnknownType() {
    QVERIFY(WidgetRegistry::instance().displayName("does_not_exist").isEmpty());
}

void TestWidgetRegistry::registerTypeIsNoOpForDuplicateId() {
    WidgetRegistry& registry = WidgetRegistry::instance();
    const int before = registry.availableTypes().size();
    const QString originalName = registry.displayName("dummy_line");

    registry.registerType({"dummy_line", "Should Be Ignored", [](QWidget*) -> DashboardWidget* { return nullptr; }});

    QCOMPARE(registry.availableTypes().size(), before);
    QCOMPARE(registry.displayName("dummy_line"), originalName);
}

} // namespace

QTEST_MAIN(TestWidgetRegistry)
#include "test_widgetregistry.moc"
