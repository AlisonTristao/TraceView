#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QString>

class QAction;
class QEvent;
class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;
class QUndoGroup;

namespace traceview {

class Backend;
class DashboardGrid;
class DashboardWidget;
class DebugChartsWindow;
class DeviceConnection;
class DevicesGrid;
struct Device;
class LayersPanel;
class LogViewer;
class PanelDockController;
class PropertiesPanel;
class Ribbon;
class SerialWidgetBridge;
class WorkspaceSwitcher;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    // Declared (rather than left implicit): the destructor needs to
    // disconnect each dashboard widget's destroyed() handler (see
    // wireChartWidgetToTelemetry) while m_widgetSubscriptions still exists,
    // before Qt's own child-QObject teardown runs.
    ~MainWindow() override;

protected:
    // Watches m_contentRow for QEvent::Resize so m_layersPanel/
    // m_propertiesPanel (floated over the canvas, not laid out beside it --
    // see positionOverlayPanels()) get repositioned whenever it changes size.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void buildMenus();
    Ribbon* buildRibbon();
    void buildPropertiesPanel();
    void buildLayersPanel();
    void buildWorkspaceSwitcher();
    // Pushes WorkspaceManager's current workspace list/active id into
    // m_workspaceSwitcher. Called after any mutation (switch/create/delete)
    // and on project load/new/save.
    void refreshWorkspaceSwitcher();
    // Snapshots the outgoing workspace's dashboard, makes `id` active, and
    // reloads DashboardGrid from it -- the same fromJson()/undo-clear/
    // refresh sequence onNewProject()/openRecentFile() use. No-op if `id`
    // is already active.
    void switchToWorkspace(const QString& id);
    // Ctrl+Tab/Ctrl+Shift+Tab (direction +1/-1): moves to the next/previous
    // workspace in WorkspaceManager's own list order, wrapping around at
    // either end. No-op with fewer than 2 workspaces.
    void cycleWorkspace(int direction);
    void onWorkspaceSelected(const QString& id);
    void onWorkspaceDeleteRequested(const QString& id);
    void onNewWorkspaceRequested();
    // Re-applies m_dockController's geometry to every docked panel. Called
    // whenever m_contentRow resizes (see eventFilter) since the panels are
    // positioned directly rather than managed by a layout.
    void positionOverlayPanels();
    void updateRibbonIcons();

    void onRibbonTabChanged(int index);
    void onSelectionChanged(const QString& itemId);
    void updateSelectionActions();
    // Shows m_propertiesPanel/m_layersPanel only on the Layout tab, and then
    // only while something is selected or the panel's own pin toggle is
    // engaged (see PropertiesPanel/LayersPanel::isPinned()). Called on tab
    // change, selection change, and either panel's pinnedChanged().
    void updatePanelVisibility();
    // Pushes the current selection's type/name/key into m_propertiesPanel.
    // Called on selectionChanged and whenever the undo stack moves, since a
    // property edit (or its undo/redo) doesn't otherwise touch selection.
    void refreshPropertiesPanel();
    // Rebuilds m_layersPanel's rows from DashboardGrid::layerEntries() and
    // re-highlights the current selection. Called whenever the item list or
    // its order could have changed (itemsChanged(), undo/redo) or the
    // selection did (selectionChanged()).
    void refreshLayersPanel();
    void onAddWidget();
    // Appends a placeholder mock Device (Devices tab's ribbon button) --
    // mirrors onAddWidget()'s "drop in a default, let the user edit it
    // afterward" shape, but there's no upfront picker here either way
    // since Device has only one CommType today. The user renames it via
    // the card's own gear -> DeviceConfigDialog (DevicesGrid owns that
    // flow internally); this slot doesn't open it automatically.
    void onAddDevice();
    // Mirrors updateSelectionActions() for m_devicesGrid's own (single-item)
    // selection -- called on DevicesGrid::selectionChanged and on every tab
    // switch, so m_removeDeviceAction stays gated to "Devices tab active AND
    // a device is selected" (same shape as m_removeAction's own
    // m_configureTabActive-gated condition, kept mutually exclusive so both
    // never share an enabled Delete shortcut at once).
    void updateDeviceSelectionActions();
    // Builds and wires one DeviceConnection the way onDeviceAdded() always
    // has (connectionStateChanged/backend signals/deviceIdentified) --
    // factored out so onDeviceUpdated() can rebuild one in place too, when a
    // device's transportType itself changes (see its own comment).
    DeviceConnection* createDeviceConnection(const Device& device);
    // Keep m_deviceConnections (one DeviceConnection per Device::id) in sync
    // with m_devicesGrid's own list -- wired to DevicesGrid::deviceAdded/
    // deviceRemoved/deviceUpdated. onDeviceUpdated also re-points the
    // connection at a possibly-changed port/baud/line-terminator, and
    // rebuilds the connection entirely if transportType itself changed
    // (DeviceConnection's Transport/Backend pair is fixed at construction,
    // see deviceconnection.h -- it can't be swapped on a live instance).
    void onDeviceAdded(const Device& device);
    void onDeviceRemoved(const QString& id);
    void onDeviceUpdated(const Device& device);
    // Mirrors a DeviceConnection's real state back into DevicesGrid's own
    // Device::connected (DeviceCard's status dot reads that field).
    void onDeviceConnectionStateChanged(const QString& deviceId, bool connected);
    // DeviceCard's status-dot click (DevicesGrid::connectToggleRequested) --
    // flips the matching DeviceConnection's intent (DeviceConnection::
    // wantsConnection()), not just its current connectedness, so this also
    // works to silence a device that's stuck retrying.
    void onDeviceConnectToggleRequested(const QString& deviceId);
    // Converts m_devicesGrid->devices() into DeviceOptions and pushes them
    // into m_propertiesPanel -- called whenever the device list changes, so
    // every widget config editor's Device combo stays current.
    void refreshPropertiesPanelDevices();
    void onPanelTypeChangeRequested(const QString& typeId);
    void onPanelNameChangeRequested(const QString& name);
    void onPanelKeyChangeRequested(const QString& key);
    void onPanelConfigChangeRequested(const QJsonObject& config);
    void onNewProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProject();
    void onOpenLogFile();
    void openRecentFile(const QString& path);
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void onClearRecentFiles();
    void onAbout();
    void onDonate();
    void onDebug();
    void onFullscreenToggled(bool checked);

    DashboardGrid* m_dashboardGrid = nullptr;
    // Devices tab's content -- swapped in for m_dashboardGrid via
    // m_contentStack, never shown at the same time (see onRibbonTabChanged).
    DevicesGrid* m_devicesGrid = nullptr;
    // Logs tab's content -- same swap-via-m_contentStack treatment as
    // m_devicesGrid, opened on demand via m_openLogFileAction rather than
    // holding any state of its own.
    LogViewer* m_logViewer = nullptr;
    // One real, independent serial connection per Device::id -- see
    // core/deviceconnection.h. Created/destroyed/updated in lockstep with
    // m_devicesGrid's own list (onDeviceAdded/onDeviceRemoved/onDeviceUpdated).
    QHash<QString, DeviceConnection*> m_deviceConnections;
    // Holds m_dashboardGrid and m_devicesGrid; whichever one is current is
    // what fills m_contentRow. Unlike the layers/properties panels below,
    // this genuinely shares layout space (contentLayout->addWidget()) rather
    // than floating -- DevicesGrid isn't subject to DashboardGrid's
    // fraction-of-canvas geometry constraint, so swapping/resizing it here
    // doesn't reflow anything the way shrinking the canvas would.
    QStackedWidget* m_contentStack = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    LayersPanel* m_layersPanel = nullptr;
    // Row below the ribbon that hosts the canvas; m_layersPanel/
    // m_propertiesPanel are children of this (not of a layout) so they can
    // float above m_dashboardGrid instead of sharing its row -- see
    // positionOverlayPanels().
    QWidget* m_contentRow = nullptr;
    // Owns the panels' drag-to-dock/float behavior -- see paneldockcontroller.h.
    PanelDockController* m_dockController = nullptr;
    Ribbon* m_ribbon = nullptr;
    WorkspaceSwitcher* m_workspaceSwitcher = nullptr;
    QAction* m_addWidgetAction = nullptr;
    QAction* m_addDeviceAction = nullptr;
    QAction* m_removeDeviceAction = nullptr;
    QAction* m_openLogFileAction = nullptr;
    QAction* m_removeAction = nullptr;
    QAction* m_copyAction = nullptr;
    QAction* m_pasteAction = nullptr;
    QAction* m_bringToFrontAction = nullptr;
    QAction* m_bringForwardAction = nullptr;
    QAction* m_sendBackwardAction = nullptr;
    QAction* m_sendToBackAction = nullptr;
    QAction* m_groupAction = nullptr;
    QAction* m_ungroupAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    // Tracks which of m_dashboardGrid's/m_devicesGrid's own QUndoStack is
    // "active" -- m_undoAction/m_redoAction are created from this group
    // (not from either stack directly) so Ctrl+Z/Ctrl+Y always act on
    // whichever tab is actually showing, switched in onRibbonTabChanged().
    QUndoGroup* m_undoGroup = nullptr;
    QMenu* m_recentFilesMenu = nullptr;
    // WA_DeleteOnClose'd (see debugchartswindow.cpp) -- QPointer so this
    // resets to null on its own once the user closes it, instead of leaving
    // a dangling raw pointer behind for the next "Debug" click to dereference.
    QPointer<DebugChartsWindow> m_debugChartsWindow;
    int m_configureTabIndex = -1;
    int m_devicesTabIndex = -1;
    int m_logsTabIndex = -1;
    bool m_configureTabActive = false;
    // Gates m_removeDeviceAction the same way m_configureTabActive gates
    // m_removeAction -- see updateDeviceSelectionActions().
    bool m_devicesTabActive = false;

    // Wires every control/serial-monitor widget's send/receive to whichever
    // device its own config currently targets -- see core/serialwidgetbridge.h.
    // Kept as a member (rather than fire-and-forget like before the
    // multi-device refactor) so refreshTerminalWiring() can be called
    // whenever a config edit could have re-pointed a terminal widget.
    SerialWidgetBridge* m_serialWidgetBridge = nullptr;
    void wireChartWidgetToTelemetry(DashboardWidget* widget);
    // Re-derives one widget's subscription from its current config: moves it
    // to a new device's Backend (dropping the old subscription/fieldSample
    // connection) if its deviceId changed, otherwise just updates the
    // source/topic/rate on whichever Backend it's already registered with.
    void refreshWidgetSubscription(DashboardWidget* widget);
    // Calls refreshWidgetSubscription() for every open chart/gauge widget,
    // plus SerialWidgetBridge::refreshTerminalWiring() -- called whenever a
    // config edit (or its undo/redo) could have changed a widget's device/
    // source/topic/sample time.
    void refreshWidgetSubscriptions();
    // Recomputes the status-bar telemetry summary (requested vs. effective
    // rate, plus bytes/drops from a status_version=2 STATUS), aggregated
    // across every connected device's Backend.
    void updateTelemetryStatusLabel();
    // Rebuilds the Run tab's device status strip from m_devicesGrid's
    // current list/connection state. Called on every device add/remove/
    // update/connection-state change, and on theme change (dot colors).
    void refreshDeviceStatusLabel();

    // One subscription reference for a live chart/gauge widget: which
    // device's Backend it's registered with (empty if none/unconfigured) and
    // the SubscriptionManager handle within that Backend (0 if none). Needed
    // as a pair now -- unlike the single-Backend era, the handle alone isn't
    // enough to know which Backend::removeSubscriber() to call it against.
    struct WidgetSubscription {
        QString deviceId;
        quint64 handle = 0;
    };
    QHash<DashboardWidget*, WidgetSubscription> m_widgetSubscriptions;
    QLabel* m_telemetryStatusLabel = nullptr;
    int m_runTabIndex = -1;
    // Read-only "device: dot" strip replacing the old single-connection port/
    // baud/connect bar -- per-device connection config now lives in the
    // Devices tab (DeviceConfigDialog) instead.
    QLabel* m_deviceStatusLabel = nullptr;
    QToolButton* m_fullscreenButton = nullptr;
    bool m_wasMaximized = false;
    QRect m_preFullscreenGeometry;
};

} // namespace traceview
