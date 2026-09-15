#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QtTest>

#include "dashboard/widgets/robotlogwidget.h"

using traceview::RobotLogWidget;

namespace {

QJsonObject tabsConfig(const QStringList& deviceIds) {
    QJsonArray tabs;
    for (const QString& id : deviceIds) {
        QJsonObject tab;
        tab["deviceId"] = id;
        tabs.append(tab);
    }
    QJsonObject cfg;
    cfg["tabs"] = tabs;
    return cfg;
}

QTableView* tableAt(const RobotLogWidget& widget, int index) {
    auto* stack = widget.findChild<QStackedWidget*>();
    return stack ? qobject_cast<QTableView*>(stack->widget(index)) : nullptr;
}

QString cellText(QTableView* table, int row, int column) {
    return table->model()->index(row, column).data().toString();
}

QTabBar* tabBarOf(const RobotLogWidget& widget) {
    return widget.findChild<QTabBar*>();
}

class TestRobotLogWidget : public QObject {
    Q_OBJECT

private slots:
    void emptyConfigMigratesToOneTab();
    void bareDeviceIdMigratesToOneTab();
    void tabsConfigBuildsOneTablePerDevice();
    void deviceNamesLabelTheTabs();
    void feedDeviceRoutesToTheMatchingTabOnly();
    void clearLogOnlyClearsTheVisibleTab();
};

void TestRobotLogWidget::emptyConfigMigratesToOneTab() {
    RobotLogWidget widget;
    widget.setConfig(QJsonObject{});

    QCOMPARE(widget.tabDeviceIds(), QStringList{QString()});
    QVERIFY(tabBarOf(widget)->isHidden());  // no strip for a single tab
}

void TestRobotLogWidget::bareDeviceIdMigratesToOneTab() {
    RobotLogWidget widget;
    QJsonObject legacy;
    legacy["deviceId"] = QStringLiteral("dev-a");
    widget.setConfig(legacy);

    QCOMPARE(widget.tabDeviceIds(), QStringList{QStringLiteral("dev-a")});
}

void TestRobotLogWidget::tabsConfigBuildsOneTablePerDevice() {
    RobotLogWidget widget;
    QSignalSpy tabsSpy(&widget, &RobotLogWidget::tabsChanged);

    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    QCOMPARE(widget.tabDeviceIds(),
             (QStringList{QStringLiteral("dev-a"), QStringLiteral("dev-b")}));
    QCOMPARE(tabBarOf(widget)->count(), 2);
    QVERIFY(!tabBarOf(widget)->isHidden());
    QVERIFY(tableAt(widget, 0) != nullptr);
    QVERIFY(tableAt(widget, 1) != nullptr);
    QCOMPARE(tabsSpy.count(), 1);
}

void TestRobotLogWidget::deviceNamesLabelTheTabs() {
    RobotLogWidget widget;
    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    widget.setDeviceNames({{QStringLiteral("dev-a"), QStringLiteral("Alpha")},
                           {QStringLiteral("dev-b"), QStringLiteral("Beta")}});

    QCOMPARE(tabBarOf(widget)->tabText(0), QStringLiteral("Alpha"));
    QCOMPARE(tabBarOf(widget)->tabText(1), QStringLiteral("Beta"));
}

void TestRobotLogWidget::feedDeviceRoutesToTheMatchingTabOnly() {
    RobotLogWidget widget;
    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    widget.feedDevice(QStringLiteral("dev-b"), 1000ULL, 0x11223344U, 0xAABBCCDDU, 7U,
                      quint8(1) /* LogSeverity::Info */, QStringLiteral("only b"));

    QCOMPARE(tableAt(widget, 0)->model()->rowCount(), 0);
    QCOMPARE(tableAt(widget, 1)->model()->rowCount(), 1);
    QCOMPARE(cellText(tableAt(widget, 1), 0, 5 /* MessageColumn */), QStringLiteral("only b"));

    // An id no tab is bound to is dropped, not broadcast.
    widget.feedDevice(QStringLiteral("dev-x"), 2000ULL, 0, 0, 0, 0, QStringLiteral("nobody"));
    QCOMPARE(tableAt(widget, 0)->model()->rowCount(), 0);
    QCOMPARE(tableAt(widget, 1)->model()->rowCount(), 1);
}

void TestRobotLogWidget::clearLogOnlyClearsTheVisibleTab() {
    RobotLogWidget widget;
    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    widget.feedDevice(QStringLiteral("dev-a"), 1000ULL, 0, 0, 0, 0, QStringLiteral("a line"));
    widget.feedDevice(QStringLiteral("dev-b"), 1000ULL, 0, 0, 0, 0, QStringLiteral("b line"));
    QCOMPARE(tableAt(widget, 0)->model()->rowCount(), 1);
    QCOMPARE(tableAt(widget, 1)->model()->rowCount(), 1);

    tabBarOf(widget)->setCurrentIndex(1);
    widget.clearLog();

    QCOMPARE(tableAt(widget, 0)->model()->rowCount(), 1);
    QCOMPARE(tableAt(widget, 1)->model()->rowCount(), 0);
}

}  // namespace

QTEST_MAIN(TestRobotLogWidget)
#include "test_robotlogwidget.moc"
