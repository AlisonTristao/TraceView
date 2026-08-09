#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "dashboard/dashboardcell.h"
#include "dashboard/dashboardgrid.h"

using traceview::DashboardCell;
using traceview::DashboardGrid;

namespace {

class TestDashboardGrid : public QObject {
    Q_OBJECT

private slots:
    void addItemPushesUndoableCommand();
    void removeSelectedPushesUndoableCommand();
    void renameSelectedUpdatesDisplayName();
    void setSelectedKeyRejectsDuplicateKeys();
    void changeSelectedConfigIsUndoable();
    void changeSelectedTypeIsUndoable();
    void toJsonFromJsonRoundTrips();
    void dragMovesAndSnapsToNearestGridCell();
    void resizeChangesGeometryWithUndo();
    void resizeClampsToMinimumSize();
};

void TestDashboardGrid::addItemPushesUndoableCommand() {
    DashboardGrid grid;
    QCOMPARE(grid.undoStack()->count(), 0);

    grid.addItem("dummy_line");
    QCOMPARE(grid.undoStack()->count(), 1);
    QVERIFY(!grid.selectedItemId().isEmpty());
    QCOMPARE(grid.toJson().value("items").toArray().size(), 1);

    grid.undoStack()->undo();
    QVERIFY(grid.toJson().value("items").toArray().isEmpty());
    QVERIFY(grid.selectedItemId().isEmpty());

    grid.undoStack()->redo();
    QCOMPARE(grid.toJson().value("items").toArray().size(), 1);
}

void TestDashboardGrid::removeSelectedPushesUndoableCommand() {
    DashboardGrid grid;
    grid.addItem("dummy_line");
    const QString itemId = grid.selectedItemId();
    grid.undoStack()->clear(); // isolate the Remove command from the Add above

    grid.removeSelected();
    QCOMPARE(grid.undoStack()->count(), 1);
    QVERIFY(grid.toJson().value("items").toArray().isEmpty());

    grid.undoStack()->undo();
    QCOMPARE(grid.toJson().value("items").toArray().size(), 1);
    QCOMPARE(grid.selectedItemId(), itemId); // RemoveWidgetCommand::undo() reselects the restored item

    grid.undoStack()->redo();
    QVERIFY(grid.toJson().value("items").toArray().isEmpty());
}

void TestDashboardGrid::renameSelectedUpdatesDisplayName() {
    DashboardGrid grid;
    grid.addItem("dummy_line");
    const QString defaultName = grid.selectedItemDisplayName();
    QVERIFY(!defaultName.isEmpty()); // falls back to WidgetRegistry's display name

    grid.renameSelected("Custom Name");
    QCOMPARE(grid.selectedItemDisplayName(), QString("Custom Name"));

    grid.undoStack()->undo();
    QCOMPARE(grid.selectedItemDisplayName(), defaultName);

    grid.undoStack()->redo();
    QCOMPARE(grid.selectedItemDisplayName(), QString("Custom Name"));
}

void TestDashboardGrid::setSelectedKeyRejectsDuplicateKeys() {
    DashboardGrid grid;
    grid.addItem("dummy_line");
    const QString firstId = grid.selectedItemId();
    QVERIFY(grid.setSelectedKey("shared"));
    QCOMPARE(grid.selectedItemKey(), QString("shared"));

    grid.addItem("dummy_bar");
    QVERIFY(grid.selectedItemId() != firstId);
    QVERIFY(!grid.setSelectedKey("shared")); // already used by the first item
    QVERIFY(grid.selectedItemKey().isEmpty());

    QVERIFY(grid.setSelectedKey("unique"));
    grid.undoStack()->undo(); // undoes the successful SetItemKeyCommand above
    QVERIFY(grid.selectedItemKey().isEmpty());
}

void TestDashboardGrid::changeSelectedConfigIsUndoable() {
    DashboardGrid grid;
    grid.addItem("dummy_line");
    QVERIFY(grid.selectedItemConfig().isEmpty());

    QJsonObject config;
    config["series"] = 2;
    grid.changeSelectedConfig(config);
    QCOMPARE(grid.selectedItemConfig(), config);

    grid.undoStack()->undo();
    QVERIFY(grid.selectedItemConfig().isEmpty());

    grid.undoStack()->redo();
    QCOMPARE(grid.selectedItemConfig(), config);
}

void TestDashboardGrid::changeSelectedTypeIsUndoable() {
    DashboardGrid grid;
    grid.addItem("dummy_line");
    QCOMPARE(grid.selectedItemTypeId(), QString("dummy_line"));

    grid.changeSelectedType("dummy_bar");
    QCOMPARE(grid.selectedItemTypeId(), QString("dummy_bar"));

    grid.undoStack()->undo();
    QCOMPARE(grid.selectedItemTypeId(), QString("dummy_line"));

    grid.undoStack()->redo();
    QCOMPARE(grid.selectedItemTypeId(), QString("dummy_bar"));
}

void TestDashboardGrid::toJsonFromJsonRoundTrips() {
    DashboardGrid source;
    source.addItem("dummy_line");
    source.renameSelected("Chart A");
    QVERIFY(source.setSelectedKey("chartA"));

    source.addItem("push_button");
    source.renameSelected("Button B");

    const QJsonObject json = source.toJson();
    QCOMPARE(json.value("items").toArray().size(), 2);

    DashboardGrid target;
    target.fromJson(json);
    QCOMPARE(target.toJson(), json);
}

void TestDashboardGrid::dragMovesAndSnapsToNearestGridCell() {
    DashboardGrid grid;
    // 496x336 total => 480x320 usable area (8px margin each side, see
    // kMargin in dashboardgrid.cpp) => exactly 8px per grid column (60
    // cols) and 8px per grid row (40 rows), so pixel deltas below map to
    // whole-cell fractions without float rounding noise in the assertions.
    grid.resize(496, 336);
    grid.setEditMode(true);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    grid.addItem("dummy_line"); // default size 16/60 x 12/40, auto-placed at (0,0)

    auto* cell = grid.findChild<DashboardCell*>();
    QVERIFY(cell != nullptr);

    const QPoint headerPoint(80, 10); // inside the cell's 24px header strip
    // A delta that is NOT an exact multiple of the 8px cell size (27px,
    // 13px) to prove the drag snaps to the nearest grid cell instead of
    // landing at a sub-cell offset.
    const QPoint dragged = headerPoint + QPoint(27, 13);

    QTest::mousePress(cell, Qt::LeftButton, Qt::NoModifier, headerPoint);
    QTest::mouseMove(cell, dragged);
    QTest::mouseRelease(cell, Qt::LeftButton, Qt::NoModifier, dragged);

    QJsonObject item = grid.toJson().value("items").toArray().first().toObject();
    QCOMPARE(item.value("x").toDouble(), 3.0 / 60.0); // 27px snapped to 3 columns
    QCOMPARE(item.value("y").toDouble(), 2.0 / 40.0);  // 13px snapped to 2 rows

    QCOMPARE(grid.undoStack()->count(), 2); // AddWidgetCommand + MoveWidgetCommand
    grid.undoStack()->undo();
    item = grid.toJson().value("items").toArray().first().toObject();
    QCOMPARE(item.value("x").toDouble(), 0.0);
    QCOMPARE(item.value("y").toDouble(), 0.0);

    grid.undoStack()->redo();
    item = grid.toJson().value("items").toArray().first().toObject();
    QCOMPARE(item.value("x").toDouble(), 3.0 / 60.0);
}

void TestDashboardGrid::resizeChangesGeometryWithUndo() {
    DashboardGrid grid;
    grid.resize(496, 336); // same 480x320 usable / 8px-per-cell setup as the move test above
    grid.setEditMode(true);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    grid.addItem("dummy_line"); // default size 16/60 x 12/40 => 128x96px

    auto* cell = grid.findChild<DashboardCell*>();
    QVERIFY(cell != nullptr);

    // Bottom-right corner grip (14px hit area -- see kGripSize in dashboardcell.cpp).
    const QPoint gripPoint(123, 91);
    const QPoint dragged = gripPoint + QPoint(16, 8); // +2 columns, +1 row

    QTest::mousePress(cell, Qt::LeftButton, Qt::NoModifier, gripPoint);
    QTest::mouseMove(cell, dragged);
    QTest::mouseRelease(cell, Qt::LeftButton, Qt::NoModifier, dragged);

    QJsonObject item = grid.toJson().value("items").toArray().first().toObject();
    QCOMPARE(item.value("width").toDouble(), 18.0 / 60.0);
    QCOMPARE(item.value("height").toDouble(), 13.0 / 40.0);

    grid.undoStack()->undo();
    item = grid.toJson().value("items").toArray().first().toObject();
    QCOMPARE(item.value("width").toDouble(), 16.0 / 60.0);
    QCOMPARE(item.value("height").toDouble(), 12.0 / 40.0);
}

void TestDashboardGrid::resizeClampsToMinimumSize() {
    DashboardGrid grid;
    grid.resize(496, 336);
    grid.setEditMode(true);
    grid.show();
    QVERIFY(QTest::qWaitForWindowExposed(&grid));

    grid.addItem("dummy_line");
    auto* cell = grid.findChild<DashboardCell*>();
    QVERIFY(cell != nullptr);

    const QPoint gripPoint(123, 91);
    // Drag the corner far past the item's own top-left -- width/height must
    // clamp to the minimum instead of going to zero or negative.
    const QPoint dragged(1, 1);

    QTest::mousePress(cell, Qt::LeftButton, Qt::NoModifier, gripPoint);
    QTest::mouseMove(cell, dragged);
    QTest::mouseRelease(cell, Qt::LeftButton, Qt::NoModifier, dragged);

    const QJsonObject item = grid.toJson().value("items").toArray().first().toObject();
    QCOMPARE(item.value("width").toDouble(), 5.0 / 60.0);  // kMinItemWidth
    QCOMPARE(item.value("height").toDouble(), 5.0 / 40.0); // kMinItemHeight
}

} // namespace

QTEST_MAIN(TestDashboardGrid)
#include "test_dashboardgrid.moc"
