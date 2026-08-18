#include "devicecommands.h"

#include <QCoreApplication>

#include "devicesgrid.h"

namespace traceview {

// These command classes derive from QUndoCommand, not QObject, so tr() isn't
// available here; use QCoreApplication::translate() with the owning class as
// context instead (same idiom as dashboardcommands.cpp).
AddDeviceCommand::AddDeviceCommand(DevicesGrid* grid, const Device& device, int index)
    : QUndoCommand(QCoreApplication::translate("DeviceCommands", "Add Device")), m_grid(grid), m_device(device),
      m_index(index) {}

void AddDeviceCommand::redo() {
    m_grid->applyInsertDevice(m_device, m_index);
}

void AddDeviceCommand::undo() {
    m_grid->applyRemoveDeviceById(m_device.id);
}

RemoveDeviceCommand::RemoveDeviceCommand(DevicesGrid* grid, const Device& device, int index)
    : QUndoCommand(QCoreApplication::translate("DeviceCommands", "Remove Device")), m_grid(grid), m_device(device),
      m_index(index) {}

void RemoveDeviceCommand::redo() {
    m_grid->applyRemoveDeviceById(m_device.id);
}

void RemoveDeviceCommand::undo() {
    m_grid->applyInsertDevice(m_device, m_index);
}

UpdateDeviceCommand::UpdateDeviceCommand(DevicesGrid* grid, const Device& fromDevice, const Device& toDevice)
    : QUndoCommand(QCoreApplication::translate("DeviceCommands", "Edit Device")), m_grid(grid),
      m_fromDevice(fromDevice), m_toDevice(toDevice) {}

void UpdateDeviceCommand::redo() {
    m_grid->applyUpdateDevice(m_toDevice);
}

void UpdateDeviceCommand::undo() {
    m_grid->applyUpdateDevice(m_fromDevice);
}

} // namespace traceview
