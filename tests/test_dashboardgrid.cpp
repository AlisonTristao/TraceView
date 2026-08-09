#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>

#include "dashboard/dashboardcell.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/roundedcorners.h"
#include "dashboard/dashboardwidget.h"
#include "traceview/thememanager.h"

using traceview::DashboardCell;
using traceview::DashboardGrid;
using traceview::DashboardWidget;
using traceview::ThemeManager;

namespace {

constexpr QRgb kContentColor = qRgb(214, 44, 79);

class SolidContentWidget final : public DashboardWidget {
public:
    using DashboardWidget::DashboardWidget;

    bool wantsCellHeader() const override { return false; }
    QColor cellFillColor(const traceview::ThemePalette&) const override { return QColor::fromRgb(kContentColor); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor::fromRgb(kContentColor));
    }
};

int colorDistance(const QColor& a, const QColor& b) {
    return qAbs(a.red() - b.red()) + qAbs(a.green() - b.green()) + qAbs(a.blue() - b.blue());
}

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
    void cellHasNoIdleBorderAndSelectedBorderStaysAboveContent();
    void squareCornerPatchesDoNotCreateHoles();
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

void TestDashboardGrid::cellHasNoIdleBorderAndSelectedBorderStaysAboveContent() {
    ThemeManager& themes = ThemeManager::instance();
    struct RestoreTheme {
        QString id;
        ~RestoreTheme() { ThemeManager::instance().setTheme(id); }
    } restoreTheme{themes.currentTheme().id};

    for (const QString& themeId : {QString("dark"), QString("light")}) {
        themes.setTheme(themeId);
        const QColor forbiddenIdleBorder = themes.currentTheme().borderStrong;
        const QColor canvasColor = themes.currentTheme().background;
        const QColor selectedBorder = themes.currentTheme().accent;
        const QColor contentColor = QColor::fromRgb(kContentColor);

        auto* content = new SolidContentWidget;
        DashboardCell cell("test", "solid", "Solid", content);
        cell.resize(80, 60);
        cell.show();
        QCoreApplication::processEvents();

        QImage rendered(cell.size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        cell.render(&rendered);

        // Idle cells have no outer stroke. In particular, no borderStrong
        // fragments may survive around the rounded mask as they did before.
        const QColor idleTopEdge = rendered.pixelColor(cell.width() / 2, 0);
        QVERIFY2(colorDistance(idleTopEdge, contentColor) < colorDistance(idleTopEdge, forbiddenIdleBorder),
                 qPrintable(QString("theme=%1 idle-edge=%2 forbidden-border=%3 content=%4")
                                .arg(themeId, idleTopEdge.name(QColor::HexArgb),
                                     forbiddenIdleBorder.name(QColor::HexArgb), contentColor.name(QColor::HexArgb))));
        const QColor idleCornerCurve = rendered.pixelColor(4, 4);
        QVERIFY2(colorDistance(idleCornerCurve, contentColor) < colorDistance(idleCornerCurve, canvasColor),
                 qPrintable(QString("theme=%1 corner=%2 canvas=%3 content=%4")
                                .arg(themeId, idleCornerCurve.name(QColor::HexArgb),
                                     canvasColor.name(QColor::HexArgb), contentColor.name(QColor::HexArgb))));

        cell.setEditMode(true);
        cell.setSelected(true);
        QTest::qWait(180); // selection outline animation is 150ms

        rendered.fill(Qt::transparent);
        cell.render(&rendered);
        const QColor selectedTopEdge = rendered.pixelColor(cell.width() / 2, 0);
        QVERIFY2(colorDistance(selectedTopEdge, selectedBorder) < colorDistance(selectedTopEdge, contentColor),
                 qPrintable(QString("theme=%1 selected-edge=%2 accent=%3 content=%4")
                                .arg(themeId, selectedTopEdge.name(QColor::HexArgb),
                                     selectedBorder.name(QColor::HexArgb), contentColor.name(QColor::HexArgb))));

    }
}

void TestDashboardGrid::squareCornerPatchesDoNotCreateHoles() {
    const QPainterPath path =
        traceview::partiallyRoundedRect(QRectF(0, 0, 80, 60), 12.0, false, true, true, true);

    // Both points belong to a deliberately square top-left corner. With the
    // default OddEvenFill, the second point sits in the overlap between the
    // rounded base and square patch and becomes a visible radius-sized hole.
    QVERIFY(path.contains(QPointF(1, 1)));
    QVERIFY(path.contains(QPointF(8, 8)));
}

} // namespace

QTEST_MAIN(TestDashboardGrid)
#include "test_dashboardgrid.moc"
