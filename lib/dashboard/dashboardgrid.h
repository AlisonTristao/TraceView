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

// A Bootstrap-like grid of DashboardWidgets: a fixed column/row count, each
// item occupying a row/column rect. Cells are positioned manually (no
// QLayout) so they can be dragged/resized with the mouse in edit mode.
//
// Both columns and rows are driven by the widget's current size (divided
// evenly), so the grid always exactly fills the visible area — it never
// scrolls.
class DashboardGrid : public QWidget {
    Q_OBJECT

public:
    explicit DashboardGrid(QWidget* parent = nullptr);

    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }

    void setRemoveMode(bool enabled);
    void setTypeEditMode(bool enabled);

    // Auto-places a new item of `typeId` in the first free cell.
    void addItem(const QString& typeId);
    void removeItem(const QString& itemId);
    // Swaps the widget at `itemId` for a new instance of `newTypeId`,
    // keeping its position/span. No-op if the type is unknown or unchanged.
    void changeItemType(const QString& itemId, const QString& newTypeId);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // Emitted when the user clicks a widget while in type-edit mode.
    // MainWindow owns the type picker UI; it should call changeItemType()
    // once the user picks a new type.
    void widgetTypeEditRequested(const QString& itemId, const QString& currentTypeId);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    struct DragOp {
        QString itemId;
        QPoint startGlobalPos;
        DashboardItem original;
        DashboardItem candidate;
        bool resizing = false;
    };

    qreal cellWidth() const;
    qreal cellHeight() const;
    qreal columnStep() const;
    qreal rowStep() const;
    QRect cellRect(const DashboardItem& item) const;
    QSize contentSize() const;

    void relayout();
    void relayoutItem(const QString& itemId);
    void clearItems();

    bool findFirstFreeCell(int rowSpan, int columnSpan, int* outRow, int* outColumn) const;
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
    void handleRemoveRequested(const QString& itemId);
    void handleTypeEditRequested(const QString& itemId);

    int m_columns;
    int m_rows;
    bool m_editMode = false;
    bool m_removeMode = false;
    bool m_typeEditMode = false;

    QVector<DashboardItem> m_items;
    QMap<QString, DashboardCell*> m_cells;

    std::optional<DragOp> m_drag;
};

} // namespace traceview
