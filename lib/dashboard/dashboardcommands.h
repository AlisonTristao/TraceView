#pragma once

#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QUndoCommand>
#include <QVector>

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

// Removes several items (a multi-selection) as one undo step. Deliberately
// not built out of N RemoveWidgetCommand pushes wrapped in a QUndoStack
// macro: RemoveWidgetCommand::undo() unconditionally re-selects just the one
// item it restores, so undoing a macro of several would leave only the
// last-undone (first-pushed) item selected instead of the whole original
// multi-selection. This command instead reinserts every item in one redo/
// undo call and selects the whole restored set exactly once.
class RemoveWidgetsCommand : public QUndoCommand {
public:
    RemoveWidgetsCommand(DashboardGrid* grid, const QVector<DashboardItem>& items);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QVector<DashboardItem> m_items;
};

// Moves one or more items together as one undo step -- a normal single-item
// drag just produces a 1-entry map, so this also replaces what used to be a
// dedicated single-item MoveWidgetCommand.
class MoveWidgetsCommand : public QUndoCommand {
public:
    MoveWidgetsCommand(DashboardGrid* grid, const QMap<QString, QPointF>& fromPositions,
                       const QMap<QString, QPointF>& toPositions);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QMap<QString, QPointF> m_fromPositions;
    QMap<QString, QPointF> m_toPositions;
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
    SetItemKeyCommand(DashboardGrid* grid, const QString& itemId, const QString& fromKey,
                      const QString& toKey);

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

class ChangeZOrderCommand : public QUndoCommand {
public:
    // fromIndex/toIndex are positions in DashboardGrid's back-to-front item
    // order (see DashboardGrid::layerEntries()). label becomes the command's
    // text (e.g. "Bring to Front"), shown in the Undo/Redo action.
    ChangeZOrderCommand(DashboardGrid* grid, const QString& itemId, int fromIndex, int toIndex,
                        const QString& label);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QString m_itemId;
    int m_fromIndex;
    int m_toIndex;
};

// Welds the given items' relative positions together (see
// DashboardGrid::groupSelected()). previousGroupIds stores each item's prior
// groupId individually (not just "ungrouped") so undo correctly restores
// items that were re-grouped out of different existing groups.
class GroupItemsCommand : public QUndoCommand {
public:
    GroupItemsCommand(DashboardGrid* grid, const QMap<QString, QString>& previousGroupIds,
                      const QString& newGroupId);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QMap<QString, QString> m_previousGroupIds;
    QString m_newGroupId;
};

// Clears the given items' group membership (see
// DashboardGrid::ungroupSelected()); previousGroupIds restores each on undo.
class UngroupItemsCommand : public QUndoCommand {
public:
    UngroupItemsCommand(DashboardGrid* grid, const QMap<QString, QString>& previousGroupIds);

    void undo() override;
    void redo() override;

private:
    DashboardGrid* m_grid;
    QMap<QString, QString> m_previousGroupIds;
};

}  // namespace traceview
