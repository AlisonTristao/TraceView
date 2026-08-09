#include <QtTest>

#include "core/serialdatarouter.h"
#include "core/serialmanager.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/dashboardwidget.h"
#include "dashboard/widgetregistry.h"

using traceview::DashboardGrid;
using traceview::DashboardWidget;
using traceview::SerialDataRouter;
using traceview::SerialManager;
using traceview::WidgetRegistry;

namespace {

// Records every payload handed to it so tests can assert on delivery
// without a real chart/gauge payload parser (Tasks 7/8 aren't done yet).
class ProbeWidget : public DashboardWidget {
public:
    explicit ProbeWidget(QWidget* parent = nullptr) : DashboardWidget(parent) {}

    QList<QByteArray> received;

    void onSerialPayload(const QByteArray& payload) override { received.append(payload); }
};

void registerProbeTypeOnce() {
    WidgetRegistry::instance().registerType(
        {"test_probe", "Test Probe", [](QWidget* parent) -> DashboardWidget* { return new ProbeWidget(parent); },
         nullptr});
}

class TestSerialDataRouter : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { registerProbeTypeOnce(); }

    void deliversPayloadToWidgetWithMatchingKey();
    void ignoresUnmatchedIdAndMalformedLines();
    void reassemblesFragmentedFrames();
    void reindexesAfterKeyChange();
    void stopsDeliveringAfterKeyCleared();
};

void TestSerialDataRouter::deliversPayloadToWidgetWithMatchingKey() {
    DashboardGrid grid;
    grid.addItem("test_probe");
    QVERIFY(grid.setSelectedKey("temp1"));
    auto* probe = static_cast<ProbeWidget*>(grid.keyedWidgets().value("temp1"));
    QVERIFY(probe);

    SerialManager manager;
    SerialDataRouter router(&manager, &grid);

    router.onSerialDataReceived(QByteArrayLiteral("[1234][temp1] 23.5\n"));

    QCOMPARE(probe->received.size(), 1);
    QCOMPARE(probe->received.first(), QByteArrayLiteral("23.5"));
}

void TestSerialDataRouter::ignoresUnmatchedIdAndMalformedLines() {
    DashboardGrid grid;
    grid.addItem("test_probe");
    QVERIFY(grid.setSelectedKey("temp1"));
    auto* probe = static_cast<ProbeWidget*>(grid.keyedWidgets().value("temp1"));
    QVERIFY(probe);

    SerialManager manager;
    SerialDataRouter router(&manager, &grid);

    router.onSerialDataReceived(QByteArrayLiteral("[1][other] 1\nnot a frame at all\n"));

    QVERIFY(probe->received.isEmpty());
}

void TestSerialDataRouter::reassemblesFragmentedFrames() {
    DashboardGrid grid;
    grid.addItem("test_probe");
    QVERIFY(grid.setSelectedKey("rpm"));
    auto* probe = static_cast<ProbeWidget*>(grid.keyedWidgets().value("rpm"));
    QVERIFY(probe);

    SerialManager manager;
    SerialDataRouter router(&manager, &grid);

    router.onSerialDataReceived(QByteArrayLiteral("[10][r"));
    router.onSerialDataReceived(QByteArrayLiteral("pm] 42"));
    QVERIFY(probe->received.isEmpty()); // still buffered, no EOL yet
    router.onSerialDataReceived(QByteArrayLiteral("00\n"));

    QCOMPARE(probe->received.size(), 1);
    QCOMPARE(probe->received.first(), QByteArrayLiteral("4200"));
}

void TestSerialDataRouter::reindexesAfterKeyChange() {
    DashboardGrid grid;
    grid.addItem("test_probe");
    QVERIFY(grid.setSelectedKey("oldKey"));
    auto* probe = static_cast<ProbeWidget*>(grid.keyedWidgets().value("oldKey"));
    QVERIFY(probe);

    SerialManager manager;
    SerialDataRouter router(&manager, &grid);

    QVERIFY(grid.setSelectedKey("newKey")); // rekey after the router already indexed "oldKey"

    router.onSerialDataReceived(QByteArrayLiteral("[1][oldKey] a\n[2][newKey] b\n"));

    QCOMPARE(probe->received.size(), 1);
    QCOMPARE(probe->received.first(), QByteArrayLiteral("b"));
}

void TestSerialDataRouter::stopsDeliveringAfterKeyCleared() {
    DashboardGrid grid;
    grid.addItem("test_probe");
    QVERIFY(grid.setSelectedKey("k"));
    auto* probe = static_cast<ProbeWidget*>(grid.keyedWidgets().value("k"));
    QVERIFY(probe);

    SerialManager manager;
    SerialDataRouter router(&manager, &grid);

    QVERIFY(grid.setSelectedKey(QString())); // clear the key

    router.onSerialDataReceived(QByteArrayLiteral("[1][k] x\n"));

    QVERIFY(probe->received.isEmpty());
}

} // namespace

QTEST_MAIN(TestSerialDataRouter)
#include "test_serialdatarouter.moc"
