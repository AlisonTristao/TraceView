#pragma once

#include <optional>

#include <QJsonObject>
#include <QMap>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <QWidget>

#include "dashboarditem.h"

namespace traceview {

class DashboardCell;

// A Bootstrap-like grid of DashboardWidgets: a fixed, small square cell
// size (in pixels) that the canvas is divided into, each item occupying an
// integer-cell rect. Cells are positioned manually (no QLayout) so they can
// be dragged/resized with the mouse in edit mode. The cell size is not
// user-configurable — keeping it fixed and integer-based is what keeps
// items pixel-exact on the grid lines (no rounding math involved).
//
// Interaction in edit mode is selection-based: clicking a cell selects it
// (only one at a time, grid-wide); the selected cell can be dragged/resized,
// and removeSelected()/changeSelectedType() act on whichever one that is.
class DashboardGrid : public QWidget {
    Q_OBJECT

public:
    explicit DashboardGrid(QWidget* parent = nullptr);

    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }

    void selectItem(const QString& itemId); // empty string clears selection
    QString selectedItemId() const { return m_selectedItemId; }
    // Type of the selected item, or empty if nothing is selected.
    QString selectedItemTypeId() const;

    // Auto-places a new item of `typeId` in the first free cell.
    void addItem(const QString& typeId);
    // No-op if nothing is selected.
    void removeSelected();
    // Swaps the selected item's widget for a new instance of `newTypeId`,
    // keeping its position/size. No-op if nothing is selected, the type is
    // unknown, or unchanged.
    void changeSelectedType(const QString& newTypeId);

    void undo();
    void redo();
    bool canUndo() const { return !m_undoStack.isEmpty(); }
    bool canRedo() const { return !m_redoStack.isEmpty(); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // itemId is empty when the selection is cleared.
    void selectionChanged(const QString& itemId);
    void historyChanged();

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
    };

    QRect usableRect() const;
    int columns() const;
    int rows() const;
    QRect cellRect(const DashboardItem& item) const;
    QSize contentSize() const;

    void relayout();
    void relayoutItem(const QString& itemId);
    void clearItems();
    void removeItem(const QString& itemId);
    void pushUndoSnapshot();

    bool findFreeSlot(int columnSpan, int rowSpan, int* outColumn, int* outRow) const;
    bool isPlacementValid(const DashboardItem& candidate, const QString& excludeId) const;
    DashboardItem* itemById(const QString& itemId);
    const DashboardItem* itemById(const QString& itemId) const;

    DashboardCell* createCell(const DashboardItem& item);

    void handleDragStarted(const QString& itemId, const QPoint& globalPos);
    void handleDragMoved(const QString& itemId, const QPoint& globalPos);
    void handleDragFinished(const QString& itemId, const QPoint& globalPos);
    void handleResizeStarted(const QString& itemId, const QPoint& globalPos);
    void handleResizeMoved(const QString& itemId, const QPoint& globalPos);
    void handleResizeFinished(const QString& itemId, const QPoint& globalPos);
    void handleSelectRequested(const QString& itemId);

    bool m_editMode = false;
    QString m_selectedItemId;

    QVector<DashboardItem> m_items;
    QMap<QString, DashboardCell*> m_cells;

    std::optional<DragOp> m_drag;

    QVector<QJsonObject> m_undoStack;
    QVector<QJsonObject> m_redoStack;
};

} // namespace traceview
