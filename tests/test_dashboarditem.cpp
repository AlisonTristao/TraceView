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
    item.small = {0.1, 0.2, 0.4, 0.4};
    item.medium = {0.15, 0.3, 0.35, 0.3};
    item.large = {0.25, 0.5, 0.3333, 0.125};

    bool ok = false;
    const DashboardItem roundTripped = dashboardItemFromJson(dashboardItemToJson(item), &ok);

    QVERIFY(ok);
    QCOMPARE(roundTripped.id, item.id);
    QCOMPARE(roundTripped.typeId, item.typeId);
    QCOMPARE(roundTripped.name, item.name);
    QCOMPARE(roundTripped.key, item.key);
    QCOMPARE(roundTripped.config, item.config);
    QCOMPARE(roundTripped.small.x, item.small.x);
    QCOMPARE(roundTripped.small.y, item.small.y);
    QCOMPARE(roundTripped.small.width, item.small.width);
    QCOMPARE(roundTripped.small.height, item.small.height);
    QCOMPARE(roundTripped.medium.x, item.medium.x);
    QCOMPARE(roundTripped.medium.width, item.medium.width);
    QCOMPARE(roundTripped.large.x, item.large.x);
    QCOMPARE(roundTripped.large.y, item.large.y);
    QCOMPARE(roundTripped.large.width, item.large.width);
    QCOMPARE(roundTripped.large.height, item.large.height);
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
    // No "layouts" key -- exercises the legacy-project migration path (a
    // flat x/y/width/height, predating per-screen-size layouts), which seeds
    // all three breakpoints identically from it (see dashboarditem.cpp).
    bool ok = false;
    const DashboardItem item = dashboardItemFromJson(QJsonObject{{"id", "abc"},
                                                                 {"type", "dummy_line"},
                                                                 {"x", -0.5},
                                                                 {"y", 1.5},
                                                                 {"width", 0.5},
                                                                 {"height", 0.5}},
                                                     &ok);

    QVERIFY(ok);
    QCOMPARE(item.small.x, 0.0);
    QCOMPARE(item.small.y, 1.0);
    QCOMPARE(item.medium.x, 0.0);
    QCOMPARE(item.medium.y, 1.0);
    QCOMPARE(item.large.x, 0.0);
    QCOMPARE(item.large.y, 1.0);
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

}  // namespace

QTEST_MAIN(TestDashboardItem)
#include "test_dashboarditem.moc"
