#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabBar>
#include <QtTest>

#include "dashboard/widgets/serialmonitorwidget.h"
#include "dashboard/widgets/serialterminalwidget.h"

using traceview::SerialMonitorWidget;
using traceview::SerialTerminalWidget;

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

SerialTerminalWidget* terminalAt(const SerialMonitorWidget& widget, int index) {
    auto* stack = widget.findChild<QStackedWidget*>();
    return stack ? qobject_cast<SerialTerminalWidget*>(stack->widget(index)) : nullptr;
}

QTabBar* tabBarOf(const SerialMonitorWidget& widget) {
    return widget.findChild<QTabBar*>();
}

class TestSerialMonitorWidget : public QObject {
    Q_OBJECT

private slots:
    void emptyConfigMigratesToOneTab();
    void bareDeviceIdMigratesToOneTab();
    void tabsConfigBuildsOneTerminalPerDevice();
    void deviceNamesLabelTheTabs();
    void feedDeviceRoutesToTheMatchingTabOnly();
    void terminalInputCarriesTheActiveTabDeviceId();
    void tabSwitchSignalCyclesTabs();
};

void TestSerialMonitorWidget::emptyConfigMigratesToOneTab() {
    SerialMonitorWidget widget;
    widget.setConfig(QJsonObject{});

    QCOMPARE(widget.tabDeviceIds(), QStringList{QString()});
    QVERIFY(tabBarOf(widget)->isHidden());  // no strip for a single tab
}

void TestSerialMonitorWidget::bareDeviceIdMigratesToOneTab() {
    SerialMonitorWidget widget;
    QJsonObject legacy;
    legacy["deviceId"] = QStringLiteral("dev-a");
    widget.setConfig(legacy);

    QCOMPARE(widget.tabDeviceIds(), QStringList{QStringLiteral("dev-a")});
}

void TestSerialMonitorWidget::tabsConfigBuildsOneTerminalPerDevice() {
    SerialMonitorWidget widget;
    QSignalSpy tabsSpy(&widget, &SerialMonitorWidget::tabsChanged);

    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    QCOMPARE(widget.tabDeviceIds(),
             (QStringList{QStringLiteral("dev-a"), QStringLiteral("dev-b")}));
    QCOMPARE(tabBarOf(widget)->count(), 2);
    QVERIFY(!tabBarOf(widget)->isHidden());
    QVERIFY(terminalAt(widget, 0) != nullptr);
    QVERIFY(terminalAt(widget, 1) != nullptr);
    QCOMPARE(tabsSpy.count(), 1);
}

void TestSerialMonitorWidget::deviceNamesLabelTheTabs() {
    SerialMonitorWidget widget;
    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    widget.setDeviceNames({{QStringLiteral("dev-a"), QStringLiteral("Alpha")},
                           {QStringLiteral("dev-b"), QStringLiteral("Beta")}});

    QCOMPARE(tabBarOf(widget)->tabText(0), QStringLiteral("Alpha"));
    QCOMPARE(tabBarOf(widget)->tabText(1), QStringLiteral("Beta"));
}

void TestSerialMonitorWidget::feedDeviceRoutesToTheMatchingTabOnly() {
    SerialMonitorWidget widget;
    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));

    widget.feedDevice(QStringLiteral("dev-b"), QByteArrayLiteral("only b"));

    QCOMPARE(terminalAt(widget, 0)->toPlainText(), QString());
    QCOMPARE(terminalAt(widget, 1)->toPlainText(), QStringLiteral("only b"));

    // An id no tab is bound to is dropped, not broadcast.
    widget.feedDevice(QStringLiteral("dev-x"), QByteArrayLiteral("nobody"));
    QCOMPARE(terminalAt(widget, 0)->toPlainText(), QString());
    QCOMPARE(terminalAt(widget, 1)->toPlainText(), QStringLiteral("only b"));
}

void TestSerialMonitorWidget::terminalInputCarriesTheActiveTabDeviceId() {
    SerialMonitorWidget widget;
    widget.setConfig(tabsConfig({QStringLiteral("dev-a"), QStringLiteral("dev-b")}));
    QSignalSpy spy(&widget, &SerialMonitorWidget::terminalInput);

    QTest::keyClick(terminalAt(widget, 0), Qt::Key_X);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("dev-a"));

    tabBarOf(widget)->setCurrentIndex(1);
    QTest::keyClick(terminalAt(widget, 1), Qt::Key_Y);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("dev-b"));
}

void TestSerialMonitorWidget::tabSwitchSignalCyclesTabs() {
    SerialMonitorWidget widget;
    widget.setConfig(tabsConfig(
        {QStringLiteral("dev-a"), QStringLiteral("dev-b"), QStringLiteral("dev-c")}));
    QCOMPARE(tabBarOf(widget)->currentIndex(), 0);

    QTest::keyClick(terminalAt(widget, 0), Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(tabBarOf(widget)->currentIndex(), 1);

    QTest::keyClick(terminalAt(widget, 1), Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(tabBarOf(widget)->currentIndex(), 2);

    // Wraps back to the first tab.
    QTest::keyClick(terminalAt(widget, 2), Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(tabBarOf(widget)->currentIndex(), 0);

    QTest::keyClick(terminalAt(widget, 0), Qt::Key_Left, Qt::ControlModifier);
    QCOMPARE(tabBarOf(widget)->currentIndex(), 2);
}

}  // namespace

QTEST_MAIN(TestSerialMonitorWidget)
#include "test_serialmonitorwidget.moc"
