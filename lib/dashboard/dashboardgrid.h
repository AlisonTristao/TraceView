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

class DashboardWidget;

class AddWidgetCommand;
class RemoveWidgetCommand;
class MoveWidgetCommand;
class ResizeWidgetCommand;
class ChangeWidgetTypeCommand;
class RenameWidgetCommand;
class SetItemKeyCommand;
class SetItemConfigCommand;
class ChangeZOrderCommand;

// One entry in a front-to-back or back-to-front listing of dashboard items
// (see DashboardGrid::layerEntries()) -- just enough to populate a layers/
// checklist UI without exposing the rest of DashboardItem (config, geometry).
struct DashboardLayerEntry {
    QString id;
    QString displayName;
};

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
    friend class ChangeZOrderCommand;

public:
    explicit DashboardGrid(QWidget* parent = nullptr);

    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }

    // Broadcasts the app's single global serial connection state (see
    // core/serialmanager.h) to every cell's header status dot (see
    // DashboardCell::setConnected()) -- applied to existing cells immediately
    // and to any cell created afterward (see createCell()).
    void setDeviceConnected(bool connected);

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
    // Puts the selected item on the system clipboard (as a custom MIME
    // type) for a later pasteItem() call. No-op if nothing is selected.
    void copySelected() const;
    // True if the clipboard currently holds something copySelected() put
    // there (possibly from a previous run, or another TraceView window).
    bool canPaste() const;
    // Inserts a new item cloned from whatever copySelected() last put on
    // the clipboard, near the copied item's original spot if that space is
    // free, otherwise in the first free cell. No-op if the clipboard holds
    // nothing pasteable.
    void pasteItem();
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

    // Reorders the selected item's stacking position among overlapping
    // widgets (see DashboardLayerEntry / layerEntries()). No-op if nothing
    // is selected or it's already at that extreme of the stack.
    void bringSelectedToFront();
    void bringSelectedForward();
    void sendSelectedBackward();
    void sendSelectedToBack();

    QUndoStack* undoStack() const { return m_undoStack; }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    // One entry per item with a non-empty key, mapping that key to its
    // content widget (see DashboardCell::content()). Used by
    // SerialDataRouter (lib/core/serialdatarouter.h) to route decoded frames
    // by <id> -- rebuild after every itemsChanged() rather than caching,
    // since keys/widgets can change underneath.
    QMap<QString, DashboardWidget*> keyedWidgets() const;

    // Every item's id/display name, back-to-front (index 0 is the bottom of
    // the stack) -- the model-order counterpart to keyedWidgets() above, for
    // a UI that needs to list every item regardless of key (see
    // lib/core/layerspanel.h).
    QVector<DashboardLayerEntry> layerEntries() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // itemId is empty when the selection is cleared.
    void selectionChanged(const QString& itemId);
    // Fires whenever the key->widget association could have changed: item
    // added/removed, a key edited, a type change (swaps the content
    // widget), or a full project load. Move/resize/rename/config edits don't
    // touch this and don't emit it.
    void itemsChanged();
    // Fires exactly once per widget instance, right after createCell()
    // constructs it (fresh insert, project load, or a type change swapping
    // in a new instance -- see createCell()). Unlike itemsChanged(), this
    // isn't about the key index; it's a one-shot construction hook for
    // things that need to attach to a widget once and never re-scan the
    // grid (e.g. SerialWidgetBridge wiring a control's output to
    // SerialManager -- BACKEND_TODO.txt Task 9/10).
    void widgetCreated(DashboardWidget* widget);

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

    // Outer margin (canvas edge <-> widgets) and gutter (widget <-> widget)
    // both resolve to this same pixel value, computed from the canvas's own
    // size — see kGutterFraction in dashboardgrid.cpp for why.
    int gutter() const;
    QRect usableRect() const;
    // The item's full allotted cell, before the half-gutter inset that
    // itemRect() applies to leave visible breathing room around it. Painted
    // as a guide outline in edit mode (see paintEvent()) so the gutter
    // doesn't hide where the cell's actual bounds are while positioning it.
    QRect slotRect(const DashboardItem& item) const;
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
    // index is clamped to the current item count; a no-op if itemId is
    // unknown or already at that index.
    void applyZOrder(const QString& itemId, int index);

    // Replays m_items' order onto the live cells' Qt stacking order
    // (cell->raise() in back-to-front sequence) -- the single place that
    // keeps what's on screen matching the persisted layer order, used both
    // after a z-order mutation and to restore it once a temporary
    // selection-driven raise (see selectItem()) ends.
    void restackCells();

    // Custom name if set, otherwise the type's registered display name.
    QString displayNameFor(const DashboardItem& item) const;
    // True if no item other than `excludeId` already has `key` (an empty
    // key is always available — it just means "no key set").
    bool isKeyAvailable(const QString& key, const QString& excludeId) const;

    bool findFreeSlot(double width, double height, double* outX, double* outY) const;
    // Canvas-bounds check only -- overlapping another item is allowed. Used
    // to accept/reject an interactive drag or resize.
    bool isPlacementValid(const DashboardItem& candidate, const QString& excludeId) const;
    // isPlacementValid() plus "doesn't overlap any other item" -- used only
    // to find a genuinely empty spot for a brand-new or pasted item (nicer
    // default than dropping it right on top of something when there's free
    // space available); falls back to a spot that may overlap when there
    // isn't, same as before.
    bool isPlacementFree(const DashboardItem& candidate, const QString& excludeId) const;
    DashboardItem* itemById(const QString& itemId);
    const DashboardItem* itemById(const QString& itemId) const;
    int indexOfItem(const QString& itemId) const;

    DashboardCell* createCell(const DashboardItem& item);

    void handleDragStarted(const QString& itemId, const QPoint& globalPos);
    void handleDragMoved(const QString& itemId, const QPoint& globalPos);
    void handleDragFinished(const QString& itemId, const QPoint& globalPos);
    void handleResizeStarted(const QString& itemId, const QPoint& globalPos, DashboardCell::ResizeHandle handle);
    void handleResizeMoved(const QString& itemId, const QPoint& globalPos);
    void handleResizeFinished(const QString& itemId, const QPoint& globalPos);
    void handleSelectRequested(const QString& itemId);

    bool m_editMode = false;
    bool m_deviceConnected = false;
    QString m_selectedItemId;

    QVector<DashboardItem> m_items;
    QMap<QString, DashboardCell*> m_cells;

    std::optional<DragOp> m_drag;

    QUndoStack* m_undoStack;
};

} // namespace traceview
