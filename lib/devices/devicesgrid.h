#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUndoStack>
#include <QVector>
#include <QWidget>
#include <functional>

#include "devices/device.h"
#include "telemetry/catalogtopicinfo.h"

namespace traceview {

class DeviceCard;
class AddDeviceCommand;
class RemoveDeviceCommand;
class UpdateDeviceCommand;

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

    friend class AddDeviceCommand;
    friend class RemoveDeviceCommand;
    friend class UpdateDeviceCommand;

public:
    explicit DevicesGrid(QWidget* parent = nullptr);

    // If device.id is empty, generates one (QUuid). Appends to the end of
    // the display order and creates its DeviceCard. Returns the id used.
    // Undoable -- pushed onto undoStack() as an AddDeviceCommand (see
    // applyInsertDevice() below for the actual mutation both directions
    // replay).
    QString addDevice(const Device& device);
    // No-op if id is unknown. Removes the entry and its card, then re-flows
    // every following card into the gap (see class comment above). Clears
    // the selection first if the removed device was the selected one.
    // Undoable -- pushed onto undoStack() as a RemoveDeviceCommand.
    void removeDevice(const QString& id);
    // Ids of the hub-channel devices that ride `id` as their parent. Empty
    // for a device nothing rides, which is every device until someone
    // configures a hub channel.
    QStringList childDeviceIds(const QString& id) const;
    // Matched by id; no-op if unknown. Refreshes the corresponding card's
    // displayed data in place -- does not change display order. Undoable --
    // pushed onto undoStack() as an UpdateDeviceCommand. Only meant for a
    // real edit (see DeviceConfigDialog via handleConfigRequested()); ambient
    // connection-state mirroring uses setDeviceConnected() instead so a
    // device's dot flipping doesn't itself become an undo step.
    void updateDevice(const Device& device);
    // Mirrors a live DeviceConnection state into device.connected without
    // going through the undo stack -- called by MainWindow whenever a
    // connection actually opens/closes (including its own ambient retry
    // loop), which is not a user edit. No-op if id is unknown or the flag is
    // already what's stored.
    void setDeviceConnected(const QString& id, bool connected);
    // Mirrors a live DeviceConnection's Backend::deviceIdentified into
    // device.btpVersion/btpId, same non-undoable treatment as
    // setDeviceConnected() above and for the same reason: this is the
    // handshake reporting what the device actually is, not a user edit.
    // Called with empty strings when the connection drops, so a stale
    // identity from a previous session never lingers in the UI.
    void setDeviceIdentity(const QString& id, const QString& btpVersion, const QString& btpId);
    // Mirrors the dongle's hub.peers view of a hub child's robot into
    // device.peerOnline/peerPresenceKnown/peerLongOffline/peerBootId -- same
    // non-undoable, not-persisted treatment as setDeviceConnected()/
    // setDeviceIdentity(). Unlike those it does NOT emit deviceUpdated(): it
    // must not trigger MainWindow's onDeviceUpdated() (which re-applies the
    // transport target and reattaches hub children) once a second. Refreshes
    // the card in place. See Device::peerOnline for what the flags mean.
    void setDevicePeerState(const QString& id, bool peerOnline, bool peerPresenceKnown,
                            bool peerLongOffline, quint32 peerBootId);
    // Mirrors a live Backend::deviceInfoReported into device.reportedInfo (the
    // device's MANIFEST_DATA source_info block, BTP's docs/commands.md section
    // 3.12). Same not-undoable, not-persisted, no-deviceUpdated() treatment as
    // setDevicePeerState() above -- a manifest arriving must not re-apply the
    // connection target. Refreshes the card in place and, via
    // deviceReportedInfoChanged() below, any open config dialog. Called with an
    // empty vector when the connection drops.
    void setDeviceReportedInfo(const QString& id, const QVector<DeviceInfoRecord>& info);
    // Tells this device's config dialog, if it's currently open, to re-pull
    // its catalog list from m_topicCatalogProvider. MANIFEST_DATA lands
    // asynchronously after the handshake that fires deviceIdentified() (see
    // BtpBackend::sessionEstablished's connect() -- deviceIdentified is
    // emitted immediately, the manifest reply comes later over the wire), so
    // catalogTopics() is typically still empty at the moment the dialog's
    // deviceUpdated-triggered refresh runs. MainWindow calls this once
    // Backend::catalogChanged actually fires so the still-open dialog picks
    // up the real list instead of only showing it on next open. Deliberately
    // its own signal rather than routed through deviceUpdated: that signal
    // reaches MainWindow::onDeviceUpdated too, which re-applies the device's
    // connection target -- something a catalog arriving has no business
    // triggering. No-op if no dialog is open for this device.
    void notifyCatalogChanged(const QString& id);

    QVector<Device> devices() const {
        return m_devices;
    }

    // 0 or 1 -- single-selection only (a click on a card replaces whatever
    // was selected before, same "Replace selection" default as
    // DashboardGrid's plain click, minus the Ctrl-click multi-select this
    // grid has no use for).
    int selectedCount() const {
        return m_selectedId.isEmpty() ? 0 : 1;
    }
    // No-op if nothing is selected. Undoable (see removeDevice() above).
    void removeSelected();

    QUndoStack* undoStack() const {
        return m_undoStack;
    }

    // Mirrors DashboardGrid::toJson()/fromJson()'s shape/convention: a
    // single "devices" array of deviceToJson() objects (see device.h). Used
    // by MainWindow to persist the device list into ProjectStore's own
    // "devices" section. fromJson() clears the current list first (same
    // "replace, don't merge" contract as DashboardGrid::fromJson()) and
    // re-adds each entry, so deviceAdded() still fires for every loaded
    // device -- MainWindow's DeviceConnection bookkeeping needs no separate
    // load path. Bypasses undoStack() entirely (see applyInsertDevice()/
    // applyRemoveDeviceById() below), same as DashboardGrid::fromJson().
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& object);

    // Supplies the live OS port list DeviceConfigDialog offers (initially,
    // and again each time its refresh button is clicked). DevicesGrid can't
    // query that itself -- traceview_devices doesn't depend on QSerialPort
    // (see lib/CMakeLists.txt) -- so MainWindow injects it here once. Safe
    // to leave unset: the dialog's port combo then just starts empty and
    // Refresh is a no-op, same as before this existed.
    void setPortListProvider(std::function<QStringList()> provider) {
        m_portListProvider = std::move(provider);
    }

    // Same reasoning as setPortListProvider() above, for the USB device
    // picker -- traceview_devices doesn't depend on hidapi either (see
    // lib/CMakeLists.txt). Safe to leave unset: the dialog's USB combo then
    // just starts empty and its refresh is a no-op.
    void setUsbDeviceListProvider(std::function<QVector<UsbDeviceOption>()> provider) {
        m_usbDeviceListProvider = std::move(provider);
    }

    // Supplies the topic catalog (TelemetryCatalog, via MANIFEST_DATA) the
    // gear icon's "Reported by device" section lists for a given device id.
    // DevicesGrid can't reach a Backend itself (traceview_devices doesn't
    // depend on traceview_protocol, same reasoning as setPortListProvider()
    // above) -- MainWindow injects this once. Safe to leave unset: the
    // dialog's catalog list then just starts empty.
    void setTopicCatalogProvider(
        std::function<QVector<CatalogTopicInfo>(const QString&)> provider) {
        m_topicCatalogProvider = std::move(provider);
    }

    // Supplies the live hub.peers snapshot the gear icon's "Robot source_id"
    // combo offers for a given hub Device::id -- same reasoning as
    // setTopicCatalogProvider() above (traceview_devices doesn't depend on
    // traceview_protocol), except polled on a timer rather than fetched once
    // up front: unlike a manifest exchange, new peers/status arrive
    // continuously over telemetry for as long as the dialog stays open (see
    // handleConfigRequested()). Safe to leave unset: the combo then just
    // starts (and stays) empty, same manual-entry-only fallback the old
    // plain text field offered.
    void setHubPeerListProvider(std::function<QVector<HubPeer>(const QString&)> provider) {
        m_hubPeerListProvider = std::move(provider);
    }

signals:
    void selectionChanged();
    // Fired from applyInsertDevice()/applyRemoveDeviceById()/
    // applyUpdateDevice()/setDeviceConnected() -- i.e. on every actual
    // mutation, regardless of whether it came from a fresh call, an
    // undo, or a redo -- so a connection owner (MainWindow) can create/
    // destroy/re-point the matching DeviceConnection without polling
    // devices() itself.
    void deviceAdded(const Device& device);
    void deviceRemoved(const QString& id);
    void deviceUpdated(const Device& device);
    // Bridges notifyCatalogChanged() (above) to whichever config dialog is
    // currently open for `id` -- see handleConfigRequested(). Internal to
    // this class's own dialog-refresh wiring; nothing outside DevicesGrid
    // needs to listen to it.
    void deviceCatalogChanged(const QString& id);
    // Same bridge as deviceCatalogChanged() but for setDeviceReportedInfo() --
    // pushes the source_info block into the open config dialog for `id`.
    void deviceReportedInfoChanged(const QString& id);
    // Bubbled straight from the selected card's DeviceCard::connectToggleRequested
    // -- DevicesGrid has no DeviceConnection of its own to flip, MainWindow does.
    void connectToggleRequested(const QString& deviceId);
    // A removeDevice() that was refused because other devices ride this one
    // (see removeDevice()). Carries the blocked device and the names of what
    // depends on it, so the explanation can say which ones rather than just
    // that there are some. DevicesGrid raises no dialogs of its own; the
    // window that owns it decides how to say this.
    void removeBlockedByChildren(const QString& deviceId, const QStringList& childNames);

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

    // Mutators used both by fromJson() (bulk load -- deliberately bypasses
    // the undo stack, same reasoning as DashboardGrid::fromJson()) and by the
    // AddDeviceCommand/RemoveDeviceCommand/UpdateDeviceCommand friends above
    // to apply/unapply one committed change. DevicesGrid stays the only place
    // that actually touches m_devices/m_cards; everything else just decides
    // which direction to call these with.
    void applyInsertDevice(const Device& device, int index);
    void applyRemoveDeviceById(const QString& id);
    void applyUpdateDevice(const Device& device);

    QVector<Device> m_devices;     // insertion order = display order
    QVector<DeviceCard*> m_cards;  // parallel to m_devices, same order/index
    QString m_selectedId;          // empty when nothing is selected
    std::function<QStringList()> m_portListProvider;
    std::function<QVector<UsbDeviceOption>()> m_usbDeviceListProvider;
    std::function<QVector<CatalogTopicInfo>(const QString&)> m_topicCatalogProvider;
    std::function<QVector<HubPeer>(const QString&)> m_hubPeerListProvider;
    QUndoStack* m_undoStack;
};

}  // namespace traceview
