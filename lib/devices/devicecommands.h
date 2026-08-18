#pragma once

#include <QUndoCommand>

#include "devices/device.h"

namespace traceview {

class DevicesGrid;

// One QUndoCommand per DevicesGrid mutation, mirroring dashboard/
// dashboardcommands.h's convention: each command stores just the bit of
// state its operation touches and forwards to a DevicesGrid::apply*()
// helper -- DevicesGrid remains the only place that actually touches
// m_devices/m_cards, these classes just decide which direction (redo/undo)
// to call those with.

class AddDeviceCommand : public QUndoCommand {
public:
    // index is where the device lands in display order (see
    // DevicesGrid::indexOfDevice()) -- always the end of the list for a
    // fresh user add, but also reused by RemoveDeviceCommand::undo() to
    // restore a removed device to its original position.
    AddDeviceCommand(DevicesGrid* grid, const Device& device, int index);

    void undo() override;
    void redo() override;

private:
    DevicesGrid* m_grid;
    Device m_device;
    int m_index;
};

class RemoveDeviceCommand : public QUndoCommand {
public:
    RemoveDeviceCommand(DevicesGrid* grid, const Device& device, int index);

    void undo() override;
    void redo() override;

private:
    DevicesGrid* m_grid;
    Device m_device;
    int m_index;
};

class UpdateDeviceCommand : public QUndoCommand {
public:
    UpdateDeviceCommand(DevicesGrid* grid, const Device& fromDevice, const Device& toDevice);

    void undo() override;
    void redo() override;

private:
    DevicesGrid* m_grid;
    Device m_fromDevice;
    Device m_toDevice;
};

} // namespace traceview
