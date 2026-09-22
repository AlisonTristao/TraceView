#include <QtTest>

#include "dashboard/dashboarditem.h"

using traceview::DashboardBreakpoint;
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
    item.geometry(DashboardBreakpoint::Small) = {0.1, 0.2, 0.4, 0.4};
    item.geometry(DashboardBreakpoint::Medium) = {0.15, 0.3, 0.35, 0.3};
    item.geometry(DashboardBreakpoint::Large) = {0.25, 0.5, 0.3333, 0.125};

    bool ok = false;
    const DashboardItem roundTripped = dashboardItemFromJson(dashboardItemToJson(item), &ok);

    QVERIFY(ok);
    QCOMPARE(roundTripped.id, item.id);
    QCOMPARE(roundTripped.typeId, item.typeId);
    QCOMPARE(roundTripped.name, item.name);
    QCOMPARE(roundTripped.key, item.key);
    QCOMPARE(roundTripped.config, item.config);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Small).x,
             item.geometry(DashboardBreakpoint::Small).x);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Small).y,
             item.geometry(DashboardBreakpoint::Small).y);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Small).width,
             item.geometry(DashboardBreakpoint::Small).width);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Small).height,
             item.geometry(DashboardBreakpoint::Small).height);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Medium).x,
             item.geometry(DashboardBreakpoint::Medium).x);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Medium).width,
             item.geometry(DashboardBreakpoint::Medium).width);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Large).x,
             item.geometry(DashboardBreakpoint::Large).x);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Large).y,
             item.geometry(DashboardBreakpoint::Large).y);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Large).width,
             item.geometry(DashboardBreakpoint::Large).width);
    QCOMPARE(roundTripped.geometry(DashboardBreakpoint::Large).height,
             item.geometry(DashboardBreakpoint::Large).height);
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
    QCOMPARE(item.geometry(DashboardBreakpoint::Small).x, 0.0);
    QCOMPARE(item.geometry(DashboardBreakpoint::Small).y, 1.0);
    QCOMPARE(item.geometry(DashboardBreakpoint::Medium).x, 0.0);
    QCOMPARE(item.geometry(DashboardBreakpoint::Medium).y, 1.0);
    QCOMPARE(item.geometry(DashboardBreakpoint::Large).x, 0.0);
    QCOMPARE(item.geometry(DashboardBreakpoint::Large).y, 1.0);
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
