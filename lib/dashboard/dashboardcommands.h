#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QRectF>
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
    // Geometry is position + size (both fractions of the canvas) since
    // resizing from a top/left edge or corner moves the anchored position
    // along with the size, not just the size.
    ResizeWidgetCommand(DashboardGrid* grid, const QString& itemId, const QRectF& fromGeometry,
                         const QRectF& toGeometry);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QRectF m_fromGeometry;
    QRectF m_toGeometry;
};

class RenameWidgetCommand : public QUndoCommand {
public:
    RenameWidgetCommand(DashboardGrid* grid, const QString& itemId, const QString& fromName,
                         const QString& toName);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QString m_fromName;
    QString m_toName;
};

class SetItemKeyCommand : public QUndoCommand {
public:
    SetItemKeyCommand(DashboardGrid* grid, const QString& itemId, const QString& fromKey, const QString& toKey);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QString m_fromKey;
    QString m_toKey;
};

class SetItemConfigCommand : public QUndoCommand {
public:
    SetItemConfigCommand(DashboardGrid* grid, const QString& itemId, const QJsonObject& fromConfig,
                          const QJsonObject& toConfig);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    QJsonObject m_fromConfig;
    QJsonObject m_toConfig;
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
