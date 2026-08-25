#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "devices/device.h"
#include "devices/devicecard.h"
#include "devices/devicesgrid.h"

using traceview::CommType;
using traceview::Device;
using traceview::DeviceCard;
using traceview::DevicesGrid;
using traceview::deviceToJson;
using traceview::TransportType;

namespace {

Device makeDevice(const QString& name) {
    Device device;
    device.name = name;
    device.commType = CommType::Btp;
    return device;
}

DeviceCard* cardFor(const DevicesGrid& grid, const QString& id) {
    for (DeviceCard* card : grid.findChildren<DeviceCard*>()) {
        if (card->device().id == id) {
            return card;
        }
    }
    return nullptr;
}

}  // namespace

class TestDevicesGrid : public QObject {
    Q_OBJECT

private slots:
    void addDeviceReturnsUsableIdAndOrdersLeftToRightThenWraps();
    void removeDeviceCompactsRemainingLayout();
    void updateDeviceRefreshesCardData();
    void clickSelectsCardAndReplacesPreviousSelection();
    void removeSelectedRemovesTheSelectedDevice();
    void removeDeviceClearsSelectionIfRemovedDeviceWasSelected();
    void toJsonFromJsonRoundTripsTheWholeList();
    void fromJsonReplacesRatherThanMerges();
    void removingAHubWithChildrenIsRefusedAndExplained();
};

void TestDevicesGrid::addDeviceReturnsUsableIdAndOrdersLeftToRightThenWraps() {
    DevicesGrid grid;
    // Both dimensions <= 849 so gutter() clamps to its 8px floor
    // deterministically (see kMinGutter/kGutterFraction in
    // devicesgrid.cpp) no matter the exact rounding: 600 * 0.01 = 6, below
    // the floor either way. At an 8px gutter, kDeviceCardSize (260x140)
    // gives a 268px row pitch; 600 - 8 = 592px usable width fits exactly 2
    // cards per row (2*268=536 <= 592 < 3*268=804).
    grid.resize(600, 600);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    const QString idA = grid.addDevice(makeDevice("Alpha"));
    QVERIFY(!idA.isEmpty());
    const QString idB = grid.addDevice(makeDevice("Bravo"));
    const QString idC = grid.addDevice(makeDevice("Charlie"));
    QVERIFY(idA != idB && idB != idC);

    QCOMPARE(grid.devices().size(), 3);

    DeviceCard* cardA = cardFor(grid, idA);
    DeviceCard* cardB = cardFor(grid, idB);
    DeviceCard* cardC = cardFor(grid, idC);
    QVERIFY(cardA && cardB && cardC);

    QCOMPARE(cardA->geometry(), QRect(8, 8, 260, 140));
    QCOMPARE(cardB->geometry(), QRect(276, 8, 260, 140));
    QCOMPARE(cardC->geometry(), QRect(8, 156, 260, 140));  // wraps to row 2
}

void TestDevicesGrid::removeDeviceCompactsRemainingLayout() {
    DevicesGrid grid;
    grid.resize(600, 600);  // same layout math as the test above
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    const QString idA = grid.addDevice(makeDevice("Alpha"));
    const QString idB = grid.addDevice(makeDevice("Bravo"));
    const QString idC = grid.addDevice(makeDevice("Charlie"));

    grid.removeDevice(idB);  // middle entry -- proves compaction, not just erase

    QCOMPARE(grid.devices().size(), 2);
    for (const Device& device : grid.devices()) {
        QVERIFY(device.id != idB);
    }
    // Not asserting the removed card is gone from findChildren() here --
    // it's cleaned up via deleteLater() (see DevicesGrid::removeDevice()),
    // which only actually runs once the event loop gets a turn.

    DeviceCard* cardA = cardFor(grid, idA);
    DeviceCard* cardC = cardFor(grid, idC);
    QVERIFY(cardA && cardC);

    // idC re-flows from row 2, col 0 into idB's old row 1, col 1 slot --
    // no gap left where idB used to be.
    QCOMPARE(cardA->geometry(), QRect(8, 8, 260, 140));
    QCOMPARE(cardC->geometry(), QRect(276, 8, 260, 140));
}

void TestDevicesGrid::updateDeviceRefreshesCardData() {
    DevicesGrid grid;
    const QString id = grid.addDevice(makeDevice("Alpha"));

    Device updated = grid.devices().first();
    updated.name = "Alpha Renamed";
    updated.connected = true;
    updated.description = "Updated description";
    grid.updateDevice(updated);

    DeviceCard* card = cardFor(grid, id);
    QVERIFY(card != nullptr);
    QCOMPARE(card->device().name, QString("Alpha Renamed"));
    QVERIFY(card->device().connected);
    QCOMPARE(card->device().description, QString("Updated description"));

    // updateDevice() must not disturb the other entries/order -- only one
    // device exists here, but check the model itself reflects the edit too.
    QCOMPARE(grid.devices().size(), 1);
    QCOMPARE(grid.devices().first().name, QString("Alpha Renamed"));
}

void TestDevicesGrid::clickSelectsCardAndReplacesPreviousSelection() {
    DevicesGrid grid;
    grid.resize(600, 600);  // same layout math as the tests above
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    const QString idA = grid.addDevice(makeDevice("Alpha"));
    const QString idB = grid.addDevice(makeDevice("Bravo"));
    DeviceCard* cardA = cardFor(grid, idA);
    DeviceCard* cardB = cardFor(grid, idB);
    QVERIFY(cardA && cardB);
    QCOMPARE(grid.selectedCount(), 0);

    QSignalSpy selectionSpy(&grid, &DevicesGrid::selectionChanged);
    // Header point away from the gear button (top-right corner) -- same safe
    // spot test_dashboardgrid.cpp's ctrlClickTogglesSelectionAndWholeGroupAsUnit
    // clicks on DashboardCell.
    const QPoint headerPoint(10, 10);

    QTest::mouseClick(cardA, Qt::LeftButton, Qt::NoModifier, headerPoint);
    QCOMPARE(grid.selectedCount(), 1);
    QVERIFY(cardA->isSelected());
    QVERIFY(!cardB->isSelected());
    QCOMPARE(selectionSpy.count(), 1);

    // DevicesGrid has no Ctrl-click multi-select like DashboardGrid --
    // clicking a second card always replaces the selection.
    QTest::mouseClick(cardB, Qt::LeftButton, Qt::NoModifier, headerPoint);
    QCOMPARE(grid.selectedCount(), 1);
    QVERIFY(!cardA->isSelected());
    QVERIFY(cardB->isSelected());
    QCOMPARE(selectionSpy.count(), 2);
}

void TestDevicesGrid::removeSelectedRemovesTheSelectedDevice() {
    DevicesGrid grid;
    grid.resize(600, 600);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    const QString idA = grid.addDevice(makeDevice("Alpha"));
    const QString idB = grid.addDevice(makeDevice("Bravo"));
    DeviceCard* cardB = cardFor(grid, idB);
    QVERIFY(cardB);
    QTest::mouseClick(cardB, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QCOMPARE(grid.selectedCount(), 1);

    grid.removeSelected();

    QCOMPARE(grid.devices().size(), 1);
    QCOMPARE(grid.devices().first().id, idA);
    QCOMPARE(grid.selectedCount(), 0);

    // No-op once nothing is selected -- must not touch the remaining device.
    grid.removeSelected();
    QCOMPARE(grid.devices().size(), 1);
}

void TestDevicesGrid::removeDeviceClearsSelectionIfRemovedDeviceWasSelected() {
    DevicesGrid grid;
    grid.resize(600, 600);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    const QString idA = grid.addDevice(makeDevice("Alpha"));
    DeviceCard* cardA = cardFor(grid, idA);
    QVERIFY(cardA);
    QTest::mouseClick(cardA, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QCOMPARE(grid.selectedCount(), 1);

    QSignalSpy selectionSpy(&grid, &DevicesGrid::selectionChanged);
    grid.removeDevice(idA);  // removed directly, not via removeSelected()
    QCOMPARE(grid.selectedCount(), 0);
    QCOMPARE(selectionSpy.count(), 1);
}

void TestDevicesGrid::toJsonFromJsonRoundTripsTheWholeList() {
    DevicesGrid grid;
    Device alpha = makeDevice("Alpha");
    alpha.portName = "COM3";
    alpha.baudRate = 115200;
    const QString idA = grid.addDevice(alpha);
    const QString idB = grid.addDevice(makeDevice("Bravo"));

    const QJsonObject saved = grid.toJson();

    DevicesGrid restored;
    restored.fromJson(saved);

    QCOMPARE(restored.devices().size(), 2);
    QCOMPARE(restored.devices().at(0).id, idA);
    QCOMPARE(restored.devices().at(0).name, QString("Alpha"));
    QCOMPARE(restored.devices().at(0).portName, QString("COM3"));
    QCOMPARE(restored.devices().at(0).baudRate, 115200);
    QCOMPARE(restored.devices().at(1).id, idB);
}

void TestDevicesGrid::fromJsonReplacesRatherThanMerges() {
    DevicesGrid grid;
    grid.addDevice(makeDevice("Stale"));

    Device fresh = makeDevice("Fresh");
    fresh.id = "fixed-id-for-this-test";
    QJsonObject saved;
    saved["devices"] = QJsonArray{deviceToJson(fresh)};

    grid.fromJson(saved);

    QCOMPARE(grid.devices().size(), 1);
    QCOMPARE(grid.devices().first().name, QString("Fresh"));
}

// Deleting a hub that other devices ride is refused, not cascaded.
//
// A cascade would be one undo step that quietly destroys several devices
// along with every chart binding pointing at them, and the person clicking
// delete on the dongle is usually not asking to lose the robots configured
// behind it. Refusing names what depends on it and lets them decide -- and
// the naming matters: "it has children" would leave them hunting.
void TestDevicesGrid::removingAHubWithChildrenIsRefusedAndExplained() {
    DevicesGrid grid;

    Device hub;
    hub.id = "dongle-0";
    hub.name = "Bench dongle";
    const QString hubId = grid.addDevice(hub);

    Device child;
    child.id = "robot-a";
    child.name = "Robot A";
    child.transportType = TransportType::HubChannel;
    child.parentDeviceId = hubId;
    child.peerSourceId = 0x0A0A0A0Au;
    grid.addDevice(child);

    QCOMPARE(grid.childDeviceIds(hubId), QStringList{QStringLiteral("robot-a")});

    QSignalSpy blocked(&grid, &DevicesGrid::removeBlockedByChildren);
    QSignalSpy removed(&grid, &DevicesGrid::deviceRemoved);

    grid.removeDevice(hubId);

    QCOMPARE(blocked.count(), 1);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(grid.devices().size(), 2);  // nothing was destroyed
    // The explanation carries the child's NAME, not its id.
    QCOMPARE(blocked.at(0).at(1).toStringList(), QStringList{QStringLiteral("Robot A")});

    // Remove the child first and the hub becomes removable, with no special
    // action needed to "unblock" it.
    grid.removeDevice("robot-a");
    QCOMPARE(grid.devices().size(), 1);
    grid.removeDevice(hubId);
    QCOMPARE(grid.devices().size(), 0);
}

QTEST_MAIN(TestDevicesGrid)
#include "test_devicesgrid.moc"
