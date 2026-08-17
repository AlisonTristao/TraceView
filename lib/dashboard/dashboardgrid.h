#pragma once

#include <optional>

#include <QJsonObject>
#include <QMap>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QUndoStack>
#include <QVector>
#include <QWidget>

#include "dashboardcell.h"
#include "dashboarditem.h"

class QRubberBand;

namespace traceview {

class DashboardWidget;

class AddWidgetCommand;
class RemoveWidgetCommand;
class RemoveWidgetsCommand;
class MoveWidgetsCommand;
class ResizeWidgetCommand;
class ChangeWidgetTypeCommand;
class RenameWidgetCommand;
class SetItemKeyCommand;
class SetItemConfigCommand;
class ChangeZOrderCommand;
class GroupItemsCommand;
class UngroupItemsCommand;

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
// (replacing the current selection); Ctrl-click toggles it in/out of a
// multi-selection instead, and dragging a rubber-band rectangle over empty
// canvas selects everything it touches (see toggleItemSelection()/
// selectItems()). Items sharing a non-empty DashboardItem::groupId (see
// groupSelected()/ungroupSelected()) always select and drag together as one
// rigid unit, no matter which member was clicked/dragged. removeSelected()/
// changeSelectedType() and friends act on "the" selected item and are only
// meaningful when exactly one item is selected (see selectedItemId()).
class DashboardGrid : public QWidget {
    Q_OBJECT

    friend class AddWidgetCommand;
    friend class RemoveWidgetCommand;
    friend class RemoveWidgetsCommand;
    friend class MoveWidgetsCommand;
    friend class ResizeWidgetCommand;
    friend class ChangeWidgetTypeCommand;
    friend class RenameWidgetCommand;
    friend class SetItemKeyCommand;
    friend class SetItemConfigCommand;
    friend class ChangeZOrderCommand;
    friend class GroupItemsCommand;
    friend class UngroupItemsCommand;

public:
    explicit DashboardGrid(QWidget* parent = nullptr);

    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }

    // Broadcasts the app's single global serial connection state (see
    // core/serialmanager.h) to every cell's header status dot (see
    // DashboardCell::setConnected()) -- applied to existing cells immediately
    // and to any cell created afterward (see createCell()).
    void setDeviceConnected(bool connected);

    // Replaces the current selection with just `itemId` (or clears it, for
    // an empty string) -- expanded to the item's whole group if it belongs
    // to one (see expandGroups()). Kept as a single-item Replace call with
    // this exact signature since it's connected via &DashboardGrid::selectItem
    // pointer syntax elsewhere; see toggleItemSelection()/selectItems() for
    // the multi-select entry points.
    void selectItem(const QString& itemId); // empty string clears selection
    // Ctrl-click entry point: adds/removes itemId (or its whole group, if
    // grouped) to/from the current selection as one unit.
    void toggleItemSelection(const QString& itemId);
    // Rubber-band entry point: replaces (or, if add=true, adds to) the
    // current selection with `ids`, each expanded to its full group.
    void selectItems(const QSet<QString>& ids, bool add);
    // The sole selected item's id, or empty if zero or 2+ items are
    // selected -- every single-item operation below (rename, type/key/config
    // edit, copy, z-order) only acts when this is non-empty.
    QString selectedItemId() const;
    int selectedCount() const { return m_selectedItemIds.size(); }
    QSet<QString> selectedItemIds() const { return m_selectedItemIds; }
    // True if any selected item belongs to a group -- drives the Ungroup
    // ribbon action's enabled state.
    bool selectionHasGroup() const;
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
    // No-op if nothing is selected; removes every selected item as one undo
    // step when more than one is selected.
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

    // Locks the selected items' positions together as a group: from then on,
    // selecting or dragging any member acts on the whole group at once (see
    // expandGroups()). No-op if fewer than 2 items are selected.
    void groupSelected();
    // Clears the group membership of every selected item that has one.
    void ungroupSelected();

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
    // itemId is empty when the selection is cleared; also empty (not the
    // clicked item) while 2+ items are selected -- see selectedItemId().
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
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    struct DragOp {
        QString primaryItemId; // the cell the mouse is actually tracking;
                                // used to compute the drag delta
        QPoint startGlobalPos;
        // Keyed by itemId -- more than one entry only while dragging a
        // multi-selection or group; always exactly one entry while resizing
        // (resize stays single-item, see DashboardCell::setResizable()).
        QMap<QString, DashboardItem> originals;
        QMap<QString, DashboardItem> candidates;
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
    void applySetGroup(const QString& itemId, const QString& groupId);

    // Replays m_items' order onto the live cells' Qt stacking order
    // (cell->raise() in back-to-front sequence) -- the single place that
    // keeps what's on screen matching the persisted layer order, used both
    // after a z-order mutation and to restore it once a temporary
    // selection-driven raise (see applySelection()) ends.
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

    // If any id in `ids` belongs to a (non-empty) group, expands the result
    // to include every item sharing that group -- the single place the
    // "a group always acts as one rigid unit" rule is enforced, used by
    // every selection entry point (selectItem/toggleItemSelection/
    // selectItems).
    QSet<QString> expandGroups(const QSet<QString>& ids) const;
    // Core selection mutator: diffs `target` against m_selectedItemIds,
    // updates each affected cell's selected/resizable state, restacks/raises
    // the selection, and emits selectionChanged() -- every public selection
    // entry point funnels through this.
    void applySelection(const QSet<QString>& target);
    // Resize is single-item-only: disables DashboardCell's resize grip on
    // every selected cell whenever more than one item is selected, re-enables
    // it once the selection drops back to <= 1 (see DashboardCell::setResizable()).
    void updateResizableFlags();

    void handleDragStarted(const QString& itemId, const QPoint& globalPos);
    void handleDragMoved(const QString& itemId, const QPoint& globalPos);
    void handleDragFinished(const QString& itemId, const QPoint& globalPos);
    void handleResizeStarted(const QString& itemId, const QPoint& globalPos, DashboardCell::ResizeHandle handle);
    void handleResizeMoved(const QString& itemId, const QPoint& globalPos);
    void handleResizeFinished(const QString& itemId, const QPoint& globalPos);
    void handleSelectRequested(const QString& itemId, Qt::KeyboardModifiers modifiers);

    bool m_editMode = false;
    bool m_deviceConnected = false;
    QSet<QString> m_selectedItemIds;

    QVector<DashboardItem> m_items;
    QMap<QString, DashboardCell*> m_cells;

    std::optional<DragOp> m_drag;

    // Rubber-band select-by-dragging-over-empty-canvas gesture (see
    // mousePressEvent()/mouseMoveEvent()/mouseReleaseEvent()). m_rubberBand
    // is only actually created once the drag exceeds
    // QApplication::startDragDistance() -- a press that never moves that far
    // is treated as a plain click (clears the selection), same as before
    // this feature existed.
    QRubberBand* m_rubberBand = nullptr;
    QPoint m_rubberBandOrigin;
    bool m_rubberBandAdditive = false;
    bool m_rubberBandPending = false;

    QUndoStack* m_undoStack;
};

} // namespace traceview
