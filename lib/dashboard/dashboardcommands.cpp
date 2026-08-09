#include "dashboardcommands.h"

#include "dashboardgrid.h"

namespace traceview {

AddWidgetCommand::AddWidgetCommand(DashboardGrid* grid, const DashboardItem& item)
    : QUndoCommand("Add Widget"), m_grid(grid), m_item(item) {}

void AddWidgetCommand::redo() {
    m_grid->applyInsertItem(m_item);
}

void AddWidgetCommand::undo() {
    m_grid->applyRemoveItemById(m_item.id);
}

RemoveWidgetCommand::RemoveWidgetCommand(DashboardGrid* grid, const DashboardItem& item)
    : QUndoCommand("Remove Widget"), m_grid(grid), m_item(item) {}

void RemoveWidgetCommand::redo() {
    m_grid->applyRemoveItemById(m_item.id);
}

void RemoveWidgetCommand::undo() {
    m_grid->applyInsertItem(m_item);
    m_grid->selectItem(m_item.id);
}

MoveWidgetCommand::MoveWidgetCommand(DashboardGrid* grid, const QString& itemId, const QPointF& fromPosition,
                                      const QPointF& toPosition)
    : QUndoCommand("Move Widget"), m_grid(grid), m_itemId(itemId), m_fromPosition(fromPosition),
      m_toPosition(toPosition) {}

void MoveWidgetCommand::redo() {
    m_grid->applyMove(m_itemId, m_toPosition);
}

void MoveWidgetCommand::undo() {
    m_grid->applyMove(m_itemId, m_fromPosition);
}

ResizeWidgetCommand::ResizeWidgetCommand(DashboardGrid* grid, const QString& itemId, const QRectF& fromGeometry,
                                          const QRectF& toGeometry)
    : QUndoCommand("Resize Widget"), m_grid(grid), m_itemId(itemId), m_fromGeometry(fromGeometry),
      m_toGeometry(toGeometry) {}

void ResizeWidgetCommand::redo() {
    m_grid->applyResize(m_itemId, m_toGeometry);
}

void ResizeWidgetCommand::undo() {
    m_grid->applyResize(m_itemId, m_fromGeometry);
}

RenameWidgetCommand::RenameWidgetCommand(DashboardGrid* grid, const QString& itemId, const QString& fromName,
                                          const QString& toName)
    : QUndoCommand("Rename Widget"), m_grid(grid), m_itemId(itemId), m_fromName(fromName), m_toName(toName) {}

void RenameWidgetCommand::redo() {
    m_grid->applyRename(m_itemId, m_toName);
}

void RenameWidgetCommand::undo() {
    m_grid->applyRename(m_itemId, m_fromName);
}

SetItemKeyCommand::SetItemKeyCommand(DashboardGrid* grid, const QString& itemId, const QString& fromKey,
                                      const QString& toKey)
    : QUndoCommand("Set Widget Key"), m_grid(grid), m_itemId(itemId), m_fromKey(fromKey), m_toKey(toKey) {}

void SetItemKeyCommand::redo() {
    m_grid->applySetKey(m_itemId, m_toKey);
}

void SetItemKeyCommand::undo() {
    m_grid->applySetKey(m_itemId, m_fromKey);
}

SetItemConfigCommand::SetItemConfigCommand(DashboardGrid* grid, const QString& itemId, const QJsonObject& fromConfig,
                                            const QJsonObject& toConfig)
    : QUndoCommand("Set Widget Config"), m_grid(grid), m_itemId(itemId), m_fromConfig(fromConfig),
      m_toConfig(toConfig) {}

void SetItemConfigCommand::redo() {
    m_grid->applySetConfig(m_itemId, m_toConfig);
}

void SetItemConfigCommand::undo() {
    m_grid->applySetConfig(m_itemId, m_fromConfig);
}

ChangeWidgetTypeCommand::ChangeWidgetTypeCommand(DashboardGrid* grid, const QString& itemId,
                                                  const QString& fromTypeId, const QString& toTypeId)
    : QUndoCommand("Change Widget Type"), m_grid(grid), m_itemId(itemId), m_fromTypeId(fromTypeId),
      m_toTypeId(toTypeId) {}

void ChangeWidgetTypeCommand::redo() {
    m_grid->applyTypeChange(m_itemId, m_toTypeId);
}

void ChangeWidgetTypeCommand::undo() {
    m_grid->applyTypeChange(m_itemId, m_fromTypeId);
}

} // namespace traceview
