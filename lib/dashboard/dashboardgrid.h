#pragma once

#include <optional>

#include <QJsonObject>
#include <QMap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QUndoStack>
#include <QVector>
#include <QWidget>

#include "dashboardcell.h"
#include "dashboarditem.h"

namespace traceview {

class AddWidgetCommand;
class RemoveWidgetCommand;
class MoveWidgetCommand;
class ResizeWidgetCommand;
class ChangeWidgetTypeCommand;
class RenameWidgetCommand;
class SetItemKeyCommand;
class SetItemConfigCommand;

// A Bootstrap-like grid of DashboardWidgets: each item's position/size is
// stored as fractions (0.0-1.0) of the canvas area, so layouts stay
// resolution-independent — an item keeps its relative spot/size across
// resizes with no clamping or reflow needed. A fixed logical division count
// (see kGridColumns/kGridRows in the .cpp) drives grid-line painting and
// drag/resize snapping only; it is not part of the persisted model. Cells
// are positioned manually (no QLayout) so they can be dragged/resized with
// the mouse in edit mode.
//
// Interaction in edit mode is selection-based: clicking a cell selects it
// (only one at a time, grid-wide); the selected cell can be dragged/resized,
// and removeSelected()/changeSelectedType() act on whichever one that is.
class DashboardGrid : public QWidget {
    Q_OBJECT

    friend class AddWidgetCommand;
    friend class RemoveWidgetCommand;
    friend class MoveWidgetCommand;
    friend class ResizeWidgetCommand;
    friend class ChangeWidgetTypeCommand;
    friend class RenameWidgetCommand;
    friend class SetItemKeyCommand;
    friend class SetItemConfigCommand;

public:
    explicit DashboardGrid(QWidget* parent = nullptr);

    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }

    void selectItem(const QString& itemId); // empty string clears selection
    QString selectedItemId() const { return m_selectedItemId; }
    // Type of the selected item, or empty if nothing is selected.
    QString selectedItemTypeId() const;
    // Effective display name of the selected item (its custom name, or the
    // type's default if none was set), or empty if nothing is selected.
    QString selectedItemDisplayName() const;
    // The selected item's user-defined key (may be empty), or empty if
    // nothing is selected.
    QString selectedItemKey() const;
    // The selected item's type-specific config (see WidgetConfigEditor), or
    // an empty object if nothing is selected.
    QJsonObject selectedItemConfig() const;

    // Auto-places a new item of `typeId` in the first free cell.
    void addItem(const QString& typeId);
    // No-op if nothing is selected.
    void removeSelected();
    // Swaps the selected item's widget for a new instance of `newTypeId`,
    // keeping its position/size. No-op if nothing is selected, the type is
    // unknown, or unchanged.
    void changeSelectedType(const QString& newTypeId);
    // Sets the selected item's custom display name (pass an empty string to
    // revert to the type's default name). No-op if nothing is selected or
    // the name is unchanged.
    void renameSelected(const QString& newName);
    // Sets the selected item's key (pass an empty string to clear it).
    // Returns false without applying anything if nothing is selected or
    // `newKey` is already used by another item; returns true (including as
    // a no-op) otherwise.
    bool setSelectedKey(const QString& newKey);
    // Sets the selected item's type-specific config. No-op if nothing is
    // selected or the config is unchanged.
    void changeSelectedConfig(const QJsonObject& newConfig);

    QUndoStack* undoStack() const { return m_undoStack; }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // itemId is empty when the selection is cleared.
    void selectionChanged(const QString& itemId);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct DragOp {
        QString itemId;
        QPoint startGlobalPos;
        DashboardItem original;
        DashboardItem candidate;
        bool resizing = false;
        DashboardCell::ResizeHandle handle = DashboardCell::ResizeHandle::None;
    };

    QRect usableRect() const;
    QRect itemRect(const DashboardItem& item) const;
    QSize contentSize() const;

    void relayout();
    void relayoutItem(const QString& itemId);
    void clearItems();
    void removeItem(const QString& itemId);

    // Mutators used by the QUndoCommand subclasses in dashboardcommands.h
    // to apply/unapply one committed change. DashboardGrid stays the only
    // place that actually touches m_items/m_cells; the commands just decide
    // which direction (redo/undo) to call these with.
    void applyInsertItem(const DashboardItem& item);
    void applyRemoveItemById(const QString& itemId);
    void applyMove(const QString& itemId, const QPointF& position);
    // geometry.x()/y()/width()/height() are all fractions (0.0-1.0) of the
    // canvas, matching DashboardItem — a QRectF here since edge/corner
    // resizing can move the anchored position, not just the size.
    void applyResize(const QString& itemId, const QRectF& geometry);
    void applyTypeChange(const QString& itemId, const QString& typeId);
    void applyRename(const QString& itemId, const QString& name);
    void applySetKey(const QString& itemId, const QString& key);
    void applySetConfig(const QString& itemId, const QJsonObject& config);

    // Custom name if set, otherwise the type's registered display name.
    QString displayNameFor(const DashboardItem& item) const;
    // True if no item other than `excludeId` already has `key` (an empty
    // key is always available — it just means "no key set").
    bool isKeyAvailable(const QString& key, const QString& excludeId) const;

    bool findFreeSlot(double width, double height, double* outX, double* outY) const;
    bool isPlacementValid(const DashboardItem& candidate, const QString& excludeId) const;
    DashboardItem* itemById(const QString& itemId);
    const DashboardItem* itemById(const QString& itemId) const;

    DashboardCell* createCell(const DashboardItem& item);

    void handleDragStarted(const QString& itemId, const QPoint& globalPos);
    void handleDragMoved(const QString& itemId, const QPoint& globalPos);
    void handleDragFinished(const QString& itemId, const QPoint& globalPos);
    void handleResizeStarted(const QString& itemId, const QPoint& globalPos, DashboardCell::ResizeHandle handle);
    void handleResizeMoved(const QString& itemId, const QPoint& globalPos);
    void handleResizeFinished(const QString& itemId, const QPoint& globalPos);
    void handleSelectRequested(const QString& itemId);

    bool m_editMode = false;
    QString m_selectedItemId;

    QVector<DashboardItem> m_items;
    QMap<QString, DashboardCell*> m_cells;

    std::optional<DragOp> m_drag;

    QUndoStack* m_undoStack;
};

} // namespace traceview
