#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

#include "devices/device.h"

namespace traceview {

class DeviceCard;

// Auto-flow grid of DeviceCard widgets: fixed card size (see kDeviceCardSize,
// devicecard.h), left-to-right, wrapping to the next row based on available
// width. Unlike DashboardGrid, a card is never dragged/resized/repositioned
// by the user, so there's no occupancy-scan/free-slot search here (contrast
// DashboardGrid::findFreeSlot()) -- display order is exactly m_devices'
// insertion order, and every card's position is just
// row = index / itemsPerRow, col = index % itemsPerRow, recomputed on
// resizeEvent() and on every add/remove. removeDevice() therefore always
// compacts: there's no notion of a "hole" left behind.
//
// Owns opening DeviceConfigDialog itself (see handleConfigRequested()) -- a
// caller only ever needs to know about Device/DeviceCard/DevicesGrid, never
// DeviceConfigDialog.
class DevicesGrid : public QWidget {
    Q_OBJECT

public:
    explicit DevicesGrid(QWidget* parent = nullptr);

    // If device.id is empty, generates one (QUuid). Appends to the end of
    // the display order and creates its DeviceCard. Returns the id used.
    QString addDevice(const Device& device);
    // No-op if id is unknown. Removes the entry and its card, then re-flows
    // every following card into the gap (see class comment above). Clears
    // the selection first if the removed device was the selected one.
    void removeDevice(const QString& id);
    // Matched by id; no-op if unknown. Refreshes the corresponding card's
    // displayed data in place -- does not change display order.
    void updateDevice(const Device& device);

    QVector<Device> devices() const { return m_devices; }

    // 0 or 1 -- single-selection only (a click on a card replaces whatever
    // was selected before, same "Replace selection" default as
    // DashboardGrid's plain click, minus the Ctrl-click multi-select this
    // grid has no use for).
    int selectedCount() const { return m_selectedId.isEmpty() ? 0 : 1; }
    // No-op if nothing is selected.
    void removeSelected();

    // Mirrors DashboardGrid::toJson()/fromJson()'s shape/convention: a
    // single "devices" array of deviceToJson() objects (see device.h). Used
    // by MainWindow to persist the device list into ProjectStore's own
    // "devices" section. fromJson() clears the current list first (same
    // "replace, don't merge" contract as DashboardGrid::fromJson()) and
    // re-adds each entry via addDevice(), so deviceAdded() still fires for
    // every loaded device -- MainWindow's DeviceConnection bookkeeping needs
    // no separate load path.
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    // Supplies the live OS port list DeviceConfigDialog offers (initially,
    // and again each time its refresh button is clicked). DevicesGrid can't
    // query that itself -- traceview_devices doesn't depend on QSerialPort
    // (see lib/CMakeLists.txt) -- so MainWindow injects it here once. Safe
    // to leave unset: the dialog's port combo then just starts empty and
    // Refresh is a no-op, same as before this existed.
    void setPortListProvider(std::function<QStringList()> provider) { m_portListProvider = std::move(provider); }

signals:
    void selectionChanged();
    // Fired from addDevice()/removeDevice()/updateDevice() so a connection
    // owner (MainWindow) can create/destroy/re-point the matching
    // DeviceConnection without polling devices() itself.
    void deviceAdded(const Device& device);
    void deviceRemoved(const QString& id);
    void deviceUpdated(const Device& device);
    // Bubbled straight from the selected card's DeviceCard::connectToggleRequested
    // -- DevicesGrid has no DeviceConnection of its own to flip, MainWindow does.
    void connectToggleRequested(const QString& deviceId);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // Mirrors DashboardGrid::gutter()'s spacing scale (see dashboardgrid.cpp)
    // for visual consistency between the two grids: a fraction of the
    // panel's shorter side, clamped to the same [kMinGutter, kMaxGutter]
    // range.
    int gutter() const;
    void relayout();
    int indexOfDevice(const QString& id) const;
    void handleConfigRequested(const QString& deviceId);
    void handleCardSelectRequested(const QString& deviceId);

    QVector<Device> m_devices;    // insertion order = display order
    QVector<DeviceCard*> m_cards; // parallel to m_devices, same order/index
    QString m_selectedId;         // empty when nothing is selected
    std::function<QStringList()> m_portListProvider;
};

} // namespace traceview
