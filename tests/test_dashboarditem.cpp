#include <QtTest>

#include "dashboard/dashboarditem.h"

using traceview::DashboardItem;
using traceview::dashboardItemFromJson;
using traceview::dashboardItemToJson;

namespace {

class TestDashboardItem : public QObject {
    Q_OBJECT

private slots:
    void roundTripsAllFields();
    void fromJsonRejectsMissingIdOrType();
    void fromJsonClampsFractionsToUnitRange();
    void fromJsonDefaultsMissingOptionalFields();
};

void TestDashboardItem::roundTripsAllFields() {
    DashboardItem item;
    item.id = "abc-123";
    item.typeId = "dummy_line";
    item.name = "My Chart";
    item.key = "chart1";
    item.config = QJsonObject{{"series", 3}};
    item.x = 0.25;
    item.y = 0.5;
    item.width = 0.3333;
    item.height = 0.125;

    bool ok = false;
    const DashboardItem roundTripped = dashboardItemFromJson(dashboardItemToJson(item), &ok);

    QVERIFY(ok);
    QCOMPARE(roundTripped.id, item.id);
    QCOMPARE(roundTripped.typeId, item.typeId);
    QCOMPARE(roundTripped.name, item.name);
    QCOMPARE(roundTripped.key, item.key);
    QCOMPARE(roundTripped.config, item.config);
    QCOMPARE(roundTripped.x, item.x);
    QCOMPARE(roundTripped.y, item.y);
    QCOMPARE(roundTripped.width, item.width);
    QCOMPARE(roundTripped.height, item.height);
}

void TestDashboardItem::fromJsonRejectsMissingIdOrType() {
    bool ok = true;
    dashboardItemFromJson(QJsonObject{{"type", "dummy_line"}}, &ok);
    QVERIFY(!ok);

    ok = true;
    dashboardItemFromJson(QJsonObject{{"id", "abc"}}, &ok);
    QVERIFY(!ok);
}

void TestDashboardItem::fromJsonClampsFractionsToUnitRange() {
    bool ok = false;
    const DashboardItem item = dashboardItemFromJson(
        QJsonObject{{"id", "abc"}, {"type", "dummy_line"}, {"x", -0.5}, {"y", 1.5}, {"width", 0.5}, {"height", 0.5}},
        &ok);

    QVERIFY(ok);
    QCOMPARE(item.x, 0.0);
    QCOMPARE(item.y, 1.0);
}

void TestDashboardItem::fromJsonDefaultsMissingOptionalFields() {
    bool ok = false;
    const DashboardItem item = dashboardItemFromJson(
        QJsonObject{{"id", "abc"}, {"type", "dummy_line"}, {"width", 0.5}, {"height", 0.5}}, &ok);

    QVERIFY(ok);
    QVERIFY(item.name.isEmpty());
    QVERIFY(item.key.isEmpty());
    QVERIFY(item.config.isEmpty());
}

} // namespace

QTEST_MAIN(TestDashboardItem)
#include "test_dashboarditem.moc"
