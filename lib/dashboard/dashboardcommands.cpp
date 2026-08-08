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

ResizeWidgetCommand::ResizeWidgetCommand(DashboardGrid* grid, const QString& itemId, const QSizeF& fromSize,
                                          const QSizeF& toSize)
    : QUndoCommand("Resize Widget"), m_grid(grid), m_itemId(itemId), m_fromSize(fromSize), m_toSize(toSize) {}

void ResizeWidgetCommand::redo() {
    m_grid->applyResize(m_itemId, m_toSize);
}

void ResizeWidgetCommand::undo() {
    m_grid->applyResize(m_itemId, m_fromSize);
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
