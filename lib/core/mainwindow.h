#pragma once

#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QString>

class QAction;
class QComboBox;
class QEvent;
class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace traceview {

class Backend;
class DashboardGrid;
class DashboardWidget;
class DebugChartsWindow;
class DevicesGrid;
class LayersPanel;
class PanelDockController;
class PropertiesPanel;
class Ribbon;
class SerialManager;
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
    void onWorkspaceSelected(const QString& id);
    void onWorkspaceDeleteRequested(const QString& id);
    void onNewWorkspaceRequested();
    // Re-applies m_dockController's geometry to every docked panel. Called
    // whenever m_contentRow resizes (see eventFilter) since the panels are
    // positioned directly rather than managed by a layout.
    void positionOverlayPanels();
    void updateRibbonIcons();

    void onRibbonTabChanged(int index);
    void refreshSerialPorts();
    void onSerialConnectToggled(bool checked);
    void onSerialConnectionStateChanged(bool connected);
    void onSerialErrorOccurred(const QString& message);
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
    void onPanelTypeChangeRequested(const QString& typeId);
    void onPanelNameChangeRequested(const QString& name);
    void onPanelKeyChangeRequested(const QString& key);
    void onPanelConfigChangeRequested(const QJsonObject& config);
    void onNewProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProject();
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
    QMenu* m_recentFilesMenu = nullptr;
    // WA_DeleteOnClose'd (see debugchartswindow.cpp) -- QPointer so this
    // resets to null on its own once the user closes it, instead of leaving
    // a dangling raw pointer behind for the next "Debug" click to dereference.
    QPointer<DebugChartsWindow> m_debugChartsWindow;
    int m_configureTabIndex = -1;
    int m_devicesTabIndex = -1;
    bool m_configureTabActive = false;
    // Gates m_removeDeviceAction the same way m_configureTabActive gates
    // m_removeAction -- see updateDeviceSelectionActions().
    bool m_devicesTabActive = false;

    void onLineTerminatorChanged(int index);

    SerialManager* m_serialManager = nullptr;
    // Everything on the other end of the wire, behind the Backend interface
    // (backend/backend.h) -- decoded telemetry, topic subscriptions, and
    // terminal framing. Concretely a BtpBackend today; MainWindow only ever
    // talks to it through the abstract interface. SerialManager above stays
    // concrete: it just moves raw bytes in and out of the open port.
    Backend* m_backend = nullptr;
    void wireChartWidgetToTelemetry(DashboardWidget* widget);
    // Re-derives every open widget's subscription from its current config --
    // called whenever a config edit (or its undo/redo) could have changed a
    // widget's source/topic/sample time.
    void refreshWidgetSubscriptions();
    // Recomputes the status-bar telemetry summary (requested vs. effective
    // rate, plus bytes/drops from a status_version=2 STATUS).
    void updateTelemetryStatusLabel();
    // One entry per live chart/gauge widget: its SubscriptionManager handle
    // (0 when the widget has no source/topic configured yet). Cleaned up from
    // each widget's own destroyed() signal.
    QHash<DashboardWidget*, quint64> m_widgetSubscriptions;
    QLabel* m_telemetryStatusLabel = nullptr;
    int m_runTabIndex = -1;
    QComboBox* m_portCombo = nullptr;
    QToolButton* m_refreshPortsButton = nullptr;
    QComboBox* m_baudCombo = nullptr;
    QComboBox* m_lineTerminatorCombo = nullptr;
    QPushButton* m_connectButton = nullptr;
    QToolButton* m_fullscreenButton = nullptr;
    bool m_wasMaximized = false;
    QRect m_preFullscreenGeometry;
};

} // namespace traceview
