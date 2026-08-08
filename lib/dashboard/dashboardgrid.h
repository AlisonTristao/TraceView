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
// Columns are width-driven (recomputed from the widget's current width, so
// they respond to window resizing); rows use a fixed pixel height.
class DashboardGrid : public QWidget {
public:
    explicit DashboardGrid(QWidget* parent = nullptr);

    void setEditMode(bool enabled);
    bool editMode() const { return m_editMode; }

    // Auto-places a new item of `typeId` in the first free cell.
    void addItem(const QString& typeId);
    void removeItem(const QString& itemId);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

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
    qreal columnStep() const;
    int rowStep() const;
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

    int m_columns;
    int m_rows;
    bool m_editMode = false;

    QVector<DashboardItem> m_items;
    QMap<QString, DashboardCell*> m_cells;

    std::optional<DragOp> m_drag;
};

} // namespace traceview
