#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QMainWindow>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVector>

#include "backend/statusseverity.h"
#include "core/usermodemanager.h"
#include "dashboard/dashboarditem.h"
#include "devices/device.h"
#include "devices/hubpeeraccumulator.h"
#include "telemetry/telemetrybinding.h"
#include "updater/updateinfo.h"

class QAction;
class QDialog;
class QEvent;
class QLabel;
class QMenu;
class QMoveEvent;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QStackedWidget;
class QTimer;
class QToolButton;
class QTreeWidget;
class QUndoGroup;

namespace traceview {

class Backend;
#ifdef TRACEVIEW_ENABLE_BLE
class BleDiscoveryService;
#endif
class BtpMonitorTab;
class DashboardGrid;
class DashboardWidget;
class DebugChartsWindow;
class DeviceConnection;
class DevicePreviewFrame;
class DevicesGrid;
class DiagramScriptRuntime;
class FrameLog;
class LayersPanel;
class LogViewer;
class NotificationHistoryWindow;
class NotificationLog;
class OtaTab;
class PanelDockController;
class PropertiesPanel;
class Ribbon;
class SerialWidgetBridge;
class SettingsPage;
class UpdateChecker;
class UpdateDownloader;
class WorkspaceDock;
class BrandCornerMark;
class StartupLoadingOverlay;
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
    // see positionOverlayPanels()) get repositioned whenever it changes size;
    // also watches m_dashboardScrollArea->viewport() for QEvent::Resize so
    // applyBreakpointViewport() can re-derive the canvas-height-multiplier
    // pixel math (which reads that viewport's current height) whenever it
    // changes, not just when the breakpoint itself does.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Applies any floating panel's saved position on the very first show --
    // m_dockController defers that (see PanelDockController::
    // applyFloatingPositions()) because it needs this window's real
    // on-screen position, which doesn't exist yet in the constructor.
    void showEvent(QShowEvent* event) override;
    // Keeps any floating panel visually anchored to the window while it's
    // being dragged live (including onto a different monitor): shifts every
    // floating panel by the same delta the window itself just moved by, so
    // dragging TraceView carries its floating panels along instead of
    // leaving them behind at their old screen position.
    void moveEvent(QMoveEvent* event) override;
    // Re-derives the auto-detected screen-size breakpoint (see
    // applyAutoBreakpoint()) on every resize while in User mode. This is now
    // the primary way the breakpoint tracks the window's real size -- unlike
    // before the device-viewport preview existed, a resize genuinely can
    // change the result, so applyAutoBreakpoint()'s own dead band is what
    // keeps this from thrashing setBreakpoint() while a border is dragged
    // back and forth across a threshold.
    void resizeEvent(QResizeEvent* event) override;
    // Saves the window geometry restored by the constructor.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    // Rebuilds the Access menu's contents from UserModeManager's current
    // state -- a login prompt while in User mode, or the current username
    // plus user-management/logout actions while in Developer mode. Called
    // once from buildMenus() (User mode, nothing logged in yet) and again
    // from applyUserMode() on every mode change.
    void updateAccessMenu();
    // UserModeManager::modeChanged handler -- hides/shows the Devices tab,
    // the dashboard edit-mode lock, and workspace creation/deletion
    // depending on whether Developer mode is active (see each call's own
    // comment). Also called once, directly, right after construction so the
    // UI reflects the mode it always starts in (User) before the window is
    // ever shown.
    void applyUserMode(UserModeManager::UserMode mode);
    // Recomputes the auto-detected screen-size breakpoint from
    // m_dashboardScrollArea->viewport()'s own width and applies it to
    // m_dashboardGrid -- but only while in User mode; in Developer mode the
    // screen-size button (m_screenSizeButton) is the only thing that changes
    // the breakpoint, never a resize. Safe to call unconditionally from
    // anywhere (mode change, a dashboard reload, a resize) -- it's a no-op
    // outside User mode. In User mode there's no device frame (see
    // applyBreakpointViewport()), so the viewport's width already equals the
    // canvas's own -- the same thing Developer mode's manual preview reads
    // through the frame. Applies a +/-40px dead band around each threshold,
    // measured against the breakpoint already active, so dragging the window
    // border back and forth across a threshold doesn't thrash setBreakpoint()
    // (and the relayout() it triggers) once every pixel.
    void applyAutoBreakpoint();
    // The detection itself, minus the User-mode gate and without applying
    // anything: picks Small/Medium/Large from the viewport's width with a
    // +/-hysteresisPx dead band around each threshold measured against
    // `current` (0 = plain lookup). Returns `current` while the viewport has
    // no width yet. applyAutoBreakpoint() passes the resize dead band;
    // loadDashboardJson() passes 0 when a project is opened, so it starts on
    // this device's own breakpoint in either mode.
    DashboardBreakpoint detectBreakpoint(DashboardBreakpoint current, int hysteresisPx) const;
    // Which breakpoint loadDashboardJson() leaves active -- never the one
    // persisted in the dashboard's own JSON (that reflects whoever last
    // edited it, not this screen or the developer's current pick).
    enum class DashboardLoadBreakpoint {
        // Workspace switch/create/delete: keep whatever was already showing,
        // so a Developer-mode manual pick doesn't flip per workspace.
        KeepCurrent,
        // Project open/new: the breakpoint this device's screen calls for.
        DeviceDefault,
    };
    // Loads a dashboard's JSON into m_dashboardGrid with `policy`'s
    // breakpoint (auto-detected instead in User mode) already in place, so
    // the stored one never shows even briefly.
    // Every MainWindow call site that used to call m_dashboardGrid->fromJson()
    // directly goes through this instead.
    void loadDashboardJson(const QJsonObject& json,
                           DashboardLoadBreakpoint policy = DashboardLoadBreakpoint::KeepCurrent);
    // DashboardGrid::breakpointChanged handler -- keeps m_screenSizeButton's
    // icon/tooltip and its menu's checked entry in sync with whichever
    // breakpoint is actually active, however it got there (the button's own
    // menu, an undo/redo, a project load, or auto-detection). Icon/tooltip
    // only -- canvas +/- visibility is updateCanvasHeightButtons()'s job and
    // the options button's is now a fixed platform choice (see
    // kUsesCompactChrome in mainwindow.cpp); syncBreakpointChrome() is the
    // one place that still calls all of the breakpoint-driven updates together.
    void updateScreenSizeButtonIcon();
    // Canvas height +/- buttons' visibility/icons -- shown only for a
    // preview breakpoint (isPreviewBreakpoint()) while editingActive(), same
    // condition as before this was split out of updateScreenSizeButtonIcon().
    void updateCanvasHeightButtons();
    // Runs updateScreenSizeButtonIcon() and updateCanvasHeightButtons()
    // together -- the facade every call site that used to call the combined
    // updateScreenSizeButtonIcon() now calls instead (breakpointChanged,
    // applyUserMode(), updateRibbonIcons(), the ribbon tab change, the
    // edit-mode toggle).
    void syncBreakpointChrome();
    // The screen-size menu's own three actions (Small/Medium/Large) call
    // this instead of DashboardGrid::setBreakpoint() directly -- it does
    // that AND applies the matching device viewport (see
    // applyBreakpointViewport()). Manual selection only: auto-detection
    // (applyAutoBreakpoint(), User mode) never touches the viewport itself,
    // since there the canvas is presumed to already match the real screen
    // it's running on (no frame).
    void onScreenSizeBreakpointSelected(DashboardBreakpoint breakpoint);
    // True while Developer mode has a Small/Medium breakpoint selected --
    // the one condition that means "a manual device preview is up" (see
    // applyBreakpointViewport()). Factored out so compactChromeActive() can
    // reuse the exact same test instead of re-deriving "preview active" a
    // second way.
    bool previewActive() const;
    // Sets m_devicePreviewFrame's device size to the (unmultiplied) Small/
    // Medium viewport whenever previewActive() -- kSmallViewportSize/
    // kMediumViewportSize's height is no longer scaled by
    // m_dashboardGrid->canvasHeightMultiplier() here (that used to stretch
    // the DEVICE itself); a phone's screen is a fixed size, so growing the
    // canvas now grows m_dashboardGrid's own minimumHeight instead, scrolled
    // via m_dashboardScrollArea *inside* the fixed device -- see the .cpp.
    // QSize() (no frame) for Large or outside Developer mode. Called
    // whenever the breakpoint changes by any path (manual selection, auto-
    // detection, undo/redo, a project load), whenever the canvas height +/-
    // buttons change the multiplier for the currently active breakpoint, and
    // (via the eventFilter() on m_dashboardScrollArea's viewport) whenever
    // that viewport resizes, since the canvas-height pixel math depends on
    // its current size.
    void applyBreakpointViewport();
    // True whenever the app is showing the chrome a phone/tablet gets
    // instead of the desktop's native menu bar: always on Android
    // (kUsesCompactChrome), and on desktop while a Developer-mode Small/
    // Medium preview is up (previewActive()) -- so the preview genuinely
    // shows the mobile chrome the app will render on a phone, rather than a
    // desktop window with a few pieces hidden. Drives menuBar()/
    // m_optionsButton/m_chromeTopBar visibility (see updateChromeVisibility()).
    bool compactChromeActive() const;
    // The single place that reconciles menuBar()/m_optionsButton/
    // m_chromeTopBar visibility against BOTH compactChromeActive() and
    // fullscreen (m_fullscreenButton->isChecked()) -- the two independent
    // reasons the menu bar can be hidden, so neither one's logic clobbers
    // the other's (see onFullscreenToggled()). Called from syncBreakpointChrome()
    // (breakpoint/mode changes) and directly from onFullscreenToggled().
    void updateChromeVisibility();
    // Ribbon::removeTab() deletes the page synchronously; parks
    // m_optionsButton back in m_chromeTopBar first so it isn't deleted along
    // with the page it's currently hosted in (see updateChromeVisibility()).
    void removeRibbonTab(int index);
    // Puts m_screenSizeButton back in m_statusRow after compact chrome (or a
    // closing ribbon page) had it elsewhere -- see updateChromeVisibility().
    void restoreScreenSizeButtonToStatusRow();
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
    // Opens IconPickerDialog for `id` and stores the pick in WorkspaceManager.
    // Returns false when the user cancelled.
    bool pickWorkspaceIcon(const QString& id);
    // Prompts for a new name for `id` and stores it in WorkspaceManager.
    void renameWorkspace(const QString& id);
    // Re-applies m_dockController's geometry to every docked panel. Called
    // whenever m_contentRow resizes (see eventFilter) since the panels are
    // positioned directly rather than managed by a layout.
    void positionOverlayPanels();
    // Keeps m_brandCornerMark flush with m_appShell's top-right corner.
    void positionBrandCornerMark();
    void updateRibbonIcons();

    void onRibbonTabChanged(int index);
    // The "enable editing" lock toggle on the Dashboard tab's toolbar --
    // flips m_editModeEnabled and re-runs exactly the same follow-up calls
    // onRibbonTabChanged() used to make when the (now-removed) Layout tab
    // became active: DashboardGrid edit mode, m_addWidgetAction's enabled
    // state, panel visibility and selection-action gating.
    void onEditModeToggled(bool enabled);
    void updateEditModeIcon();
    // The panel show/hide toggle next to the lock -- the sole control for
    // m_layersPanel/m_propertiesPanel's visibility while editing (see
    // updatePanelVisibility()), so this is also how a user reaches
    // m_layersPanel's Add Widget button on an empty canvas, or reclaims
    // canvas space on demand without leaving edit mode.
    void onTogglePanelsClicked(bool visible);
    void updateTogglePanelsIcon();
    // Whether the Layout-era editing affordances (DashboardGrid edit mode,
    // its ribbon actions, the Layers/Properties panels) should be active --
    // true only while the Dashboard tab is current AND the edit-mode toggle
    // is on. Replaces the old m_configureTabActive, which used to be derived
    // straight from "is the Layout tab the current one".
    bool editingActive() const {
        return developerUiActive() && !m_subscriptionsWorkspaceActive &&
               m_dashboardTabActive && m_editModeEnabled;
    }
    bool developerUiActive() const {
        return UserModeManager::instance().mode() == UserModeManager::UserMode::Developer &&
               !m_previewAsUser;
    }
    void onSelectionChanged(const QString& itemId);
    void updateSelectionActions();
    // Shows m_propertiesPanel/m_layersPanel only while the Dashboard tab is
    // active AND editing is enabled (see editingActive()) AND the panel
    // show/hide toggle is on (m_panelsVisible) -- selection used to also
    // open them on its own, which made them pop in/out independent of the
    // toggle's own state. Called on tab change, selection change, the
    // edit-mode toggle, and the panel show/hide toggle.
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
    // editingActive()-gated condition, kept mutually exclusive so both
    // never share an enabled Delete shortcut at once).
    void updateDeviceSelectionActions();
    // Builds and wires one DeviceConnection the way onDeviceAdded() always
    // has (connectionStateChanged/backend signals/deviceIdentified) --
    // factored out so onDeviceUpdated() can rebuild one in place too, when a
    // device's transportType itself changes (see its own comment).
    DeviceConnection* createDeviceConnection(const Device& device);
    // Resolves a Device's own BTP identity the same way for both a
    // hub-channel device (its persisted target, known even before it ever
    // connects) and anything else (its live-reported btpId) -- shared by
    // refreshPropertiesPanelDevices() and hubPeersFor() so the two
    // definitions of "this device's own source_id" can't drift apart.
    quint32 deviceSelfSourceId(const Device& device) const;
    // Keep m_deviceConnections (one DeviceConnection per Device::id) in sync
    // with m_devicesGrid's own list -- wired to DevicesGrid::deviceAdded/
    // deviceRemoved/deviceUpdated. onDeviceUpdated also re-points the
    // connection at a possibly-changed port/baud/line-terminator, and
    // rebuilds the connection entirely if transportType itself changed
    // (DeviceConnection's Transport/Backend pair is fixed at construction,
    // see deviceconnection.h -- it can't be swapped on a live instance).
    // Points one connection at whatever its Device says its target is,
    // dispatching on transport: a port name, a HID path, or -- for a hub
    // channel -- the parent connection plus the robot's source_id. The one
    // place that knows all three, so the three call sites below do not each
    // have to.
    void applyDeviceTarget(DeviceConnection* connection, const Device& device);
    // Re-points every hub-channel child at its parent. Needed because a
    // child can exist before its parent does: loading a project walks the
    // saved device list in order, and nothing guarantees a hub comes before
    // the devices that ride it. Cheap enough to just re-run after any device
    // add/remove/update rather than tracking which children a change could
    // possibly have affected.
    void reattachHubChildren();
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
    // m_removeDeviceAction's triggered handler -- both the Devices tab and
    // the OTA tab share this one action (see buildRibbon()/onOpenOtaTab()),
    // so this dispatches to whichever tab's own selection is current rather
    // than always going through DevicesGrid::removeSelected() directly.
    void onRemoveDeviceRequested();
    // Converts m_devicesGrid->devices() into DeviceOptions and pushes them
    // into m_propertiesPanel -- called whenever the device list changes, so
    // every widget config editor's Device combo stays current.
    void refreshPropertiesPanelDevices();
    // Same fan-out as refreshPropertiesPanelDevices() above, for the OTA
    // tab's device list -- a no-op if m_otaTab hasn't been opened yet.
    void refreshOtaTabDevices();
    // Lazily starts watching `parentDeviceId`'s hub.peers topic (resolved by
    // name from its own catalog, never a hardcoded topic/field id -- see
    // m_hubPeerWatches) the first time it is asked for, then returns
    // whatever HubPeer snapshot has been decoded from it so far (empty until
    // the device is connected, its manifest exchange has completed, and the
    // first full sample has arrived). Safe to call on every poll -- wired to
    // DevicesGrid::setHubPeerListProvider() in the constructor.
    QVector<HubPeer> hubPeersFor(const QString& parentDeviceId);
    // Resolves + subscribes `parentDeviceId`'s hub.peers watch if it is not up
    // yet; a no-op once it is. Returns true when the watch has a live
    // subscription handle. Shared by hubPeersFor() (the config dialog's poll)
    // and syncHubPeerWatches() (the always-on watch every connected child
    // needs).
    bool ensureHubPeerWatch(const QString& parentDeviceId);
    // Ensures a hub.peers watch exists for the parent of every currently
    // connected HubChannel child -- so online/offline and robot-reboot
    // detection work without a config dialog being open. Called from the
    // reconcile timer and on connection-state changes. Purely additive; a
    // watch is only torn down when its parent device is removed/rebuilt
    // (releaseHubPeerWatch()).
    void syncHubPeerWatches();
    // Once per second: recomputes each connected HubChannel child's
    // Device::peerOnline/peerPresenceKnown/peerBootId (driving the card dot and
    // the dashboard cells' dot) from the robot's own end-to-end frames
    // (BtpBackend::lastPeerDataFrameMsSinceEpoch, primary) with the dongle's
    // decoded hub.peers as the fallback, and hands an on/off change to that
    // child's Backend::onPeerPresence() so it can re-request its catalog /
    // re-subscribe after a robot reboot or a return from out-of-range.
    void reconcileHubChildPresence();
    // Drops `deviceId`'s hub.peers watch and unsubscribes it. Called when the
    // device is removed or rebuilt; hubPeersFor() re-establishes the watch
    // from scratch the next time somebody asks.
    void releaseHubPeerWatch(const QString& deviceId);
    // Backend::fieldSample handler shared by every DeviceConnection (hooked
    // in createDeviceConnection()), filtered down to whichever ones are
    // currently a watched hub.peers subscription. Decodes the six PACKED_LE
    // variable arrays telemetry.md 4.1 / bally_dongle's DonglePublisher.h
    // kPeersFields describe back into m_hubPeersAccum -- see the .cpp for
    // the exact field-name to struct-member mapping.
    void onHubPeerFieldSample(const QString& deviceId, const TelemetryFieldBinding& binding,
                              quint64 timestampUs, double value);
    void onPanelTypeChangeRequested(const QString& typeId);
    void onPanelNameChangeRequested(const QString& name);
    void onPanelKeyChangeRequested(const QString& key);
    void onPanelConfigChangeRequested(const QJsonObject& config);
    void onNewProject();
    void onSaveProject();
    void onSaveProjectAs();
    void onOpenProject();
    // File > Add to Gallery: stores the current dashboard as an in-app
    // gallery entry (see project/dashboardgallery.h) and keeps it as the
    // open project, so Save updates that entry from then on.
    void onAddToGallery();
    void onOpenGallery();
    void onOpenLogFile();
    // RibbonTabBar's "x" click (forwarded through Ribbon::tabCloseRequested)
    // on one of m_openLogTabs -- removes that tab and deletes its LogViewer.
    void onLogTabCloseRequested(int index);
    // File > "Upload Firmware (OTA)..." -- opens the singleton OTA tab
    // (creating it on first use) or just switches to it if it's already
    // open. Unlike onOpenLogFile(), there is never more than one of these.
    void onOpenOtaTab();
    // Dispatched to from onLogTabCloseRequested() (a plain call, not a
    // second connection on the same signal -- Ribbon::removeTab() shifts
    // every later tab's index, so a second slot recomputing pageAt(index)
    // after the first slot has already mutated the ribbon would be looking
    // at the wrong tab). Only ever called before either handler has touched
    // the ribbon for this close event.
    void onOtaTabCloseRequested(int index);
    // File > "BTP Traffic Monitor..." -- opens the singleton BTP monitor tab
    // (creating it on first use) or switches to it. Same singleton-closable-tab
    // lifecycle as onOpenOtaTab()/onOtaTabCloseRequested().
    void onOpenBtpMonitor();
    void onBtpMonitorTabCloseRequested(int index);
    // Menu bar "Settings" / Ctrl+, -- shows the Settings window (creating it
    // on first use) or raises it. Presented like the notification history:
    // a separate window on desktop, an in-window page with a back arrow when
    // DialogPresenter embeds dialogs (Android, Small/Medium preview) -- so
    // it never depends on the ribbon's tab strip, which User mode hides.
    void onOpenSettings();
    // Script icon on a device's card (DeviceCard::scriptRequested) -- opens
    // that device's DiagramScriptRuntime script editor
    // (DiagramBlockConfigDialog). The canvas/diagram tab this used to live
    // under is shelved for now (lib/diagram/diagrampage.h and friends are
    // unused but kept for later); the runtime and its live telemetry/
    // terminal wiring are not -- see m_scriptRuntimes below.
    void onDeviceScriptRequested(const QString& deviceId);
    // Status-bar history button / View menu -- shows (or raises) the non-modal
    // window listing every status-bar message posted this session.
    void onShowNotificationHistory();
    // View menu / F1 -- the read-only reference of every keyboard shortcut,
    // built from the QActions here plus the terminal's own fixed chords.
    void onShowKeyboardShortcuts();
    // The single choke point every status-bar message goes through: shows it on
    // the status bar for `timeoutMs` AND records it in m_notificationLog with
    // `severity`/`source` so the history window can replay it. `source` is a
    // device name for anything a Backend emitted, empty for app-level messages.
    void postStatus(const QString& text, int timeoutMs,
                    traceview::StatusSeverity severity = traceview::StatusSeverity::Info,
                    const QString& source = QString());
    // Replaces statusBar()->showMessage(text, timeoutMs) now that m_statusRow
    // is a plain QWidget, not a QStatusBar: sets m_statusMessageLabel's text
    // and (re)starts m_statusMessageTimer to clear it after timeoutMs (0 =
    // stays until replaced, same as QStatusBar's own convention). A member
    // QTimer restarted on every call, rather than a fresh QTimer::singleShot
    // each time, so an earlier message's timer can't outlive it and clear a
    // newer one early. postStatus() is the sole caller.
    void showStatusMessage(const QString& text, int timeoutMs);
    // Forwards OtaTab::passwordCacheChanged into an actual Device mutation --
    // OtaTab has no undo stack of its own to push this onto.
    void onOtaPasswordCacheChanged(const QString& deviceId, const QString& password, bool cache);
    void openRecentFile(const QString& path);
    // The load + refresh sequence behind openRecentFile(), without its error
    // dialog or recent-files bookkeeping. False (ProjectStore::lastError()
    // says why) leaves the current dashboard untouched.
    bool loadProjectFile(const QString& path);
    // Opens the gallery's default dashboard, falling back to the built-in
    // example -- run once at startup, after the window is first shown so the
    // breakpoint is picked against its real size.
    void openStartupDashboard();
    // Runs openStartupDashboard() once, then drops m_startupOverlay; later
    // calls are no-ops (see the constructor's startup wiring).
    void finishStartup();
    // Copies the live workspaces/devices state into ProjectStore's sections
    // ahead of a save.
    void syncProjectSections();
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void onClearRecentFiles();
    void onAbout();
    void onDonate();
    void onDebug();
    void onFullscreenToggled(bool checked);

    // Fires once, ~5s after startup: runs checkForUpdates(false) if the user
    // hasn't disabled auto-checking. Runs on every launch, regardless of
    // the last successful check. Settings ▸ Updates' "Check now" button
    // goes through checkForUpdates(true) instead.
    void maybeCheckForUpdatesOnStartup();
    // `manual` distinguishes a user-initiated check from the startup one:
    // only a manual check surfaces "up to date"/failure feedback (via
    // postStatus).
    void checkForUpdates(bool manual);
    void onUpdateAvailable(const UpdateInfo& info);
    void onUpdateUpToDate();
    void onUpdateCheckFailed(const QString& reason);
    // Kicks off UpdateDownloader for `info`'s platform asset -- or, if this
    // release has nothing installable for this platform (or no
    // SHA256SUMS.txt to verify it against), opens the release page instead.
    void startUpdateDownload(const UpdateInfo& info);
    void onUpdateDownloadFinished(const QString& filePath);
    void onUpdateDownloadFailed(const QString& reason);
    // Relaunches TraceView to apply a restart-only setting (language,
    // diagnostics history). On Android, which can't relaunch an app from
    // inside itself, asks to close it instead (Close App / Later).
    void restartApplication();

    DashboardGrid* m_dashboardGrid = nullptr;
    // Devices tab's content -- swapped in for m_dashboardGrid via
    // m_contentStack, never shown at the same time (see onRibbonTabChanged).
    DevicesGrid* m_devicesGrid = nullptr;
    // One closable ribbon tab per log opened via onOpenLogFile() (File >
    // Open Log Offline), each swapped into m_contentStack while its tab is
    // active -- unlike m_devicesGrid, these tabs are created/destroyed at
    // runtime rather than being fixed. `ribbonPage` is the (empty) page
    // Ribbon::addTab() was given for that tab; since it's never touched
    // again after creation, it doubles as a stable key to find this entry
    // back from a tab index via Ribbon::pageAt() (indices themselves shift
    // whenever another log tab closes).
    struct OpenLogTab {
        QWidget* ribbonPage = nullptr;
        LogViewer* viewer = nullptr;
    };
    QVector<OpenLogTab> m_openLogTabs;
    // The OTA Update tab -- singleton, unlike m_openLogTabs above: there's
    // only one, opened on demand by onOpenOtaTab() and reused (never
    // recreated) if the File menu action fires again while it's already
    // open. m_otaTabPage is the same "empty ribbon page as a stable lookup
    // key" trick m_openLogTabs uses; nullptr on both means the tab has never
    // been opened yet (or was closed).
    QWidget* m_otaTabPage = nullptr;
    OtaTab* m_otaTab = nullptr;
    // The BTP Traffic Monitor tab -- same singleton-closable-tab lifecycle and
    // "empty ribbon page as a stable key" trick as the OTA tab above.
    QWidget* m_btpMonitorTabPage = nullptr;
    BtpMonitorTab* m_btpMonitorTab = nullptr;
    // The Settings window and the SettingsPage it wraps -- WA_DeleteOnClose'd
    // like m_notificationWindow, so both QPointers null out on close and the
    // next onOpenSettings() builds a fresh one.
    QPointer<QDialog> m_settingsWindow;
    QPointer<SettingsPage> m_settingsPage;
    // One live script engine per device (lib/diagram/diagramscriptruntime.h),
    // created alongside its DeviceConnection in onDeviceAdded() and kept for
    // that device's whole lifetime -- independent of whether its script
    // editor (DiagramBlockConfigDialog, opened via the card's script icon)
    // is currently open. Feeds telemetry/terminal in createDeviceConnection()
    // and relays its own sendCommand/sendTerminal calls back to that same
    // device's Backend.
    QHash<QString, DiagramScriptRuntime*> m_scriptRuntimes;
    // App-wide in-memory diagnostics buffers (lib/diagnostics). Owned here,
    // created first thing in the constructor; every DeviceConnection's Backend
    // feeds them (see createDeviceConnection) and postStatus() feeds
    // m_notificationLog. Not persisted -- empty on every launch.
    NotificationLog* m_notificationLog = nullptr;
    FrameLog* m_frameLog = nullptr;
    // WA_DeleteOnClose'd like m_debugChartsWindow -- QPointer so it nulls out
    // on close and a second "show history" just makes a fresh one.
    QPointer<NotificationHistoryWindow> m_notificationWindow;
    // One real, independent serial connection per Device::id -- see
    // core/deviceconnection.h. Created/destroyed/updated in lockstep with
    // m_devicesGrid's own list (onDeviceAdded/onDeviceRemoved/onDeviceUpdated).
    QHash<QString, DeviceConnection*> m_deviceConnections;
#ifdef TRACEVIEW_ENABLE_BLE
    // Backs DevicesGrid::setBleScanToggleHandler()/setBleDeviceListProvider()
    // (TAREFAS_TCP_BLE_ANDROID.txt T27/T32) -- traceview_devices can't own
    // this itself (doesn't depend on Qt6::Bluetooth, see lib/CMakeLists.txt).
    // Created on first scan, torn down (deleteLater) on stop rather than
    // kept idle between scans -- same one-shot-per-attempt lifecycle
    // BleDiscoveryService itself already gives start()/stop().
    BleDiscoveryService* m_bleDiscovery = nullptr;
    // Every (name, address) seen since the discovery service's own list was
    // last cleared (on each fresh start() -- see onBleScanToggled()),
    // de-duplicated by address so a peripheral re-advertising repeatedly
    // doesn't grow this without bound. What setBleDeviceListProvider()'s
    // polled callback reads.
    QVector<QPair<QString, QString>> m_bleDiscoveredDevices;
    void onBleScanToggled(bool start);
#endif
    // Holds the dashboard, Devices grid and Settings page; whichever one is current is
    // what fills m_contentRow. Unlike the layers/properties panels below,
    // this genuinely shares layout space (contentLayout->addWidget()) rather
    // than floating -- DevicesGrid isn't subject to DashboardGrid's
    // fraction-of-canvas geometry constraint, so swapping/resizing it here
    // doesn't reflow anything the way shrinking the canvas would.
    QStackedWidget* m_contentStack = nullptr;
    // Wraps m_dashboardGrid directly (setWidget(m_dashboardGrid)) so a canvas
    // taller than the device's own screen -- Small/Medium, once canvas height
    // +/- has grown m_dashboardGrid's minimumHeight past it, see
    // applyBreakpointViewport() -- gets a scrollbar *inside* the device
    // instead of the device itself growing. Unlike before the whole-app
    // preview (see m_devicePreviewFrame below), this no longer wraps a frame:
    // the frame moved up to emolder the entire app, not just this canvas.
    QScrollArea* m_dashboardScrollArea = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    LayersPanel* m_layersPanel = nullptr;
    // Row below the ribbon that hosts the canvas; m_layersPanel/
    // m_propertiesPanel are children of this (not of a layout) so they can
    // float above m_dashboardGrid instead of sharing its row -- see
    // positionOverlayPanels().
    QWidget* m_contentRow = nullptr;
    // Compact top bar: options on the left. The screen-size selector lives
    // in the status row. Hidden on desktop by updateChromeVisibility().
    QWidget* m_chromeTopBar = nullptr;
    // Everything the app actually shows -- m_chromeTopBar, the ribbon,
    // m_contentRow, m_statusRow -- reparented here as this frame's sole
    // content (see DevicePreviewFrame::setContentWidget()) so a Developer-
    // mode Small/Medium preview frames the WHOLE app, not just the dashboard
    // canvas: the ribbon, m_chromeTopBar (which is what a phone build shows
    // instead of the native menu bar -- see compactChromeActive()) and
    // m_statusRow (which replaces the native status bar) all shrink to
    // device size together, exactly as they will on Android. Sits directly
    // inside m_devicePreviewFrame; QSize() (no frame, Large/User mode)
    // just lets it fill the frame's own rect() unchanged. See
    // MainWindow::MainWindow() for the full widget tree.
    QWidget* m_appShell = nullptr;
    // Frames m_appShell inside a device-shaped viewport for a manual Small/
    // Medium preview (Developer mode only), or lets it fill the whole central
    // widget for Large/User mode -- see devicepreviewframe.h. What
    // setCentralWidget()'s own top-level layout actually wraps now (in place
    // of the ribbon/m_contentRow stack directly); m_appShell becomes its
    // child instead.
    DevicePreviewFrame* m_devicePreviewFrame = nullptr;
    // Owns the panels' drag-to-dock/float behavior -- see paneldockcontroller.h.
    PanelDockController* m_dockController = nullptr;
    // Guards showEvent() so applyFloatingPositions() runs once, on the
    // window's first real appearance, rather than snapping any
    // since-repositioned floating panel back every time the window is
    // reshown (e.g. after minimizing).
    bool m_floatingPanelsPositioned = false;
    Ribbon* m_ribbon = nullptr;
    WorkspaceSwitcher* m_workspaceSwitcher = nullptr;
    // Takes m_statusRow's place in compact chrome (see
    // updateChromeVisibility()): the bottom bar becomes icon-only workspace
    // navigation. Fed the same entries as m_workspaceSwitcher by
    // refreshWorkspaceSwitcher().
    WorkspaceDock* m_workspaceDock = nullptr;
    // User-mode-only wordmark overlaid on m_appShell's top-right corner --
    // see brandcornermark.h; shown/hidden by updateChromeVisibility().
    BrandCornerMark* m_brandCornerMark = nullptr;
    // Covers the window until the startup dashboard is built; null after.
    StartupLoadingOverlay* m_startupOverlay = nullptr;
    QAction* m_addWidgetAction = nullptr;
    QAction* m_addDeviceAction = nullptr;
    QAction* m_removeDeviceAction = nullptr;
    QAction* m_openLogFileAction = nullptr;
    QAction* m_openOtaTabAction = nullptr;
    QAction* m_openBtpMonitorAction = nullptr;
    QAction* m_openSettingsAction = nullptr;
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
    // Top-level "Access" menu -- login/logout and user management, rebuilt
    // from scratch by updateAccessMenu() on every mode change (its content
    // differs enough between User/Developer that toggling individual action
    // visibility isn't simpler than just clearing and re-adding).
    QMenu* m_accessMenu = nullptr;
    // Developer-only: hidden (and its shortcuts disabled) by applyUserMode()
    // whenever developerUiActive() is false, "View as user" included.
    QMenu* m_fileMenu = nullptr;
    QAction* m_debugAction = nullptr;
    QAction* m_keyboardDiagnosticsAction = nullptr;
    QAction* m_copyKeyboardLogAction = nullptr;
    // View's Developer-only entries (Keyboard Shortcuts, Open Log Folder,
    // Reset Panel Positions and the separator after them) -- User mode keeps
    // only Theme/Font/Language there.
    QList<QAction*> m_developerViewActions;
    // Visual preview only: preserves the developer session and manual breakpoint.
    bool m_previewAsUser = false;
    // WA_DeleteOnClose'd (see debugchartswindow.cpp) -- QPointer so this
    // resets to null on its own once the user closes it, instead of leaving
    // a dangling raw pointer behind for the next "Debug" click to dereference.
    QPointer<DebugChartsWindow> m_debugChartsWindow;
    int m_devicesTabIndex = -1;
    // Whether the Dashboard tab (m_dashboardTabIndex) is the current one --
    // combined with m_editModeEnabled by editingActive() to decide whether
    // the Layout-era editing affordances should be active.
    bool m_dashboardTabActive = false;
    // The "enable editing" lock toggle's state, set by onEditModeToggled().
    // Unlike the old m_configureTabActive, this is a persistent user choice,
    // not derived from which tab is current -- switching to Devices and back
    // to Dashboard leaves it exactly as the user left it.
    bool m_editModeEnabled = false;
    // The panel show/hide toggle's state (see onTogglePanelsClicked()) --
    // the sole control for whether m_layersPanel/m_propertiesPanel are shown
    // while editing. Defaults to true so turning editing on shows them
    // immediately, even on an empty dashboard with nothing selected --
    // otherwise there'd be no visible way to reach Add Widget at all.
    bool m_panelsVisible = true;
    // Gates m_removeDeviceAction the same way editingActive() gates
    // m_removeAction -- see updateDeviceSelectionActions().
    bool m_devicesTabActive = false;
    // Same gating for m_removeDeviceAction while the OTA tab is the visible
    // one instead -- set in onRibbonTabChanged() by page pointer, not tab
    // index, since the OTA tab is closable and can shift (see m_otaTabPage).
    bool m_otaTabActive = false;
    // While a project populates DevicesGrid, preserve the user's startup
    // preference instead of immediately opening every persisted target.
    bool m_loadingProject = false;

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
    // Recomputes the built-in subscriptions table (requested vs. effective
    // rate, plus bytes/drops from a status_version=2 STATUS), aggregated
    // across every connected device's Backend.
    void updateSubscriptionsWorkspace();
    // Rebuilds the Dashboard tab's device status strip from m_devicesGrid's
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

    // One hub's live hub.peers state, from the moment something first asks
    // for it (hubPeersFor()) until its device goes away. Keyed by the HUB's
    // Device::id, not by any child's.
    //
    // `sourceId`/`topicId`/`fieldSlot` are resolved once from that device's
    // own catalog, by NAME ("hub.peers", then "channel"/"source_id"/... ),
    // never from a hardcoded number: telemetry.md section 1 makes topic and
    // field ids local to a source's namespace, so the dongle is free to
    // renumber them and only the names are a contract. Resolution is
    // therefore deferred until the manifest has actually arrived -- until
    // then there is nothing to resolve against and hubPeersFor() reports an
    // empty list.
    //
    // The reassembly itself is HubPeerAccumulator's job (devices/
    // hubpeeraccumulator.h) -- decoding six parallel arrays out of one
    // (field, element, value) emission at a time is protocol shape, not UI,
    // and keeping it there is what makes it testable without a QWidget.
    // What stays here is the half that genuinely needs a Backend: which
    // (source, topic) to watch and the subscription handle for it.
    struct HubPeersWatch {
        quint32 sourceId = 0;
        quint16 topicId = 0;
        quint64 handle = 0;  // SubscriptionManager handle, 0 = not subscribed
        HubPeerAccumulator accumulator;
    };
    QHash<QString, HubPeersWatch> m_hubPeerWatches;
    // Drives reconcileHubChildPresence() at 1 Hz -- the dongle caps hub.peers
    // at 2 Hz and its "online" window is 4 s, so a 1 s reconcile is plenty.
    QTimer* m_hubPeerReconcileTimer = nullptr;
    // Per hub child (keyed by Device::id): consecutive reconcile ticks the
    // FALLBACK hub.peers path has read the robot offline. Debounces that
    // direction only (a single missed hub.peers sample must not flap the card
    // or hand BtpBackend an on/off/on); cleared the moment the robot's own
    // frames are flowing, which bypasses this path entirely.
    QHash<QString, int> m_hubChildOfflineTicks;
    // Own status row replacing the native QStatusBar (see MainWindow::
    // showStatusMessage()/buildRibbon()/buildMenus()) -- a plain QWidget so
    // it lives inside m_appShell/m_devicePreviewFrame like everything else
    // the preview needs to frame, which a real QStatusBar (owned by
    // QMainWindow itself, outside centralWidget()) could not. Normal widgets
    // (m_fullscreenButton) on the left, a
    // stretch (m_statusMessageLabel doubles as it), permanent widgets
    // (m_workspaceSwitcher, m_screenSizeButton) on the right -- same
    // visual order the old statusBar()->addWidget()/addPermanentWidget() call
    // sites built, minus m_optionsButton (moved to m_chromeTopBar instead).
    QWidget* m_statusRow = nullptr;
    // What showStatusMessage() writes to -- sits where QStatusBar's own
    // transient message area did, between the normal and permanent widgets
    // (see m_statusRow above), with an expanding size policy so it also
    // supplies m_statusRow's stretch.
    QLabel* m_statusMessageLabel = nullptr;
    // Restarted (never a fresh QTimer::singleShot) by every showStatusMessage()
    // call so an earlier message's timeout can't fire after a newer message
    // replaced it and clear that one early instead.
    QTimer* m_statusMessageTimer = nullptr;
    QTreeWidget* m_subscriptionsTable = nullptr;
    bool m_subscriptionsWorkspaceActive = false;
    int m_dashboardTabIndex = -1;
    // Read-only "device: dot" strip replacing the old single-connection port/
    // baud/connect bar -- per-device connection config now lives in the
    // Devices tab (DeviceConfigDialog) instead.
    QLabel* m_deviceStatusLabel = nullptr;
    // Panel show/hide toggle and "enable editing" lock, top-right of the
    // Dashboard tab's toolbar in that order (m_deviceStatusLabel sits at its
    // top-left) -- see onTogglePanelsClicked()/onEditModeToggled().
    QToolButton* m_togglePanelsButton = nullptr;
    // Screen-size breakpoint toggle (phone/tablet/notebook) -- Developer-
    // mode-only. Lives at the right of the status row, including View as user.
    // Its menu's
    // three actions call DashboardGrid::setBreakpoint() directly;
    // updateScreenSizeButtonIcon() keeps the button's own icon/tooltip and
    // the menu's checked entry in sync with whatever's actually active.
    QToolButton* m_screenSizeButton = nullptr;
    QMenu* m_screenSizeMenu = nullptr;
    // "More options" -- mirrors the File/View/Access menus as a plain widget
    // of m_chromeTopBar (see buildMenus()), not the status bar anymore (see
    // m_statusRow above): compactChromeActive() now governs its visibility
    // (via updateChromeVisibility()), not just the fixed kUsesCompactChrome
    // platform choice alone -- a desktop Developer-mode Small/Medium preview
    // needs it shown too, same reasoning as compactChromeActive()'s own
    // comment (the preview must show the real mobile chrome, not a desktop
    // window with pieces hidden).
    QToolButton* m_optionsButton = nullptr;
    // Canvas height +/- -- Small/Medium only, hidden otherwise (see
    // updateCanvasHeightButtons()). Call DashboardGrid::growCanvasHeight()/
    // shrinkCanvasHeight() directly.
    QToolButton* m_canvasShrinkButton = nullptr;
    QToolButton* m_canvasGrowButton = nullptr;
    QToolButton* m_editModeButton = nullptr;
    QToolButton* m_fullscreenButton = nullptr;
    bool m_wasMaximized = false;
    QByteArray m_preFullscreenGeometry;

    // Self-update (see lib/updater). Both created once in the constructor and
    // reused for every check/download -- unlike m_settingsWindow, there is
    // always exactly one of each for the app's whole lifetime.
    UpdateChecker* m_updateChecker = nullptr;
    UpdateDownloader* m_updateDownloader = nullptr;
    // Set right before each checkForUpdates() call and read back by the
    // UpdateChecker signal handlers, since the signal itself carries no
    // record of which call triggered it.
    bool m_updateCheckWasManual = false;
};

}  // namespace traceview
