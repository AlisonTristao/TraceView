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

MoveWidgetCommand::MoveWidgetCommand(DashboardGrid* grid, const QString& itemId, const QPoint& fromCell,
                                      const QPoint& toCell)
    : QUndoCommand("Move Widget"), m_grid(grid), m_itemId(itemId), m_fromCell(fromCell), m_toCell(toCell) {}

void MoveWidgetCommand::redo() {
    m_grid->applyMove(m_itemId, m_toCell);
}

void MoveWidgetCommand::undo() {
    m_grid->applyMove(m_itemId, m_fromCell);
}

ResizeWidgetCommand::ResizeWidgetCommand(DashboardGrid* grid, const QString& itemId, const QSize& fromSpan,
                                          const QSize& toSpan)
    : QUndoCommand("Resize Widget"), m_grid(grid), m_itemId(itemId), m_fromSpan(fromSpan), m_toSpan(toSpan) {}

void ResizeWidgetCommand::redo() {
    m_grid->applyResize(m_itemId, m_toSpan);
}

void ResizeWidgetCommand::undo() {
    m_grid->applyResize(m_itemId, m_fromSpan);
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
