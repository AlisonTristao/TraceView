#pragma once

#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QUndoCommand>

#include "dashboarditem.h"

namespace traceview {

class DashboardGrid;

// One QUndoCommand per DashboardGrid mutation, replacing whole-dashboard
// JSON snapshots. Each command only stores the small bit of state its
// operation touches and forwards to a DashboardGrid::apply*() helper — the
// grid remains the single place that knows how to mutate its item/cell
// state; these classes are just undo/redo bookkeeping around that.

class AddWidgetCommand : public QUndoCommand {
public:
    AddWidgetCommand(DashboardGrid* grid, const DashboardItem& item);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    DashboardItem m_item;
};

class RemoveWidgetCommand : public QUndoCommand {
public:
    RemoveWidgetCommand(DashboardGrid* grid, const DashboardItem& item);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    DashboardItem m_item;
};

class MoveWidgetCommand : public QUndoCommand {
public:
    MoveWidgetCommand(DashboardGrid* grid, const QString& itemId, const QPointF& fromPosition,
                       const QPointF& toPosition);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QPointF m_fromPosition;
    QPointF m_toPosition;
};

class ResizeWidgetCommand : public QUndoCommand {
public:
    ResizeWidgetCommand(DashboardGrid* grid, const QString& itemId, const QSizeF& fromSize, const QSizeF& toSize);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QSizeF m_fromSize;
    QSizeF m_toSize;
};

class ChangeWidgetTypeCommand : public QUndoCommand {
public:
    ChangeWidgetTypeCommand(DashboardGrid* grid, const QString& itemId, const QString& fromTypeId,
                             const QString& toTypeId);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QString m_fromTypeId;
    QString m_toTypeId;
};

} // namespace traceview
