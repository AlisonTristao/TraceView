#include "dashboardcommands.h"

#include <QCoreApplication>
#include <QSet>

#include "dashboardgrid.h"

namespace traceview {

// These command classes derive from QUndoCommand, not QObject, so tr() isn't
// available here; use QCoreApplication::translate() with the owning class as
// context instead.
AddWidgetCommand::AddWidgetCommand(DashboardGrid* grid, const DashboardItem& item)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Add Widget")), m_grid(grid), m_item(item) {}

void AddWidgetCommand::redo() {
    m_grid->applyInsertItem(m_item);
}

void AddWidgetCommand::undo() {
    m_grid->applyRemoveItemById(m_item.id);
}

RemoveWidgetCommand::RemoveWidgetCommand(DashboardGrid* grid, const DashboardItem& item)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Remove Widget")), m_grid(grid), m_item(item) {}

void RemoveWidgetCommand::redo() {
    m_grid->applyRemoveItemById(m_item.id);
}

void RemoveWidgetCommand::undo() {
    m_grid->applyInsertItem(m_item);
    m_grid->selectItem(m_item.id);
}

RemoveWidgetsCommand::RemoveWidgetsCommand(DashboardGrid* grid, const QVector<DashboardItem>& items)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Remove Widgets")), m_grid(grid),
      m_items(items) {}

void RemoveWidgetsCommand::redo() {
    for (const DashboardItem& item : m_items) {
        m_grid->applyRemoveItemById(item.id);
    }
}

void RemoveWidgetsCommand::undo() {
    QSet<QString> ids;
    for (const DashboardItem& item : m_items) {
        m_grid->applyInsertItem(item);
        ids.insert(item.id);
    }
    m_grid->selectItems(ids, /*add=*/false);
}

MoveWidgetsCommand::MoveWidgetsCommand(DashboardGrid* grid, const QMap<QString, QPointF>& fromPositions,
                                        const QMap<QString, QPointF>& toPositions)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Move Widget")), m_grid(grid),
      m_fromPositions(fromPositions), m_toPositions(toPositions) {}

void MoveWidgetsCommand::redo() {
    for (auto it = m_toPositions.constBegin(); it != m_toPositions.constEnd(); ++it) {
        m_grid->applyMove(it.key(), it.value());
    }
}

void MoveWidgetsCommand::undo() {
    for (auto it = m_fromPositions.constBegin(); it != m_fromPositions.constEnd(); ++it) {
        m_grid->applyMove(it.key(), it.value());
    }
}

ResizeWidgetCommand::ResizeWidgetCommand(DashboardGrid* grid, const QString& itemId, const QRectF& fromGeometry,
                                          const QRectF& toGeometry)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Resize Widget")), m_grid(grid),
      m_itemId(itemId), m_fromGeometry(fromGeometry), m_toGeometry(toGeometry) {}

void ResizeWidgetCommand::redo() {
    m_grid->applyResize(m_itemId, m_toGeometry);
}

void ResizeWidgetCommand::undo() {
    m_grid->applyResize(m_itemId, m_fromGeometry);
}

RenameWidgetCommand::RenameWidgetCommand(DashboardGrid* grid, const QString& itemId, const QString& fromName,
                                          const QString& toName)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Rename Widget")), m_grid(grid),
      m_itemId(itemId), m_fromName(fromName), m_toName(toName) {}

void RenameWidgetCommand::redo() {
    m_grid->applyRename(m_itemId, m_toName);
}

void RenameWidgetCommand::undo() {
    m_grid->applyRename(m_itemId, m_fromName);
}

SetItemKeyCommand::SetItemKeyCommand(DashboardGrid* grid, const QString& itemId, const QString& fromKey,
                                      const QString& toKey)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Set Widget Key")), m_grid(grid),
      m_itemId(itemId), m_fromKey(fromKey), m_toKey(toKey) {}

void SetItemKeyCommand::redo() {
    m_grid->applySetKey(m_itemId, m_toKey);
}

void SetItemKeyCommand::undo() {
    m_grid->applySetKey(m_itemId, m_fromKey);
}

SetItemConfigCommand::SetItemConfigCommand(DashboardGrid* grid, const QString& itemId, const QJsonObject& fromConfig,
                                            const QJsonObject& toConfig)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Set Widget Config")), m_grid(grid),
      m_itemId(itemId), m_fromConfig(fromConfig), m_toConfig(toConfig) {}

void SetItemConfigCommand::redo() {
    m_grid->applySetConfig(m_itemId, m_toConfig);
}

void SetItemConfigCommand::undo() {
    m_grid->applySetConfig(m_itemId, m_fromConfig);
}

ChangeWidgetTypeCommand::ChangeWidgetTypeCommand(DashboardGrid* grid, const QString& itemId,
                                                  const QString& fromTypeId, const QString& toTypeId)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Change Widget Type")), m_grid(grid),
      m_itemId(itemId), m_fromTypeId(fromTypeId), m_toTypeId(toTypeId) {}

void ChangeWidgetTypeCommand::redo() {
    m_grid->applyTypeChange(m_itemId, m_toTypeId);
}

void ChangeWidgetTypeCommand::undo() {
    m_grid->applyTypeChange(m_itemId, m_fromTypeId);
}

ChangeZOrderCommand::ChangeZOrderCommand(DashboardGrid* grid, const QString& itemId, int fromIndex, int toIndex,
                                          const QString& label)
    : QUndoCommand(label), m_grid(grid), m_itemId(itemId), m_fromIndex(fromIndex), m_toIndex(toIndex) {}

void ChangeZOrderCommand::redo() {
    m_grid->applyZOrder(m_itemId, m_toIndex);
}

void ChangeZOrderCommand::undo() {
    m_grid->applyZOrder(m_itemId, m_fromIndex);
}

GroupItemsCommand::GroupItemsCommand(DashboardGrid* grid, const QMap<QString, QString>& previousGroupIds,
                                      const QString& newGroupId)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Group Widgets")), m_grid(grid),
      m_previousGroupIds(previousGroupIds), m_newGroupId(newGroupId) {}

void GroupItemsCommand::redo() {
    for (auto it = m_previousGroupIds.constBegin(); it != m_previousGroupIds.constEnd(); ++it) {
        m_grid->applySetGroup(it.key(), m_newGroupId);
    }
}

void GroupItemsCommand::undo() {
    for (auto it = m_previousGroupIds.constBegin(); it != m_previousGroupIds.constEnd(); ++it) {
        m_grid->applySetGroup(it.key(), it.value());
    }
}

UngroupItemsCommand::UngroupItemsCommand(DashboardGrid* grid, const QMap<QString, QString>& previousGroupIds)
    : QUndoCommand(QCoreApplication::translate("DashboardCommands", "Ungroup Widgets")), m_grid(grid),
      m_previousGroupIds(previousGroupIds) {}

void UngroupItemsCommand::redo() {
    for (auto it = m_previousGroupIds.constBegin(); it != m_previousGroupIds.constEnd(); ++it) {
        m_grid->applySetGroup(it.key(), QString());
    }
}

void UngroupItemsCommand::undo() {
    for (auto it = m_previousGroupIds.constBegin(); it != m_previousGroupIds.constEnd(); ++it) {
        m_grid->applySetGroup(it.key(), it.value());
    }
}

} // namespace traceview
