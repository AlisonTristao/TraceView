#include "mainwindow.h"

#include <QActionGroup>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMoveEvent>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#ifdef TRACEVIEW_ENABLE_SERIAL
#include <QSerialPortInfo>
#endif
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoGroup>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "aboutdialog.h"
#include "applog.h"
#include "backend/backend.h"
#ifdef TRACEVIEW_ENABLE_BLE
#include "blediscoveryservice.h"
#endif
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgetregistry.h"
#include "dashboard/widgets/chartwidgets.h"
#include "dashboard/widgets/textboardwidget.h"
#include "debugchartswindow.h"
#include "deviceconnection.h"
#include "devicepreviewframe.h"
#include "devices/devicesgrid.h"
#include "diagnostics/btpmonitortab.h"
#include "diagnostics/framelog.h"
#include "diagnostics/notificationhistorywindow.h"
#include "diagnostics/notificationlog.h"
#include "diagram/diagramblockconfigdialog.h"
#include "diagram/diagramscriptruntime.h"
#include "donatedialog.h"
#include "fontmenuaction.h"
#include "layerspanel.h"
#include "logindialog.h"
#include "logs/logviewer.h"
#include "manageusersdialog.h"
#include "ota/otatab.h"
#include "paneldockcontroller.h"
#include "preferences/appsettings.h"
#include "project/projectstore.h"
#include "project/workspacemanager.h"
#include "propertiespanel.h"
#include "protocol/btpbackend.h"
#include "protocol/btpframe.h"
#include "protocol/keyderivation.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "serialwidgetbridge.h"
#include "settingspage.h"
#include "shortcutsdialog.h"
#include "theme/dialogpresenter.h"
#include "theme/iconlibrary.h"
#include "traceview/fontmanager.h"
#include "traceview/languagemanager.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"
#include "updateavailabledialog.h"
#include "updater/updatechecker.h"
#include "updater/updatedownloader.h"
#include "updater/updateinstaller.h"
#ifdef TRACEVIEW_ENABLE_USB_HID
#include "usbhidmanager.h"
#endif
#include "iconpickerdialog.h"
#include "workspacedock.h"
#include "brandcornermark.h"
#include "workspaceswitcher.h"

namespace traceview {

namespace {
// Namespace-scope, outside MainWindow -- plain tr() isn't available here, so
// this uses QCoreApplication::translate() directly, same as the non-QObject
// fixes elsewhere in the codebase.
const QString kProjectFileFilter =
    QCoreApplication::translate("MainWindow", "TraceView Project (*.tvproj)");
constexpr const char* kRecentFilesSettingsKey = "recentFiles/paths";
constexpr const char* kSubscriptionsWorkspaceId = "builtin:subscriptions";
// IconLibrary ids for workspace buttons (WorkspaceSwitcher/WorkspaceDock):
// what a workspace shows until the user picks its own icon, and the fixed
// one of the built-in Subscriptions entry.
constexpr const char* kDefaultWorkspaceIconId = "lucide:layout-dashboard";
constexpr const char* kSubscriptionsWorkspaceIconId = "lucide:rss";

// The stored pick when this build knows it, else the default -- an empty
// (never picked) or unknown (newer project, renamed upstream) id alike.
QString resolvedWorkspaceIcon(const QString& stored) {
    return IconLibrary::instance().contains(stored) ? stored
                                                    : QString::fromLatin1(kDefaultWorkspaceIconId);
}

// Thresholds for auto-detecting a screen-size breakpoint from
// m_dashboardScrollArea->viewport()'s own width (see MainWindow::
// applyAutoBreakpoint()), in logical pixels. Starting defaults, not
// validated against real phone/tablet hardware yet -- easy to retune once
// this runs on an actual small screen.
constexpr int kSmallBreakpointMaxViewportWidth = 700;
constexpr int kMediumBreakpointMaxViewportWidth = 1280;
// Dead band applied around each threshold above, measured against the
// breakpoint already active (see applyAutoBreakpoint()) -- without it, a
// window resize that lands a pixel to either side of a threshold while
// being dragged would flip setBreakpoint() (and the relayout() it causes)
// back and forth on every pixel crossed.
constexpr int kBreakpointHysteresisPx = 40;

// Platforms where QMenuBar isn't a dependable route to File/View/Access
// (see docs/ANDROID_BUILD.md's T47 notes) and m_chromeTopBar's overflow
// button (m_optionsButton) answers for them instead. First platform #ifdef
// in lib/ -- until now the dashboard breakpoint stood in for this, which
// made the button vanish on a tablet wide enough to auto-detect Notebook.
// Just the platform half of the story now -- see MainWindow::
// compactChromeActive(), which ORs this with a desktop Developer-mode
// preview being up, so a Phone/Tablet preview shows the exact chrome
// Android gets instead of a desktop window with pieces hidden.
constexpr bool kUsesCompactChrome =
#ifdef Q_OS_ANDROID
    true;
#else
    false;
#endif

// What a gauge asks for: it has no sample-time setting of its own (a gauge
// only ever shows the newest value), so it requests a modest fixed rate
// instead of inventing a config field for it.
constexpr quint32 kGaugeRequestedRateMillihz = 5000;  // 5 Hz
// Fallback for a chart whose configured sample time is unusable (<= 0).
constexpr quint32 kDefaultRequestedRateMillihz = 10000;  // 10 Hz

// Converts a chart's configured sample period into the rate its SUBSCRIBE
// asks for. This is only a *request*: the source clamps it to its schema's
// min/max and reports the effective rate in SUBSCRIBE_RESULT, which is what
// the status bar then shows (topico 17).
quint32 requestedRateMillihzFor(double sampleTimeMs) {
    if (!(sampleTimeMs > 0.0)) {
        return kDefaultRequestedRateMillihz;
    }
    const double millihz = 1'000'000.0 / sampleTimeMs;  // 1000 Hz-per-ms * 1000 milli
    if (millihz < 1.0) {
        return 1;
    }
    if (millihz > 4'000'000'000.0) {
        return 4'000'000'000u;
    }
    return quint32(millihz);
}

QString formatRateMillihz(quint32 millihz) {
    return QString::number(millihz / 1000.0, 'g', 4) + " Hz";
}

// The (device, source_id, topic_id, requested rate) one dashboard widget
// implies. deviceId empty and the rest zero for a widget that is not a
// telemetry consumer, one whose source/topic has not been configured yet, or
// one with no device picked in its config editor's Device combo (see
// dashboard/widgetconfigeditor.h's DeviceOption) -- MainWindow treats that as
// "no consumer" and puts nothing on any wire.
struct WidgetTopicRequest {
    QString deviceId;
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint32 rateMillihz = 0;
};

WidgetTopicRequest widgetTopicRequest(DashboardWidget* widget, DashboardGrid* grid) {
    const QString deviceId = grid->configForWidget(widget).value("deviceId").toString();
    WidgetTopicRequest request;
    if (auto* chart = dynamic_cast<ChartWidgetBase*>(widget)) {
        const ChartConfig& config = chart->config();
        request = {deviceId, config.sourceId, config.topicId,
                   requestedRateMillihzFor(config.sampleTimeMs)};
    } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(widget)) {
        const GaugeConfig& config = gauge->config();
        request = {deviceId, config.sourceId, config.topicId, kGaugeRequestedRateMillihz};
    } else if (auto* board = dynamic_cast<TextBoardWidget*>(widget)) {
        const TextBoardConfig& config = board->config();
        request = {deviceId, config.sourceId, config.topicId,
                   requestedRateMillihzFor(config.sampleTimeMs)};
    } else {
        return {};
    }

    // Settings > Dashboard's subscribe rate override, when on, replaces
    // whatever rate the widget itself asked for -- one dial for the whole
    // dashboard's subscribe load instead of each widget's own sample time.
    // The topic's own max/min on the robot still clamps it either way.
    if (request.topicId != 0 && AppSettings::instance().subscribeRateOverrideEnabled()) {
        request.rateMillihz = quint32(AppSettings::instance().subscribeRateOverrideHz()) * 1000U;
    }
    return request;
}

quint64 topicStatusKey(quint32 sourceId, quint16 topicId) {
    return (quint64(sourceId) << 16) | quint64(topicId);
}

// The dongle's peer-list topic. Matched by NAME; the field names inside it
// are HubPeerAccumulator's business (devices/hubpeeraccumulator.h).
constexpr char kHubPeersTopicName[] = "hub.peers";

// The dongle caps hub.peers at 2 Hz (DonglePublisher.cpp's
// kPeersMaxRateMillihz) and DevicesGrid re-polls the open dialog at 1 Hz, so
// asking for more would only be clamped away. As with every SUBSCRIBE this
// is a request: the source answers with the effective rate.
constexpr quint32 kHubPeersRequestedRateMillihz = 2000;  // 2 Hz

// QAction::data() round-trip for the screen-size menu (see buildMenus()):
// validates the stored int is actually one of the enum's values before the
// cast, instead of casting blindly. Out-of-range (shouldn't normally happen
// -- only every DashboardBreakpoint value is ever stored there) falls back
// to Large, the same default breakpointFromString() uses for unrecognized
// input.
DashboardBreakpoint breakpointFromActionData(int value) {
    if (value < 0 || value >= kDashboardBreakpointCount) {
        return DashboardBreakpoint::Large;
    }
    return DashboardBreakpoint(value);
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("TraceView v%1").arg(kVersion));
    resize(1024, 640);

    // Diagnostics buffers first: buildMenus()/buildRibbon() below wire actions
    // that open views onto these, and every DeviceConnection created later
    // feeds them.
    m_notificationLog = new NotificationLog(this);
    m_frameLog = new FrameLog(this);

    // Self-update (see lib/updater). Wired once here regardless of whether
    // Settings ▸ Updates has ever been opened -- the startup check below
    // needs both to exist independently of that tab's lifecycle.
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::upToDate, this, &MainWindow::onUpdateUpToDate);
    connect(m_updateChecker, &UpdateChecker::checkFailed, this, &MainWindow::onUpdateCheckFailed);
    m_updateDownloader = new UpdateDownloader(this);
    connect(m_updateDownloader, &UpdateDownloader::finished, this,
            &MainWindow::onUpdateDownloadFinished);
    connect(m_updateDownloader, &UpdateDownloader::failed, this,
            &MainWindow::onUpdateDownloadFailed);
#if !defined(TRACEVIEW_FLATPAK_BUILD)
    QTimer::singleShot(5000, this, &MainWindow::maybeCheckForUpdatesOnStartup);
#endif

    // m_chromeTopBar/m_statusRow exist before buildMenus()/buildRibbon() run
    // because both of those still build widgets (m_optionsButton,
    // m_screenSizeButton, m_fullscreenButton) that get appended into one or the other the moment each is
    // constructed -- the same incremental-assembly style the old
    // statusBar()->addWidget() call sites used. QWidget(this) here is just a
    // temporary owner; both get reparented into m_appShell's layout once
    // that's assembled further down (see this constructor's tail).
    m_chromeTopBar = new QWidget(this);
    auto* chromeTopBarLayout = new QHBoxLayout(m_chromeTopBar);
    chromeTopBarLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                           kRibbonPageMarginV);
    chromeTopBarLayout->setSpacing(kRibbonGroupSpacing);
    // Keep the compact options menu against the left edge.
    chromeTopBarLayout->addStretch();

    m_statusRow = new QWidget(this);
    // Matched by lib/theme/stylesheet.cpp's own QStatusBar rule (shared via
    // a combined selector) so this looks exactly like the native status bar
    // it replaced -- same background/border-top/text color.
    m_statusRow->setObjectName("statusRow");
    auto* statusRowLayout = new QHBoxLayout(m_statusRow);
    statusRowLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                        kRibbonPageMarginV);
    statusRowLayout->setSpacing(kRibbonGroupSpacing);
    // Stands in for QStatusBar's own transient message area AND this row's
    // stretch at once (see showStatusMessage()'s comment in mainwindow.h) --
    // added first so it already sits between wherever the "normal" widgets
    // (inserted to its left, by index, below) and the "permanent" ones
    // (appended to its right, further down) end up.
    m_statusMessageLabel = new QLabel(m_statusRow);
    m_statusMessageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    statusRowLayout->addWidget(m_statusMessageLabel, /*stretch=*/1);
    m_statusMessageTimer = new QTimer(this);
    m_statusMessageTimer->setSingleShot(true);
    connect(m_statusMessageTimer, &QTimer::timeout, m_statusMessageLabel, &QLabel::clear);

    buildMenus();

    m_dashboardGrid = new DashboardGrid(this);
    // The Settings page itself is built on demand (onOpenSettingsTab), same as
    // the OTA / BTP monitor tabs -- but the recent-files cap it exposes lives
    // in QSettings and has to stay trimmed whether or not that tab is open.
    connect(&AppSettings::instance(), &AppSettings::generalPreferencesChanged, this, [this] {
        QSettings settings;
        QStringList files = settings.value(kRecentFilesSettingsKey).toStringList();
        while (files.size() > AppSettings::instance().recentProjectsLimit()) {
            files.removeLast();
        }
        settings.setValue(kRecentFilesSettingsKey, files);
        updateRecentFilesMenu();
    });

    // Wires control-widget commands and the serial monitor/terminal to
    // whichever device each widget's own config currently targets -- see
    // core/serialwidgetbridge.h. Resolved lazily via m_deviceConnections
    // (empty right now; devices are added later via the Devices tab), so
    // construction order relative to onDeviceAdded() doesn't matter.
    m_serialWidgetBridge = new SerialWidgetBridge(
        m_dashboardGrid, [this](const QString& id) { return m_deviceConnections.value(id); }, this);

    Ribbon* ribbon = buildRibbon();
    buildLayersPanel();
    buildPropertiesPanel();

    // Canvas fills the whole row below the ribbon; the layers/properties
    // panels float on top of it instead of sharing the row via layout, so
    // docking one never shrinks the canvas -- and, since DashboardGrid
    // stores every item's position/size as a fraction of the canvas area
    // (see dashboardgrid.h), a shrink would otherwise reflow/resize every
    // widget on the dashboard along with it. Positioned directly by
    // m_dockController (see paneldockcontroller.h) rather than a
    // QDockWidget, which always reserves real layout space for a docked
    // widget and spans the full window height (menu bar to status bar),
    // not just the canvas.
    m_contentRow = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(m_contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // The Devices tab swaps this whole area for m_devicesGrid instead of
    // sharing the row with the canvas -- unlike the layers/properties
    // panels above, DevicesGrid isn't subject to DashboardGrid's
    // fraction-of-canvas geometry, so a plain QStackedWidget (real layout
    // space, not a float) is fine here.
    m_devicesGrid = new DevicesGrid(this);
    m_undoGroup->addStack(m_devicesGrid->undoStack());
    connect(m_removeDeviceAction, &QAction::triggered, this, &MainWindow::onRemoveDeviceRequested);
    connect(m_devicesGrid, &DevicesGrid::selectionChanged, this,
            &MainWindow::updateDeviceSelectionActions);
    // Keeps m_deviceConnections (one DeviceConnection per Device -- see
    // core/deviceconnection.h) in lockstep with m_devicesGrid's own list.
    connect(m_devicesGrid, &DevicesGrid::deviceAdded, this, &MainWindow::onDeviceAdded);
    connect(m_devicesGrid, &DevicesGrid::deviceRemoved, this, &MainWindow::onDeviceRemoved);
    // Deleting a hub that other devices ride is refused rather than cascaded
    // (see DevicesGrid::removeDevice). Say which devices depend on it: "it
    // has children" leaves the person hunting for them, and the whole reason
    // not to cascade is that they should choose what happens to each.
    connect(m_devicesGrid, &DevicesGrid::removeBlockedByChildren, this,
            [this](const QString&, const QStringList& childNames) {
                postStatus(
                    tr("This device carries %n other device(s) (%1). Remove or repoint them first.",
                       nullptr, int(childNames.size()))
                        .arg(childNames.join(tr(", "))),
                    8000, StatusSeverity::Warning);
            });
    connect(m_devicesGrid, &DevicesGrid::deviceUpdated, this, &MainWindow::onDeviceUpdated);
    connect(m_devicesGrid, &DevicesGrid::connectToggleRequested, this,
            &MainWindow::onDeviceConnectToggleRequested);
    connect(m_devicesGrid, &DevicesGrid::scriptRequested, this,
            &MainWindow::onDeviceScriptRequested);
    // DevicesGrid can't enumerate ports itself (traceview_devices doesn't
    // depend on QSerialPort, see lib/CMakeLists.txt) -- MainWindow supplies
    // the live list DeviceConfigDialog's port combo offers.
#ifdef TRACEVIEW_ENABLE_SERIAL
    m_devicesGrid->setPortListProvider([]() -> QStringList {
        QStringList names;
        const QList<QSerialPortInfo> infos = QSerialPortInfo::availablePorts();
        names.reserve(infos.size());
        for (const QSerialPortInfo& info : infos) {
            names.append(info.portName());
        }
        return names;
    });
#endif
    // Same reasoning as setPortListProvider() above, for the USB device
    // combo -- DevicesGrid can't enumerate HID devices itself
    // (traceview_devices doesn't depend on hidapi, see lib/CMakeLists.txt).
#ifdef TRACEVIEW_ENABLE_USB_HID
    m_devicesGrid->setUsbDeviceListProvider([]() -> QVector<UsbDeviceOption> {
        QVector<UsbDeviceOption> options;
        const QVector<UsbHidManager::DeviceInfo> devices = UsbHidManager::availableDevices();
        options.reserve(devices.size());
        for (const UsbHidManager::DeviceInfo& device : devices) {
            options.append({device.path, device.label});
        }
        return options;
    });
#endif
#ifdef TRACEVIEW_ENABLE_BLE
    // Same reasoning as setUsbDeviceListProvider() above: DevicesGrid can't
    // scan for BLE peripherals itself (traceview_devices doesn't depend on
    // Qt6::Bluetooth, see lib/CMakeLists.txt). Unlike the two synchronous
    // OS-query providers above, this is push/toggle-based (see
    // MainWindow::onBleScanToggled()) -- a scan runs for as long as the
    // dialog leaves it on, not a one-shot list.
    m_devicesGrid->setBleScanToggleHandler([this](bool start) { onBleScanToggled(start); });
    m_devicesGrid->setBleDeviceListProvider([this]() { return m_bleDiscoveredDevices; });
#endif
    // Same reasoning as setPortListProvider() above: DevicesGrid can't reach
    // a Backend itself (traceview_devices doesn't depend on
    // traceview_protocol), so MainWindow supplies the gear icon's "Reported
    // catalog" list from whichever DeviceConnection is live for that id.
    m_devicesGrid->setTopicCatalogProvider(
        [this](const QString& deviceId) -> QVector<CatalogTopicInfo> {
            DeviceConnection* connection = m_deviceConnections.value(deviceId);
            return connection ? connection->backend()->catalogTopics()
                              : QVector<CatalogTopicInfo>();
        });
    // The gear icon's "Robot source_id" combo. Unlike the three providers
    // above this one is polled for as long as the dialog stays open (see
    // DevicesGrid::handleConfigRequested) -- peers appear, go offline and
    // age continuously over the hub's own telemetry, so a one-shot fetch
    // would show a snapshot that is stale by the time it is read.
    m_devicesGrid->setHubPeerListProvider(
        [this](const QString& parentDeviceId) { return hubPeersFor(parentDeviceId); });
    refreshDeviceStatusLabel();  // starts empty ("No devices configured")
    refreshPropertiesPanelDevices();

    // m_dashboardScrollArea now wraps m_dashboardGrid directly -- the device
    // frame moved up to emolder the WHOLE app (m_appShell below), not just
    // this canvas (see m_devicePreviewFrame's own comment in mainwindow.h).
    // widgetResizable keeps the grid's width following the viewport (no
    // horizontal scrollbar is ever wanted here) while letting the grid's own
    // minimumHeight (see applyBreakpointViewport()'s canvas-height-multiplier
    // handling) exceed the viewport's -- so a Small/Medium canvas grown past
    // its device's own screen scrolls INSIDE that screen, the way scrolling
    // a too-tall page works on an actual phone, instead of the device itself
    // growing (that's now fixed size -- see DevicePreviewFrame).
    m_dashboardScrollArea = new QScrollArea(m_contentRow);
    m_dashboardScrollArea->setWidget(m_dashboardGrid);
    m_dashboardScrollArea->setWidgetResizable(true);
    m_dashboardScrollArea->setFrameShape(QFrame::NoFrame);
    m_dashboardScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // applyBreakpointViewport()'s canvas-height-multiplier math reads this
    // viewport's own height -- re-run it whenever that height changes for
    // ANY reason (not just a breakpoint change): a window resize, the device
    // frame clamping to a different available area, etc. (see eventFilter()).
    m_dashboardScrollArea->viewport()->installEventFilter(this);
    connect(m_dashboardGrid, &DashboardGrid::breakpointChanged, this,
            &MainWindow::syncBreakpointChrome);
    connect(m_dashboardGrid, &DashboardGrid::breakpointChanged, this,
            &MainWindow::applyBreakpointViewport);

    m_contentStack = new QStackedWidget(m_contentRow);
    m_contentStack->addWidget(m_dashboardScrollArea);
    m_contentStack->addWidget(m_devicesGrid);
    m_subscriptionsTable = new QTreeWidget(m_contentStack);
    m_subscriptionsTable->setObjectName("subscriptionsWorkspace");
    m_subscriptionsTable->setRootIsDecorated(false);
    m_subscriptionsTable->setAlternatingRowColors(false);
    // A single column: every row (heading, subscription, empty state) is
    // one spanned, centered string -- see refreshSubscriptionsTable(). A
    // second column existed only for the old right-aligned-name/left-
    // aligned-rate layout, whose "meeting point" depended on Qt's Stretch
    // mode splitting two columns exactly 50/50 (not guaranteed) and, even
    // then, wasn't the same thing as the combined text actually being
    // centered. One column has no boundary to land off-true.
    m_subscriptionsTable->setColumnCount(1);
    m_subscriptionsTable->setHeaderHidden(true);
    m_subscriptionsTable->setFrameShape(QFrame::NoFrame);
    m_subscriptionsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_subscriptionsTable->setFocusPolicy(Qt::NoFocus);
    m_subscriptionsTable->setStyleSheet(
        "QTreeWidget#subscriptionsWorkspace { background: transparent; border: none; }"
        "QTreeWidget#subscriptionsWorkspace::item { padding: 8px 16px; border: none;"
        " background: transparent; }"
        "QTreeWidget#subscriptionsWorkspace::item:hover { background: transparent; }");
    m_contentStack->addWidget(m_subscriptionsTable);
    // Each log opened via onOpenLogFile() (File > Open Log Offline) gets its
    // own LogViewer, added here on demand -- see m_openLogTabs.
    contentLayout->addWidget(m_contentStack);

    // Reparented rather than added to contentLayout: their geometry is set
    // directly by m_dockController (wired below via an event filter on
    // m_contentRow) so they overlay the canvas instead of squeezing it, and
    // can be dragged by their header to any edge or off into a floating
    // window. The layers panel is 1/3 the width of the properties panel by
    // default, since it only needs to fit short layer names -- see each
    // panel's preferredThickness().
    m_layersPanel->setParent(m_contentRow);
    m_propertiesPanel->setParent(m_contentRow);
    m_contentRow->installEventFilter(this);

    m_dockController = new PanelDockController(m_contentRow, this, this);
    m_dockController->registerPanel(m_layersPanel, "layers", DockEdge::Left);
    m_dockController->registerPanel(m_propertiesPanel, "properties", DockEdge::Right);
    m_dockController->restoreState();
    connect(m_dockController, &PanelDockController::dragFinished, this,
            &MainWindow::updatePanelVisibility);

    // m_appShell holds everything the app actually shows -- m_chromeTopBar,
    // the ribbon, m_contentRow, m_statusRow, in that order -- the exact
    // stack central used to hold directly, plus the two new chrome rows.
    // m_devicePreviewFrame then frames THIS (see below), so a Developer-mode
    // Phone/Tablet preview shows the real mobile chrome (m_chromeTopBar
    // standing in for the hidden menuBar(), m_statusRow standing in for the
    // hidden native status bar -- see compactChromeActive()) instead of a
    // desktop window with pieces hidden. Margins/spacing are exactly what
    // central's own layout used to set directly on [ribbon, m_contentRow].
    m_appShell = new QWidget(this);
    auto* appShellLayout = new QVBoxLayout(m_appShell);
    appShellLayout->setContentsMargins(0, kRibbonTopMargin, 0, 0);
    appShellLayout->setSpacing(0);
    appShellLayout->addWidget(m_chromeTopBar);
    appShellLayout->addWidget(ribbon);
    appShellLayout->addWidget(m_contentRow, /*stretch=*/1);
    appShellLayout->addWidget(m_statusRow);

    // Frames m_appShell -- the WHOLE app -- inside a device-shaped viewport
    // for a manual Phone/Tablet preview, or lets it fill the window exactly
    // for Notebook/User mode (see applyBreakpointViewport()). Sits directly
    // under central's own layout now, in place of [ribbon, m_contentRow]
    // being added there directly.
    m_devicePreviewFrame = new DevicePreviewFrame(this);
    m_devicePreviewFrame->setContentWidget(m_appShell);
    // Embedded dialogs (see DialogPresenter) overlay m_appShell rather than
    // the whole window, so a Phone/Tablet preview shows them inside the
    // device frame; setEmbedded() follows compactChromeActive() in
    // updateChromeVisibility().
    DialogPresenter::setHost(m_appShell);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_devicePreviewFrame, /*stretch=*/1);
    setCentralWidget(central);

    // Ribbon::currentTabChanged is only connected inside buildRibbon(),
    // *after* its fixed tabs are added -- so the currentChanged(0) QTabBar
    // fires the moment the first tab is added (Dashboard) never reaches
    // onRibbonTabChanged(), and m_dashboardTabActive would otherwise stay
    // false (and editingActive() with it) until the user switches tabs away
    // and back at least once. Everything onRibbonTabChanged()/
    // updatePanelVisibility() touch (m_contentStack, m_dockController,
    // m_devicesGrid, m_layersPanel/m_propertiesPanel) exists by this point,
    // so drive it once here to pick up the actual initial tab.
    onRibbonTabChanged(ribbon->currentIndex());

    // Workspace navigation and manual preview size stay at the right of the footer.
    buildWorkspaceSwitcher();
    m_statusRow->layout()->addWidget(m_screenSizeButton);

    // The native QStatusBar this row replaced showed one of these in its
    // bottom-right corner by default (sizeGripEnabled() defaults to true,
    // and nothing here ever turned it off) -- added last so it lands at the
    // far right, past m_workspaceSwitcher, same corner it occupied before.
    m_statusRow->layout()->addWidget(new QSizeGrip(m_statusRow));

    // Created after everything else in m_appShell so it stacks above the
    // ribbon row and the dashboard it overhangs; embedded dialogs, created
    // later still, stack above it in turn. Kept flush with the corner by the
    // event filter below.
    m_brandCornerMark = new BrandCornerMark(m_appShell);
    m_brandCornerMark->setForegroundColor(ThemeManager::instance().currentTheme().textPrimary);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette& palette) {
                m_brandCornerMark->setForegroundColor(palette.textPrimary);
            });
    m_appShell->installEventFilter(this);
    // The ribbon's bottom edge moves when its tab row is shown/hidden.
    m_ribbon->installEventFilter(this);

    // The app always starts in User mode (UserModeManager never persists a
    // login across restarts) -- apply that once now so the Devices tab and
    // edit-mode lock are already hidden before the window is ever shown,
    // then react to any later login/logout the same way.
    connect(&UserModeManager::instance(), &UserModeManager::modeChanged, this,
            &MainWindow::applyUserMode);
    applyUserMode(UserModeManager::instance().mode());

    // Keeps every connected hub child's online/offline (and robot-reboot
    // detection) current off the dongle's hub.peers, independent of whether a
    // config dialog is open -- see reconcileHubChildPresence().
    m_hubPeerReconcileTimer = new QTimer(this);
    m_hubPeerReconcileTimer->setInterval(1000);
    connect(m_hubPeerReconcileTimer, &QTimer::timeout, this,
            &MainWindow::reconcileHubChildPresence);
    m_hubPeerReconcileTimer->start();
}

MainWindow::~MainWindow() {
    // The dashboard's widgets are deleted later, as QObject children, i.e.
    // after this class's own members are gone -- so their destroyed()
    // handlers (which drop a subscription reference, see
    // wireChartWidgetToTelemetry) must be taken down here, while
    // m_widgetSubscriptions still exists. Only signals *from* each widget are
    // disconnected; the fieldSample connections into them are unaffected.
    for (auto it = m_widgetSubscriptions.constBegin(); it != m_widgetSubscriptions.constEnd();
         ++it) {
        disconnect(it.key(), nullptr, this, nullptr);
    }
    m_widgetSubscriptions.clear();
}

void MainWindow::postStatus(const QString& text, int timeoutMs, StatusSeverity severity,
                            const QString& source) {
    showStatusMessage(text, timeoutMs);
    m_notificationLog->append({QDateTime::currentDateTime(), text, severity, source});

    // Every status-bar message funnels through here regardless of which
    // device/subsystem raised it (session established/failed, subscription
    // rejections, update results...) -- logging it here, once, captures all
    // of that for free instead of instrumenting each call site separately.
    const QString line = source.isEmpty() ? text : QStringLiteral("[%1] %2").arg(source, text);
    switch (severity) {
        case StatusSeverity::Info:
        case StatusSeverity::Success:
            qCInfo(lcApp).noquote() << line;
            break;
        case StatusSeverity::Warning:
            qCWarning(lcApp).noquote() << line;
            break;
        case StatusSeverity::Error:
            qCCritical(lcApp).noquote() << line;
            break;
    }
}

void MainWindow::showStatusMessage(const QString& text, int timeoutMs) {
    m_statusMessageLabel->setText(text);
    // Restart rather than a fresh QTimer::singleShot() each call: a bare
    // singleShot() from an earlier, still-pending call would otherwise fire
    // on its own original schedule and clear a message that replaced it in
    // the meantime -- stop()+start() here cancels that pending clear the
    // same way QStatusBar::showMessage() itself does internally.
    m_statusMessageTimer->stop();
    if (timeoutMs > 0) {
        m_statusMessageTimer->start(timeoutMs);
    }
}

void MainWindow::wireChartWidgetToTelemetry(DashboardWidget* widget) {
    if (!dynamic_cast<ChartWidgetBase*>(widget) && !dynamic_cast<DummyGaugeWidget*>(widget) &&
        !dynamic_cast<TextBoardWidget*>(widget)) {
        return;
    }

    // PASSO 5 (topico 17): closing a widget only drops its reference; an
    // unsubscribe is sent only when it was the last consumer of that topic
    // -- against whichever device's Backend it was actually registered
    // with, which is why WidgetSubscription remembers deviceId alongside
    // the handle now.
    connect(widget, &QObject::destroyed, this, [this, widget] {
        const WidgetSubscription sub = m_widgetSubscriptions.take(widget);
        if (DeviceConnection* connection = m_deviceConnections.value(sub.deviceId)) {
            connection->backend()->removeSubscriber(sub.handle);
        }
    });

    refreshWidgetSubscription(widget);
}

void MainWindow::refreshWidgetSubscription(DashboardWidget* widget) {
    const WidgetTopicRequest request = widgetTopicRequest(widget, m_dashboardGrid);
    WidgetSubscription& sub = m_widgetSubscriptions[widget];

    DeviceConnection* oldConnection = m_deviceConnections.value(sub.deviceId);
    DeviceConnection* newConnection = m_deviceConnections.value(request.deviceId);

    // A device change (or the old one going away) invalidates both the old
    // subscription handle (it belongs to a different Backend/
    // SubscriptionManager instance) and the fieldSample connection feeding
    // this widget.
    if (oldConnection && oldConnection != newConnection && sub.handle != 0) {
        oldConnection->backend()->removeSubscriber(sub.handle);
        disconnect(oldConnection->backend(), &Backend::fieldSample, widget, nullptr);
    }

    if (!newConnection) {
        sub.deviceId = request.deviceId;
        sub.handle = 0;
        return;
    }

    if (oldConnection != newConnection) {
        if (auto* chart = dynamic_cast<ChartWidgetBase*>(widget)) {
            connect(newConnection->backend(), &Backend::fieldSample, chart,
                    &ChartWidgetBase::onFieldSample);
        } else if (auto* gauge = dynamic_cast<DummyGaugeWidget*>(widget)) {
            connect(newConnection->backend(), &Backend::fieldSample, gauge,
                    &DummyGaugeWidget::onFieldSample);
        } else if (auto* board = dynamic_cast<TextBoardWidget*>(widget)) {
            connect(newConnection->backend(), &Backend::textSample, board,
                    &TextBoardWidget::onTextSample);
            connect(newConnection->backend(), &Backend::binarySample, board,
                    &TextBoardWidget::onBinarySample);
        }
    }

    // topico 17 PASSO 2: this widget is one *reference* to its topic, not a
    // subscription of its own -- Backend collapses however many widgets
    // read (source, topic) into a single subscription. Reusing the existing
    // handle only makes sense while staying on the same Backend instance.
    const quint64 handleToReuse = oldConnection == newConnection ? sub.handle : 0;
    sub.handle = newConnection->backend()->updateSubscriber(handleToReuse, request.sourceId,
                                                            request.topicId, request.rateMillihz);
    sub.deviceId = request.deviceId;
}

void MainWindow::refreshWidgetSubscriptions() {
    const QList<DashboardWidget*> widgets = m_widgetSubscriptions.keys();
    for (DashboardWidget* widget : widgets) {
        refreshWidgetSubscription(widget);
    }
    // A config edit can just as well have re-pointed a terminal widget at a
    // different device -- its inbound wiring needs the same kind of refresh
    // subscriptions just got, see SerialWidgetBridge::refreshTerminalWiring().
    if (m_serialWidgetBridge) {
        m_serialWidgetBridge->refreshTerminalWiring();
    }
}

void MainWindow::updateSubscriptionsWorkspace() {
    if (!m_subscriptionsTable) {
        return;
    }
    const int scrollPosition = m_subscriptionsTable->verticalScrollBar()->value();
    m_subscriptionsTable->setUpdatesEnabled(false);
    m_subscriptionsTable->clear();
    // Resolve names and metrics within each device: topic IDs can repeat
    // across devices and must never share lookup entries.
    for (const Device& device : m_devicesGrid->devices()) {
        DeviceConnection* connection = m_deviceConnections.value(device.id);
        if (!connection) {
            continue;
        }
        Backend* backend = connection->backend();
        QHash<quint64, QString> names;
        QHash<quint64, StatusTopicRecord> statuses;
        for (const auto& topic : backend->catalogTopics()) {
            names.insert(topicStatusKey(topic.sourceId, topic.topicId), topic.name);
        }
        for (const auto& status : backend->topicStatuses()) {
            statuses.insert(topicStatusKey(status.sourceId, status.topicId), status);
        }
        const auto subscriptions = backend->subscriptions();
        if (!subscriptions.isEmpty()) {
            auto* heading = new QTreeWidgetItem(m_subscriptionsTable, {device.name});
            heading->setFirstColumnSpanned(true);
            heading->setTextAlignment(0, Qt::AlignCenter);
            QFont headingFont = m_subscriptionsTable->font();
            headingFont.setBold(true);
            heading->setFont(0, headingFont);
            heading->setSizeHint(0, QSize(0, 56));
            heading->setToolTip(0, device.id);
        }
        for (const auto& state : subscriptions) {
            const auto key = topicStatusKey(state.sourceId, state.topicId);
            const QString raw = QString("0x%1/0x%2")
                                    .arg(state.sourceId, 8, 16, QChar('0'))
                                    .arg(state.topicId, 4, 16, QChar('0'));
            const QString granted = state.effectiveRateMillihz != 0
                                        ? formatRateMillihz(state.effectiveRateMillihz)
                                        : (state.awaitingResult ? tr("Pending") : tr("Not granted"));
            const auto status = statuses.constFind(key);
            // One spanned, centered column -- same pattern the heading and
            // empty-state rows already use -- rather than a right-aligned
            // name meeting a left-aligned rate across two Stretch columns.
            // That layout only looked centered when both columns landed at
            // exactly the same width, which Qt's Stretch mode doesn't
            // guarantee for two columns; worse, even at a true 50/50 split,
            // "meets in the middle" isn't "centered" once the name and the
            // rate text differ much in length, so the combined block still
            // read as pulled toward whichever side had more text. A single
            // centered string sidesteps both: there's no column boundary to
            // land off-true, and the block centers on its own actual width.
            //
            // Name and sample rate only, nothing else -- no raw source/topic
            // hex (falls back to a plain placeholder instead of `raw` when
            // the catalog hasn't named this topic, rather than surfacing the
            // ID codes in the row itself) and no "requested: ..." half,
            // which doubled the row's length for a number this compact view
            // isn't meant to carry. `raw` and the requested rate still live
            // in the tooltip (see `detail` below) for whoever needs them.
            auto* row = new QTreeWidgetItem(
                m_subscriptionsTable,
                {tr("%1: %2").arg(names.value(key, tr("Unknown topic")), granted)});
            row->setFirstColumnSpanned(true);
            row->setTextAlignment(0, Qt::AlignCenter);
            QString detail = tr("%1\nRequested: %2\nWidgets: %3")
                                 .arg(raw, formatRateMillihz(state.requestedRateMillihz))
                                 .arg(state.subscriberCount);
            if (status != statuses.constEnd()) {
                detail += tr("\nBytes: %1\nDrops: %2")
                              .arg(status->bytesTotal).arg(status->samplesDroppedTotal);
            }
            row->setToolTip(0, detail);
        }
    }
    if (m_subscriptionsTable->topLevelItemCount() == 0) {
        auto* empty = new QTreeWidgetItem(m_subscriptionsTable,
                                         {tr("No subscriptions")});
        empty->setFirstColumnSpanned(true);
        empty->setTextAlignment(0, Qt::AlignCenter);
        empty->setFlags(Qt::NoItemFlags);
    }
    m_subscriptionsTable->verticalScrollBar()->setValue(scrollPosition);
    m_subscriptionsTable->setUpdatesEnabled(true);
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu = fileMenu;

    auto* newAction = fileMenu->addAction(tr("&New Project"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewProject);

    auto* openAction = fileMenu->addAction(tr("&Open Project..."));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenProject);

    m_recentFilesMenu = fileMenu->addMenu(tr("Open &Recent"));
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction(tr("&Save Project"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);

    auto* saveAsAction = fileMenu->addAction(tr("Save Project &As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveProjectAs);

    fileMenu->addSeparator();

    m_openLogFileAction = new QAction(tr("Open &Log Offline..."), this);
    m_openLogFileAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(m_openLogFileAction, &QAction::triggered, this, &MainWindow::onOpenLogFile);
    fileMenu->addAction(m_openLogFileAction);

    m_openOtaTabAction = new QAction(tr("Upload &Firmware (OTA)..."), this);
    m_openOtaTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(m_openOtaTabAction, &QAction::triggered, this, &MainWindow::onOpenOtaTab);
    fileMenu->addAction(m_openOtaTabAction);

    m_openBtpMonitorAction = new QAction(tr("BTP Traffic &Monitor..."), this);
    m_openBtpMonitorAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(m_openBtpMonitorAction, &QAction::triggered, this, &MainWindow::onOpenBtpMonitor);
    fileMenu->addAction(m_openBtpMonitorAction);

    auto* viewMenu = menuBar()->addMenu(tr("&View"));

    auto* shortcutsAction = viewMenu->addAction(tr("&Keyboard Shortcuts..."));
    shortcutsAction->setShortcut(QKeySequence(Qt::Key_F1));
    connect(shortcutsAction, &QAction::triggered, this, &MainWindow::onShowKeyboardShortcuts);

    auto* logFolderAction = viewMenu->addAction(tr("Open &Log Folder"));
    connect(logFolderAction, &QAction::triggered, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(AppLog::logDirectory())); });

    auto* resetPanelsAction = viewMenu->addAction(tr("&Reset Panel Positions"));
    connect(resetPanelsAction, &QAction::triggered, this,
            [this] { m_dockController->resetToDefaults(); });

    viewMenu->addSeparator();

    auto* themeMenu = viewMenu->addMenu(tr("&Theme"));

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    const QString currentId = ThemeManager::instance().currentTheme().id;
    for (const ThemePalette& palette : ThemeManager::instance().availableThemes()) {
        auto* action = themeMenu->addAction(palette.displayName);
        action->setCheckable(true);
        action->setChecked(palette.id == currentId);
        action->setData(palette.id);
        group->addAction(action);

        connect(action, &QAction::triggered, this,
                [id = palette.id]() { ThemeManager::instance().setTheme(id); });
    }

    auto* fontMenu = viewMenu->addMenu(tr("&Font"));

    auto* fontGroup = new QActionGroup(this);
    fontGroup->setExclusive(true);

    const QString currentFontId = FontManager::instance().currentFont().id;
    for (const FontOption& font : FontManager::instance().availableFonts()) {
        auto* action = new FontMenuAction(font, this);
        action->setChecked(font.id == currentFontId);
        action->setData(font.id);
        fontMenu->addAction(action);
        fontGroup->addAction(action);

        connect(action, &QAction::triggered, this,
                [id = font.id]() { FontManager::instance().setFont(id); });
    }

    // Restart-to-apply: switching languages does not attempt to live-
    // retranslate every open widget (that spans the whole UI/dashboard
    // layer), so the choice is persisted and the app offers to relaunch.
    auto* languageMenu = viewMenu->addMenu(tr("&Language"));

    auto* languageGroup = new QActionGroup(this);
    languageGroup->setExclusive(true);

    const QString currentLanguageId = LanguageManager::instance().currentLanguage().id;
    for (const LanguageInfo& language : LanguageManager::instance().availableLanguages()) {
        auto* action = languageMenu->addAction(language.displayName);
        action->setCheckable(true);
        action->setChecked(language.id == currentLanguageId);
        action->setData(language.id);
        languageGroup->addAction(action);

        connect(action, &QAction::triggered, this, [this, id = language.id]() {
            if (id == LanguageManager::instance().currentLanguage().id) {
                return;
            }
            LanguageManager::instance().setLanguage(id);

            if (DialogPresenter::confirm(
                    this, tr("Restart Required"),
                    tr("The application needs to restart to apply the new language. Restart now?"),
                    tr("Restart Now"), tr("Later"))) {
                QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                        QCoreApplication::arguments().mid(1));
                QCoreApplication::quit();
            }
        });
    }

    m_accessMenu = menuBar()->addMenu(tr("&Access"));
    updateAccessMenu();

    // Top-level entry of its own (like About/Donate) rather than buried in
    // File. The menu-bar item is a separate shortcut-less action: a QMenuBar
    // renders a top-level action's shortcut next to its text ("Settings
    // Ctrl+,"). The Ctrl+, chord lives on m_openSettingsTabAction instead,
    // added to the window itself so it also works with the menu bar hidden
    // (compact chrome / fullscreen).
    auto* settingsMenuBarAction = menuBar()->addAction(tr("&Settings"));
    connect(settingsMenuBarAction, &QAction::triggered, this, &MainWindow::onOpenSettingsTab);

    m_openSettingsTabAction = new QAction(tr("&Settings"), this);
    m_openSettingsTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    connect(m_openSettingsTabAction, &QAction::triggered, this, &MainWindow::onOpenSettingsTab);
    addAction(m_openSettingsTabAction);

    // Same split as Settings above: a shortcut-less top-level menu-bar entry,
    // plus a window-level action carrying Ctrl+Shift+H so the chord still
    // works with the menu bar hidden.
    auto* notificationsMenuBarAction = menuBar()->addAction(tr("&Notifications"));
    connect(notificationsMenuBarAction, &QAction::triggered, this,
            &MainWindow::onShowNotificationHistory);

    auto* notificationHistoryShortcut = new QAction(this);
    notificationHistoryShortcut->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    connect(notificationHistoryShortcut, &QAction::triggered, this,
            &MainWindow::onShowNotificationHistory);
    addAction(notificationHistoryShortcut);

    auto* debugAction = menuBar()->addAction(tr("&Debug"));
    connect(debugAction, &QAction::triggered, this, &MainWindow::onDebug);
    debugAction->setVisible(false);

    auto* aboutAction = menuBar()->addAction(tr("&About"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);

    auto* donateAction = menuBar()->addAction(tr("Dona&te"));
    connect(donateAction, &QAction::triggered, this, &MainWindow::onDonate);

    // Android-safe stand-in for the menu bar above -- a QMenuBar's own
    // rendering on Android is unconfirmed (older Qt-for-Android versions
    // relocated it into the native, now-deprecated Android options-menu
    // instead of showing it as a normal widget row; whether that still
    // applies hasn't been tested on a device). Reuses the exact same File/
    // View/Access QMenu objects as submenus here -- QMenu::menuAction()'s
    // own docs call this out as the supported way to re-add a menu to
    // another widget -- so nothing here duplicates state: updateAccessMenu()'s
    // rebuilds show up in this copy for free, no separate upkeep. Lives in
    // m_chromeTopBar (see MainWindow::MainWindow()), not the status bar
    // anymore -- that puts it next to m_screenSizeButton, in the same row a
    // phone build's own chrome will actually use, rather than the bottom
    // status bar. Its visibility is decided by compactChromeActive() (see
    // updateChromeVisibility()), not set here: kUsesCompactChrome alone used
    // to be enough (this button existed only to stand in for a menu bar
    // Android might not render), but now a desktop Developer-mode Phone/
    // Tablet preview needs it too, for the same reason -- the preview must
    // show the real mobile chrome, not a desktop window with pieces hidden.
    m_optionsButton = new QToolButton(this);
    m_optionsButton->setObjectName("optionsButton");
    m_optionsButton->setAutoRaise(true);
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    m_optionsButton->setToolTip(tr("More options"));
    m_optionsButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_optionsButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    // Icon-only kebab-menu button -- the dot icon already reads as "this
    // opens a menu" (same as Android's own overflow icon), so the style's
    // usual drop-down arrow indicator next to it would be a redundant
    // second affordance saying the same thing; suppressed rather than
    // repositioned (contrast WorkspaceSwitcher's own corner-widget button,
    // which keeps the indicator because it's paired with visible text).
    m_optionsButton->setStyleSheet(
        "QToolButton#optionsButton::menu-indicator { image: none; width: 0px; }");

    auto* optionsMenu = new QMenu(m_optionsButton);
    optionsMenu->addAction(fileMenu->menuAction());
    optionsMenu->addAction(viewMenu->menuAction());
    optionsMenu->addAction(m_accessMenu->menuAction());
    optionsMenu->addAction(settingsMenuBarAction);
    optionsMenu->addAction(notificationsMenuBarAction);
    optionsMenu->addSeparator();
    optionsMenu->addAction(aboutAction);
    optionsMenu->addAction(donateAction);
    m_optionsButton->setMenu(optionsMenu);
    static_cast<QHBoxLayout*>(m_chromeTopBar->layout())->insertWidget(0, m_optionsButton);
}

void MainWindow::updateAccessMenu() {
    m_accessMenu->clear();

    UserModeManager& userMode = UserModeManager::instance();
    if (userMode.mode() == UserModeManager::UserMode::Developer) {
        QAction* whoAction =
            m_accessMenu->addAction(tr("Connected as: %1").arg(userMode.currentUserName()));
        whoAction->setEnabled(false);

        // Right after "Connected as", before the separator that groups the
        // actionable items below -- stays visible without pushing Manage
        // Users/Exit out of view, and reads as a status line alongside the
        // other one rather than an alert competing with them.
        if (userMode.usingDefaultPassword()) {
            QAction* defaultPasswordAction = m_accessMenu->addAction(
                tr("Using the default admin password -- change it in Manage Users"));
            defaultPasswordAction->setEnabled(false);
        }
        m_accessMenu->addSeparator();

        QAction* previewAction = m_accessMenu->addAction(tr("View as user"));
        previewAction->setCheckable(true);
        previewAction->setChecked(m_previewAsUser);
        connect(previewAction, &QAction::triggered, this, [this](bool checked) {
            // applyUserMode rebuilds this menu, so wait until the popup unwinds.
            QMetaObject::invokeMethod(this, [this, checked] {
                m_previewAsUser = checked;
                applyUserMode(UserModeManager::instance().mode());
            }, Qt::QueuedConnection);
        });

        QAction* manageAction = m_accessMenu->addAction(tr("&Manage Users..."));
        connect(manageAction, &QAction::triggered, this, [this] {
            ManageUsersDialog dialog(this);
            DialogPresenter::exec(dialog, DialogPresenter::Style::Page);
        });

        QAction* logoutAction = m_accessMenu->addAction(tr("&Exit Developer Mode"));
        // Deferred to the next event-loop turn rather than called straight
        // from here -- same hazard WorkspaceSwitcher::rebuildMenu() already
        // guards against (see its own comment): logout() emits
        // modeChanged() synchronously, which reaches back into this very
        // function via MainWindow::applyUserMode() -> updateAccessMenu() ->
        // m_accessMenu->clear(), deleting logoutAction while its own
        // triggered() handler is still on the call stack (this menu is also
        // a submenu of the "More options" button, so that stack can run
        // through its popup handling too). Scheduling the call lets that
        // frame unwind first.
        connect(logoutAction, &QAction::triggered, this, [this] {
            QMetaObject::invokeMethod(
                this, [] { UserModeManager::instance().logout(); }, Qt::QueuedConnection);
        });
    } else {
        QAction* loginAction = m_accessMenu->addAction(tr("&Enter Developer Mode..."));
        // Same deferral as logoutAction above: LoginDialog::exec() runs its
        // own nested event loop, and a successful login inside it calls
        // UserModeManager::login(), which would otherwise rebuild (and
        // delete) this menu while still inside loginAction's own
        // triggered() call.
        connect(loginAction, &QAction::triggered, this, [this] {
            QMetaObject::invokeMethod(
                this,
                [this] {
                    LoginDialog dialog(this);
                    DialogPresenter::exec(dialog, DialogPresenter::Style::Card);
                },
                Qt::QueuedConnection);
        });
    }
}

void MainWindow::applyUserMode(UserModeManager::UserMode mode) {
    if (mode != UserModeManager::UserMode::Developer) {
        m_previewAsUser = false;
    }
    const bool isDeveloper = developerUiActive();

    // Losing developer access while Devices is the current tab would
    // otherwise leave the ribbon pointed at a tab that's about to disappear
    // -- fall back to Dashboard first.
    if (!isDeveloper) {
        m_ribbon->setCurrentIndex(m_dashboardTabIndex);
        // Also return from offline content when Dashboard was already selected.
        onRibbonTabChanged(m_dashboardTabIndex);
        // Everything past the fixed Dashboard/Devices tabs (offline logs,
        // OTA, BTP Traffic, Settings) is closed outright rather than left
        // hidden behind the now-invisible tab bar -- "View as user" included.
        // Walked back to front: each close shifts the indices after it.
        for (int i = m_ribbon->count() - 1; i >= 0; --i) {
            if (i != m_dashboardTabIndex && i != m_devicesTabIndex) {
                onLogTabCloseRequested(i);
            }
        }
    }
    m_ribbon->setTabVisible(m_devicesTabIndex, isDeveloper);

    // File (projects, offline logs, OTA, BTP monitor) is Developer-only.
    // Hiding the menu action alone would leave its shortcuts live (the menu
    // bar is hidden in compact chrome and shortcuts fire anyway), so each
    // action is disabled too.
    m_fileMenu->menuAction()->setVisible(isDeveloper);
    for (QAction* action : m_fileMenu->actions()) {
        action->setEnabled(isDeveloper);
    }

    // Force the dashboard back to read-only before hiding the toggle that
    // controls it -- otherwise a grid left unlocked from a previous
    // developer session would sit there editable with no visible way to
    // relock it. setChecked(false) runs the same onEditModeToggled() cleanup
    // a manual click would.
    if (!isDeveloper && m_editModeButton->isChecked()) {
        m_editModeButton->setChecked(false);
    }
    m_editModeButton->setVisible(isDeveloper);
    m_screenSizeButton->setVisible(isDeveloper);
    // The Layers/Properties panels it shows/hides are only ever visible
    // while editing is enabled (see updatePanelVisibility()), which is
    // itself Developer-only -- so the toggle has nothing to control in User
    // mode either.
    m_togglePanelsButton->setVisible(isDeveloper);

    if (m_workspaceSwitcher) {
        m_workspaceSwitcher->setManagementEnabled(isDeveloper);
        m_workspaceDock->setManagementEnabled(isDeveloper);
    }

    updateAccessMenu();

    // No-op in Developer mode (see its own comment) -- in User mode, this is
    // what actually replaces whatever breakpoint was last selected manually
    // with the one this real screen calls for. There's no window/frame
    // state to restore on the way out of a Developer-mode preview anymore
    // (see docs/DASHBOARD.md's "Screen-size breakpoints" section) --
    // applyBreakpointViewport(), wired to DashboardGrid::breakpointChanged,
    // already collapses the frame back to none the moment setBreakpoint()
    // below actually changes anything.
    applyAutoBreakpoint();
    // Both of these run unconditionally, for the same reason: they are
    // otherwise only reached via DashboardGrid::breakpointChanged, which
    // setBreakpoint() suppresses when handed the breakpoint already active.
    // Leaving Developer mode while previewing Phone, on a window narrow
    // enough that auto-detection picks Small too, is exactly that case --
    // the breakpoint doesn't change, no signal fires, and without these the
    // device frame would stay up (and the canvas +/- buttons stay visible)
    // in User mode.
    applyBreakpointViewport();
    syncBreakpointChrome();
}

void MainWindow::applyAutoBreakpoint() {
    if (UserModeManager::instance().mode() != UserModeManager::UserMode::User) {
        return;
    }
    m_dashboardGrid->setBreakpoint(
        detectBreakpoint(m_dashboardGrid->currentBreakpoint(), kBreakpointHysteresisPx));
}

DashboardBreakpoint MainWindow::detectBreakpoint(DashboardBreakpoint current,
                                                 int hysteresisPx) const {
    // Before the window's first show(), m_dashboardScrollArea hasn't been
    // laid out yet and its viewport can report 0 width (or something else
    // meaningless) -- keep `current` instead of snapping to Small on nothing;
    // resizeEvent() re-runs auto-detection once real geometry exists.
    const int availableWidth = m_dashboardScrollArea->viewport()->width();
    if (availableWidth <= 0) {
        return current;
    }

    DashboardBreakpoint breakpoint = current;
    // Dead band (hysteresisPx) around each threshold, measured against
    // `current` -- see applyAutoBreakpoint()'s doc comment in mainwindow.h.
    // With a zero dead band this is a plain threshold lookup, whatever
    // `current` is. The three cases below chain rather than looking each
    // threshold up independently, so a resize that jumps clean across both
    // thresholds in one event (e.g. straight from Large to Small) still
    // lands on the right breakpoint instead of getting stuck one step short.
    switch (current) {
        case DashboardBreakpoint::Small:
            if (availableWidth > kSmallBreakpointMaxViewportWidth + hysteresisPx) {
                breakpoint = availableWidth > kMediumBreakpointMaxViewportWidth + hysteresisPx
                                 ? DashboardBreakpoint::Large
                                 : DashboardBreakpoint::Medium;
            }
            break;
        case DashboardBreakpoint::Medium:
            if (availableWidth < kSmallBreakpointMaxViewportWidth - hysteresisPx) {
                breakpoint = DashboardBreakpoint::Small;
            } else if (availableWidth > kMediumBreakpointMaxViewportWidth + hysteresisPx) {
                breakpoint = DashboardBreakpoint::Large;
            }
            break;
        case DashboardBreakpoint::Large:
            if (availableWidth < kMediumBreakpointMaxViewportWidth - hysteresisPx) {
                breakpoint = availableWidth < kSmallBreakpointMaxViewportWidth - hysteresisPx
                                 ? DashboardBreakpoint::Small
                                 : DashboardBreakpoint::Medium;
            }
            break;
    }
    return breakpoint;
}

void MainWindow::loadDashboardJson(const QJsonObject& json, DashboardLoadBreakpoint policy) {
    // The "breakpoint" a dashboard's JSON carries is whatever a developer
    // last had selected while editing that workspace -- never what's wanted
    // here. A freshly opened project starts on the breakpoint this device's
    // own screen calls for, in either mode; switching workspaces keeps
    // whatever was already showing, so a Developer-mode manual pick survives
    // until the screen-size button is clicked again or Developer mode is
    // left. User mode always tracks the real screen on top of either.
    //
    // The target is written into the JSON *before* fromJson() rather than
    // set afterwards: fromJson() emits breakpointChanged for whatever it
    // loads, so letting it apply the stored value first would briefly swap
    // the device frame and chrome to that breakpoint and back.
    const DashboardBreakpoint current = m_dashboardGrid->currentBreakpoint();
    DashboardBreakpoint target = policy == DashboardLoadBreakpoint::DeviceDefault
                                     ? detectBreakpoint(current, 0)
                                     : current;
    if (UserModeManager::instance().mode() == UserModeManager::UserMode::User) {
        target = detectBreakpoint(target, kBreakpointHysteresisPx);
    }
    QJsonObject withBreakpoint = json;
    withBreakpoint["breakpoint"] = breakpointToString(target);
    m_dashboardGrid->fromJson(withBreakpoint);
}

Ribbon* MainWindow::buildRibbon() {
    m_addWidgetAction = new QAction(tr("Add"), this);
    m_addWidgetAction->setEnabled(false);
    m_addWidgetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(m_addWidgetAction, &QAction::triggered, this, &MainWindow::onAddWidget);

    m_removeAction = new QAction(tr("Remove"), this);
    m_removeAction->setEnabled(false);
    m_removeAction->setShortcut(QKeySequence::Delete);
    connect(m_removeAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::removeSelected);

    m_copyAction = new QAction(tr("Copy"), this);
    m_copyAction->setEnabled(false);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::copySelected);

    m_pasteAction = new QAction(tr("Paste"), this);
    m_pasteAction->setEnabled(false);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::pasteItem);

    // Z-order shortcuts match the de-facto standard from image editors
    // (Photoshop/Illustrator/Figma): Ctrl+] / Ctrl+[ nudge one step,
    // Ctrl+Shift+] / Ctrl+Shift+[ jump to the extremes. Only enabled on the
    // Layout tab with a selection (updateSelectionActions), so they never
    // collide with the terminal in Run.
    m_bringToFrontAction = new QAction(tr("To Front"), this);
    m_bringToFrontAction->setEnabled(false);
    m_bringToFrontAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight));
    connect(m_bringToFrontAction, &QAction::triggered, m_dashboardGrid,
            &DashboardGrid::bringSelectedToFront);

    m_bringForwardAction = new QAction(tr("Forward"), this);
    m_bringForwardAction->setEnabled(false);
    m_bringForwardAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketRight));
    connect(m_bringForwardAction, &QAction::triggered, m_dashboardGrid,
            &DashboardGrid::bringSelectedForward);

    m_sendBackwardAction = new QAction(tr("Backward"), this);
    m_sendBackwardAction->setEnabled(false);
    m_sendBackwardAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
    connect(m_sendBackwardAction, &QAction::triggered, m_dashboardGrid,
            &DashboardGrid::sendSelectedBackward);

    m_sendToBackAction = new QAction(tr("To Back"), this);
    m_sendToBackAction->setEnabled(false);
    m_sendToBackAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft));
    connect(m_sendToBackAction, &QAction::triggered, m_dashboardGrid,
            &DashboardGrid::sendSelectedToBack);

    m_groupAction = new QAction(tr("Group"), this);
    m_groupAction->setEnabled(false);
    m_groupAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));
    connect(m_groupAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::groupSelected);

    m_ungroupAction = new QAction(tr("Ungroup"), this);
    m_ungroupAction->setEnabled(false);
    m_ungroupAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    connect(m_ungroupAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::ungroupSelected);

    // Two independent QUndoStacks (dashboard widgets, devices) share one
    // Undo/Redo pair via QUndoGroup: m_undoGroup tracks which stack is
    // "active" (flipped in onRibbonTabChanged() to match the visible tab),
    // and createUndoAction()/createRedoAction() on the *group* wire up
    // triggered/enabled state (and a dynamic "Undo <command text>" label)
    // from whichever stack that is -- no manual canUndo()/canRedo() syncing,
    // and Ctrl+Z always acts on what's actually on screen instead of always
    // hitting the dashboard regardless of tab. m_devicesGrid doesn't exist
    // yet at this point (built after buildRibbon() returns, see the
    // constructor) -- its stack joins the group there, same reasoning as
    // m_removeDeviceAction's own connect() right after that construction.
    m_undoGroup = new QUndoGroup(this);
    m_undoGroup->addStack(m_dashboardGrid->undoStack());
    m_undoAction = m_undoGroup->createUndoAction(this, tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_undoGroup->createRedoAction(this, tr("Redo"));
    // QKeySequence::Redo is Ctrl+Y on Windows; also accept the Ctrl+Shift+Z
    // that editor users reach for.
    m_redoAction->setShortcuts(
        {QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});

    connect(m_dashboardGrid, &DashboardGrid::selectionChanged, this,
            &MainWindow::onSelectionChanged);
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this,
            &MainWindow::refreshPropertiesPanel);
    // The layers panel's row list needs to resync on anything that could add/
    // remove/rename/reorder an item -- itemsChanged() covers add/remove/type-
    // change/load, indexChanged() covers everything else that goes through
    // the undo stack (rename, z-order, and their own undo/redo), same
    // reasoning as the refreshPropertiesPanel hook right above.
    connect(m_dashboardGrid, &DashboardGrid::itemsChanged, this, &MainWindow::refreshLayersPanel);
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this,
            &MainWindow::refreshLayersPanel);
    // A config edit (or its undo/redo) can repoint a widget at another
    // source/topic or change its sample time, which changes what this client
    // must have subscribed -- every path that edits a config goes through the
    // undo stack, so this one hook covers them all (topico 17 PASSO 2/5).
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this,
            &MainWindow::refreshWidgetSubscriptions);
    // Toggling or changing Settings > Dashboard's subscribe rate override
    // needs every already-open widget resubscribed at the new rate, not just
    // widgets created afterward.
    connect(&AppSettings::instance(), &AppSettings::dashboardPreferencesChanged, this,
            &MainWindow::refreshWidgetSubscriptions);
    // Chart/gauge widgets subscribe to live telemetry the moment they're
    // created -- topico 15's "varios assinantes por campo" wiring, covering
    // both a fresh Add Widget and a project load (DashboardGrid::createCell
    // is the single factory path for both, see dashboardgrid.cpp).
    connect(m_dashboardGrid, &DashboardGrid::widgetCreated, this,
            &MainWindow::wireChartWidgetToTelemetry);
    // The clipboard can change from a copySelected() call here, or from
    // another window/app entirely — either way, m_pasteAction's enabled
    // state needs to stay in sync with whether it's currently pasteable.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this,
            &MainWindow::updateSelectionActions);

    m_addDeviceAction = new QAction(tr("Add Device"), this);
    m_addDeviceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    connect(m_addDeviceAction, &QAction::triggered, this, &MainWindow::onAddDevice);

    // Wired to m_devicesGrid once it exists (right after its own
    // construction in MainWindow::MainWindow()) -- it isn't built yet at
    // this point, since buildRibbon() runs before it, same reason
    // m_removeAction above is wired straight to m_dashboardGrid but this one
    // can't be wired to m_devicesGrid here.
    m_removeDeviceAction = new QAction(tr("Remove Device"), this);
    m_removeDeviceAction->setEnabled(false);
    m_removeDeviceAction->setShortcut(QKeySequence::Delete);

    auto* dashboardPage = new QWidget(this);
    dashboardPage->setObjectName("ribbonPage");
    dashboardPage->setFixedHeight(kRibbonPageHeight);
    auto* dashboardLayout = new QHBoxLayout(dashboardPage);
    dashboardLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                        kRibbonPageMarginV);
    dashboardLayout->setSpacing(kRibbonGroupSpacing);

    // Port/baud/connect used to live here as one global bar (see git history
    // pre-multi-device-refactor) -- each device now owns its own connection
    // config, set in the Devices tab (DeviceConfigDialog). This read-only
    // strip is what's left for the Dashboard tab: an at-a-glance glance at
    // every configured device's live connection state, for when you're
    // looking at the dashboard and not at any cell bound to a disconnected
    // device.
    m_deviceStatusLabel = new QLabel(dashboardPage);
    m_deviceStatusLabel->setObjectName("deviceStatusLabel");
    m_deviceStatusLabel->setTextFormat(Qt::RichText);

    // Top-right, left of the lock: shows/hides m_layersPanel/m_propertiesPanel
    // on demand. Needed because updatePanelVisibility()'s usual "only while
    // something is selected or pinned" gating would otherwise leave both
    // panels -- Add Widget included, now that it lives in the Layers panel --
    // unreachable on an empty canvas. See onTogglePanelsClicked().
    m_togglePanelsButton = new QToolButton(dashboardPage);
    m_togglePanelsButton->setCheckable(true);
    m_togglePanelsButton->setChecked(m_panelsVisible);
    m_togglePanelsButton->setAutoRaise(true);
    m_togglePanelsButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_togglePanelsButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    connect(m_togglePanelsButton, &QToolButton::toggled, this, &MainWindow::onTogglePanelsClicked);

    // Screen-size breakpoint toggle -- Developer-mode-only (hidden/shown by
    // applyUserMode(), same as the lock below): lets a developer preview and
    // arrange each of the three per-screen-size layouts (see
    // dashboarditem.h) independently. In User mode the breakpoint instead
    // follows the real screen automatically (see applyAutoBreakpoint()) and
    // this button plays no part. Lives at the right of the status row and
    // remains available during View as user without ending the developer session.
    m_screenSizeButton = new QToolButton(this);
    m_screenSizeButton->setObjectName("screenSizeButton");
    m_screenSizeButton->setAutoRaise(true);
    m_screenSizeButton->setPopupMode(QToolButton::InstantPopup);
    m_screenSizeButton->setStyleSheet(
        "QToolButton#screenSizeButton::menu-indicator { image: none; width: 0px; }");
    m_screenSizeButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_screenSizeButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));

    m_screenSizeMenu = new QMenu(m_screenSizeButton);
    auto* screenSizeGroup = new QActionGroup(this);
    screenSizeGroup->setExclusive(true);
    const QVector<QPair<DashboardBreakpoint, QString>> screenSizeOptions = {
        {DashboardBreakpoint::Small, tr("Small")},
        {DashboardBreakpoint::Medium, tr("Medium")},
        {DashboardBreakpoint::Large, tr("Large")},
    };
    for (const auto& option : screenSizeOptions) {
        auto* action = m_screenSizeMenu->addAction(option.second);
        action->setCheckable(true);
        action->setData(int(option.first));
        screenSizeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, breakpoint = option.first]() {
            onScreenSizeBreakpointSelected(breakpoint);
        });
    }

    m_screenSizeButton->setMenu(m_screenSizeMenu);

    // Canvas height +/- -- Small/Medium only (hidden for Notebook, see
    // updateCanvasHeightButtons()): grows/shrinks that breakpoint's canvas
    // past the device viewport's own height one step at a time
    // (DashboardGrid::growCanvasHeight()/shrinkCanvasHeight()), for an
    // arrangement that needs more room than even the phone/tablet preview
    // frame (DevicePreviewFrame, applied via applyBreakpointViewport())
    // gives it. Neither button has a DashboardGrid signal to react to, so
    // each click also re-runs applyBreakpointViewport() itself, right here,
    // to resize the frame -- MainWindow's QScrollArea then shows a
    // scrollbar once the frame is actually taller than the viewport.
    m_canvasShrinkButton = new QToolButton(dashboardPage);
    m_canvasShrinkButton->setAutoRaise(true);
    m_canvasShrinkButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_canvasShrinkButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_canvasShrinkButton->setToolTip(tr("Shrink the canvas"));
    connect(m_canvasShrinkButton, &QToolButton::clicked, this, [this]() {
        m_dashboardGrid->shrinkCanvasHeight();
        applyBreakpointViewport();
    });

    m_canvasGrowButton = new QToolButton(dashboardPage);
    m_canvasGrowButton->setAutoRaise(true);
    m_canvasGrowButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_canvasGrowButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_canvasGrowButton->setToolTip(tr("Grow the canvas"));
    connect(m_canvasGrowButton, &QToolButton::clicked, this, [this]() {
        m_dashboardGrid->growCanvasHeight();
        applyBreakpointViewport();
    });

    // Top-right lock toggle: turns the Dashboard tab's canvas into the old
    // Layout tab's editable mode in place, instead of that being a separate
    // tab. See onEditModeToggled() for what flipping it actually does.
    m_editModeButton = new QToolButton(dashboardPage);
    m_editModeButton->setCheckable(true);
    m_editModeButton->setAutoRaise(true);
    m_editModeButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_editModeButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    connect(m_editModeButton, &QToolButton::toggled, this, &MainWindow::onEditModeToggled);

    // Lives in m_statusRow (bottom-left, see below) rather than on this
    // page: the ribbon's tab strip hides during fullscreen
    // (onFullscreenToggled -- m_ribbon->setTabBarVisible(false)), which
    // used to be harmless for this button since the Run page itself stayed
    // visible, but placing the workspace switcher in the ribbon's tab row
    // (topico's earlier design) broke under that same hide. m_statusRow is
    // never touched by fullscreen, so anything anchored there survives it
    // for free.
    m_fullscreenButton = new QToolButton(this);
    m_fullscreenButton->setCheckable(true);
    m_fullscreenButton->setAutoRaise(true);
    m_fullscreenButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_fullscreenButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_fullscreenButton->setToolTip(tr("Fullscreen dashboard (F11)"));
    connect(m_fullscreenButton, &QToolButton::toggled, this, &MainWindow::onFullscreenToggled);
    // Inserted just left of m_statusMessageLabel (rather than appended, which
    // would land it in the permanent group on the right) -- see this
    // constructor's own comment on m_statusRow for why indexOf() rather than
    // a fixed index.
    {
        auto* statusRowLayout = qobject_cast<QHBoxLayout*>(m_statusRow->layout());
        statusRowLayout->insertWidget(statusRowLayout->indexOf(m_statusMessageLabel),
                                      m_fullscreenButton);
    }

    // Window-level shortcuts (not menu items) so they keep working once the
    // menu bar is hidden while fullscreen (see onFullscreenToggled). Routed
    // through the button itself rather than duplicating onFullscreenToggled's
    // logic here.
    auto* fullscreenAction = new QAction(this);
    fullscreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    addAction(fullscreenAction);
    connect(fullscreenAction, &QAction::triggered, m_fullscreenButton, &QToolButton::toggle);

    auto* exitFullscreenAction = new QAction(this);
    exitFullscreenAction->setShortcut(QKeySequence(Qt::Key_Escape));
    addAction(exitFullscreenAction);
    connect(exitFullscreenAction, &QAction::triggered, this, [this]() {
        if (m_fullscreenButton->isChecked()) {
            m_fullscreenButton->setChecked(false);
        }
    });

    // Window-level, same reasoning as fullscreen/exitFullscreen above --
    // browser/editor-style tab cycling (Ctrl+Tab forward, Ctrl+Shift+Tab
    // back) through WorkspaceManager's own list order. m_workspaceSwitcher
    // doesn't exist yet at this point (built last in the constructor) but
    // these only fire on a later keypress, well after construction finishes.
    auto* nextWorkspaceAction = new QAction(this);
    nextWorkspaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab));
    addAction(nextWorkspaceAction);
    connect(nextWorkspaceAction, &QAction::triggered, this, [this]() { cycleWorkspace(1); });

    auto* previousWorkspaceAction = new QAction(this);
    previousWorkspaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab));
    addAction(previousWorkspaceAction);
    connect(previousWorkspaceAction, &QAction::triggered, this, [this]() { cycleWorkspace(-1); });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    dashboardLayout->addWidget(m_deviceStatusLabel);
    dashboardLayout->addStretch();
    dashboardLayout->addWidget(m_togglePanelsButton);
    dashboardLayout->addWidget(m_canvasShrinkButton);
    dashboardLayout->addWidget(m_canvasGrowButton);
    dashboardLayout->addWidget(m_editModeButton);

    // Initial refreshDeviceStatusLabel() call happens once m_devicesGrid
    // exists (buildRibbon() runs before it -- see the constructor); this
    // just keeps it live across a later theme change (dot colors).
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { refreshDeviceStatusLabel(); });

    auto* devicesPage = new QWidget(this);
    devicesPage->setObjectName("ribbonPage");
    devicesPage->setFixedHeight(kRibbonPageHeight);
    auto* devicesLayout = new QHBoxLayout(devicesPage);
    devicesLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                      kRibbonPageMarginV);
    devicesLayout->setSpacing(kRibbonGroupSpacing);

    devicesLayout->addWidget(
        Ribbon::createButtonGroup(devicesPage, {m_addDeviceAction, m_removeDeviceAction}));
    devicesLayout->addStretch();

    auto* ribbon = new Ribbon(this);
    m_dashboardTabIndex = ribbon->addTab(tr("Dashboard"), dashboardPage);
    m_devicesTabIndex = ribbon->addTab(tr("Devices"), devicesPage);

    connect(ribbon, &Ribbon::currentTabChanged, this, &MainWindow::onRibbonTabChanged);
    connect(ribbon, &Ribbon::tabCloseRequested, this, &MainWindow::onLogTabCloseRequested);

    m_ribbon = ribbon;

    // Icon refresh also synchronizes chrome visibility, so the ribbon must exist.
    updateRibbonIcons();

    // Ctrl+1/2 jump to the fixed Dashboard/Devices tabs -- window-level so
    // they keep working with the menu bar hidden (same as fullscreen above).
    // Ctrl+Tab is already taken for workspace cycling, so digits it is.
    const QVector<QKeyCombination> fixedTabChords = {Qt::CTRL | Qt::Key_1, Qt::CTRL | Qt::Key_2};
    const QVector<int> fixedTabIndices = {m_dashboardTabIndex, m_devicesTabIndex};
    for (int i = 0; i < fixedTabChords.size(); ++i) {
        auto* action = new QAction(this);
        action->setShortcut(QKeySequence(fixedTabChords[i]));
        addAction(action);
        const int tabIndex = fixedTabIndices[i];
        connect(action, &QAction::triggered, this, [this, tabIndex]() {
            if (developerUiActive() || tabIndex == m_dashboardTabIndex) {
                m_ribbon->setCurrentIndex(tabIndex);
            }
        });
    }

    return ribbon;
}

void MainWindow::buildWorkspaceSwitcher() {
    // m_statusRow (bottom-right), not the ribbon: see the comment above
    // m_fullscreenButton's construction in buildRibbon() for why -- the
    // ribbon's tab row is hidden while fullscreen, which this widget used
    // to live inside via Ribbon::setCornerWidget().
    m_workspaceSwitcher = new WorkspaceSwitcher(this);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::workspaceSelected, this,
            &MainWindow::onWorkspaceSelected);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::workspaceDeleteRequested, this,
            &MainWindow::onWorkspaceDeleteRequested);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::newWorkspaceRequested, this,
            &MainWindow::onNewWorkspaceRequested);
    connect(m_workspaceSwitcher, &WorkspaceSwitcher::iconChangeRequested, this,
            &MainWindow::pickWorkspaceIcon);
    m_workspaceSwitcher->updateIcons(ThemeManager::instance().currentTheme().textPrimary);
    m_statusRow->layout()->addWidget(m_workspaceSwitcher);

    // Directly under m_statusRow in m_appShell; exactly one of the two is
    // visible at a time (updateChromeVisibility()).
    m_workspaceDock = new WorkspaceDock(m_appShell);
    connect(m_workspaceDock, &WorkspaceDock::workspaceSelected, this,
            &MainWindow::onWorkspaceSelected);
    connect(m_workspaceDock, &WorkspaceDock::workspaceDeleteRequested, this,
            &MainWindow::onWorkspaceDeleteRequested);
    connect(m_workspaceDock, &WorkspaceDock::newWorkspaceRequested, this,
            &MainWindow::onNewWorkspaceRequested);
    connect(m_workspaceDock, &WorkspaceDock::iconChangeRequested, this,
            &MainWindow::pickWorkspaceIcon);
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_workspaceDock->updateIcons(palette.textPrimary, palette.background);
    m_workspaceDock->setVisible(compactChromeActive());
    m_appShell->layout()->addWidget(m_workspaceDock);

    refreshWorkspaceSwitcher();
}

void MainWindow::refreshWorkspaceSwitcher() {
    QVector<WorkspaceSwitcher::Entry> entries;
    for (const Workspace& workspace : WorkspaceManager::instance().workspaces()) {
        entries.append({workspace.id, workspace.name, false, resolvedWorkspaceIcon(workspace.icon)});
    }
    entries.append({QString::fromLatin1(kSubscriptionsWorkspaceId), tr("Subscriptions"), true,
                    QString::fromLatin1(kSubscriptionsWorkspaceIconId)});
    const QString activeId = m_subscriptionsWorkspaceActive
                                 ? QString::fromLatin1(kSubscriptionsWorkspaceId)
                                 : WorkspaceManager::instance().activeId();
    m_workspaceSwitcher->setWorkspaces(entries, activeId);
    m_workspaceDock->setWorkspaces(entries, activeId);
}

bool MainWindow::pickWorkspaceIcon(const QString& id) {
    WorkspaceManager& workspaces = WorkspaceManager::instance();
    if (workspaces.nameFor(id).isEmpty()) {
        return false;  // built-in (Subscriptions) or already deleted
    }
    IconPickerDialog dialog(resolvedWorkspaceIcon(workspaces.iconFor(id)), this);
    if (DialogPresenter::exec(dialog, DialogPresenter::Style::Page) != QDialog::Accepted) {
        return false;
    }
    workspaces.setIconFor(id, dialog.selectedId());
    refreshWorkspaceSwitcher();
    return true;
}

void MainWindow::switchToWorkspace(const QString& id) {
    WorkspaceManager& workspaces = WorkspaceManager::instance();
    m_subscriptionsWorkspaceActive = id == QLatin1String(kSubscriptionsWorkspaceId);
    m_ribbon->setCurrentIndex(m_dashboardTabIndex);
    onRibbonTabChanged(m_dashboardTabIndex);
    refreshWorkspaceSwitcher();
    if (m_subscriptionsWorkspaceActive) {
        updateSubscriptionsWorkspace();
        return;
    }
    if (id == workspaces.activeId()) {
        return;
    }

    workspaces.setDashboardFor(workspaces.activeId(), m_dashboardGrid->toJson());
    workspaces.setActiveId(id);
    loadDashboardJson(workspaces.dashboardFor(id));
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
}

void MainWindow::cycleWorkspace(int direction) {
    QStringList ids;
    for (const auto& workspace : WorkspaceManager::instance().workspaces()) {
        ids.append(workspace.id);
    }
    ids.append(QString::fromLatin1(kSubscriptionsWorkspaceId));
    const QString activeId = m_subscriptionsWorkspaceActive
                                 ? QString::fromLatin1(kSubscriptionsWorkspaceId)
                                 : WorkspaceManager::instance().activeId();
    const int index = qMax(0, int(ids.indexOf(activeId)));
    switchToWorkspace(ids[(index + direction + ids.size()) % ids.size()]);
}

void MainWindow::onWorkspaceSelected(const QString& id) {
    switchToWorkspace(id);
}

void MainWindow::onNewWorkspaceRequested() {
    bool ok = false;
    const QString name = DialogPresenter::getText(this, tr("New Workspace"), tr("Name:"),
                                                  QLineEdit::Normal, tr("Workspace"), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }

    WorkspaceManager& workspaces = WorkspaceManager::instance();
    workspaces.setDashboardFor(workspaces.activeId(), m_dashboardGrid->toJson());
    const QString newId = workspaces.createWorkspace(name.trimmed());
    m_subscriptionsWorkspaceActive = false;
    m_ribbon->setCurrentIndex(m_dashboardTabIndex);
    onRibbonTabChanged(m_dashboardTabIndex);
    loadDashboardJson(QJsonObject());
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
    // Straight on to its icon -- in compact chrome the icon is all the dock
    // shows. Cancelling keeps the default glyph; it can be changed later.
    pickWorkspaceIcon(newId);
    postStatus(tr("Created workspace \"%1\".").arg(name.trimmed()), 3000, StatusSeverity::Success);
}

void MainWindow::onWorkspaceDeleteRequested(const QString& id) {
    if (id == QLatin1String(kSubscriptionsWorkspaceId)) {
        return;
    }
    WorkspaceManager& workspaces = WorkspaceManager::instance();
    if (workspaces.workspaces().size() <= 1) {
        return;
    }

    const QString name = workspaces.nameFor(id);
    if (DialogPresenter::question(
            this, tr("Delete Workspace"),
            tr("Delete workspace \"%1\"? This cannot be undone.").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    const bool wasActive = id == workspaces.activeId();
    workspaces.removeWorkspace(id);
    if (wasActive) {
        loadDashboardJson(workspaces.dashboardFor(workspaces.activeId()));
        m_dashboardGrid->undoStack()->clear();
        refreshPropertiesPanel();
        refreshLayersPanel();
    }
    refreshWorkspaceSwitcher();
    postStatus(tr("Deleted workspace \"%1\".").arg(name), 3000);
}

void MainWindow::buildLayersPanel() {
    m_layersPanel = new LayersPanel(this);
    connect(m_layersPanel, &LayersPanel::itemSelected, m_dashboardGrid, &DashboardGrid::selectItem);

    // These used to be their own ribbon page (the Layout tab); now that
    // editing is a mode of the Dashboard tab instead of a separate tab, they
    // live inside the Layers panel itself, above the list -- two rows since
    // five button groups don't fit in one at this panel's width.
    auto* toolbar = new QWidget(m_layersPanel);
    auto* toolbarLayout = new QVBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                      kRibbonPageMarginV);
    toolbarLayout->setSpacing(kRibbonGroupPadding);

    auto* toolbarRow1 = new QHBoxLayout();
    toolbarRow1->setSpacing(kRibbonGroupSpacing);
    toolbarRow1->addWidget(Ribbon::createButtonGroup(toolbar, {m_addWidgetAction, m_removeAction}));
    toolbarRow1->addWidget(Ribbon::createButtonGroup(toolbar, {m_copyAction, m_pasteAction}));
    toolbarRow1->addStretch();
    toolbarLayout->addLayout(toolbarRow1);

    auto* toolbarRow2 = new QHBoxLayout();
    toolbarRow2->setSpacing(kRibbonGroupSpacing);
    toolbarRow2->addWidget(Ribbon::createButtonGroup(
        toolbar,
        {m_bringToFrontAction, m_bringForwardAction, m_sendBackwardAction, m_sendToBackAction}));
    toolbarRow2->addWidget(Ribbon::createButtonGroup(toolbar, {m_groupAction, m_ungroupAction}));
    toolbarRow2->addWidget(Ribbon::createButtonGroup(toolbar, {m_undoAction, m_redoAction}));
    toolbarRow2->addStretch();
    toolbarLayout->addLayout(toolbarRow2);

    m_layersPanel->setToolbar(toolbar);

    // Only relevant while editing is enabled -- matches m_propertiesPanel
    // (see buildPropertiesPanel() below).
    m_layersPanel->hide();
}

void MainWindow::buildPropertiesPanel() {
    m_propertiesPanel = new PropertiesPanel(this);
    m_propertiesPanel->setAvailableTypes(WidgetRegistry::instance().availableTypes());

    connect(m_propertiesPanel, &PropertiesPanel::typeChangeRequested, this,
            &MainWindow::onPanelTypeChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::nameChangeRequested, this,
            &MainWindow::onPanelNameChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::keyChangeRequested, this,
            &MainWindow::onPanelKeyChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::configChangeRequested, this,
            &MainWindow::onPanelConfigChangeRequested);

    // Only relevant while editing is enabled — matches m_addWidgetAction,
    // which also starts disabled until editingActive() is true (see
    // onRibbonTabChanged/onEditModeToggled).
    m_propertiesPanel->hide();
}

void MainWindow::updateRibbonIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    // Appends " (shortcut)" to an action's tooltip when it has one bound, so
    // every ribbon button advertises its key the way Remove/Copy/Paste
    // already did by hand.
    const auto withKey = [](const QString& tip, const QAction* action) {
        const QString keys = action->shortcut().toString(QKeySequence::NativeText);
        return keys.isEmpty() ? tip : QStringLiteral("%1 (%2)").arg(tip, keys);
    };

    m_addWidgetAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addWidgetAction->setToolTip(withKey(tr("Add widget"), m_addWidgetAction));
    m_removeAction->setIcon(makeMinusIcon(palette.danger));
    m_removeAction->setToolTip(
        tr("Remove selected widget (%1)")
            .arg(m_removeAction->shortcut().toString(QKeySequence::NativeText)));
    m_addDeviceAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addDeviceAction->setToolTip(withKey(tr("Add device"), m_addDeviceAction));
    m_removeDeviceAction->setIcon(makeMinusIcon(palette.danger));
    m_removeDeviceAction->setToolTip(
        tr("Remove selected device (%1)")
            .arg(m_removeDeviceAction->shortcut().toString(QKeySequence::NativeText)));
    m_openLogFileAction->setIcon(makeFolderIcon(palette.textPrimary));
    m_openLogFileAction->setToolTip(withKey(tr("Open a .blog log file"), m_openLogFileAction));
    m_copyAction->setIcon(makeCopyIcon(palette.textPrimary));
    m_copyAction->setToolTip(tr("Copy selected widget (%1)")
                                 .arg(m_copyAction->shortcut().toString(QKeySequence::NativeText)));
    m_pasteAction->setIcon(makePasteIcon(palette.textPrimary));
    m_pasteAction->setToolTip(
        tr("Paste as a new widget (%1)")
            .arg(m_pasteAction->shortcut().toString(QKeySequence::NativeText)));
    m_bringToFrontAction->setIcon(makeBringToFrontIcon(palette.textPrimary));
    m_bringToFrontAction->setToolTip(withKey(tr("Bring to front"), m_bringToFrontAction));
    m_bringForwardAction->setIcon(makeBringForwardIcon(palette.textPrimary));
    m_bringForwardAction->setToolTip(withKey(tr("Bring forward"), m_bringForwardAction));
    m_sendBackwardAction->setIcon(makeSendBackwardIcon(palette.textPrimary));
    m_sendBackwardAction->setToolTip(withKey(tr("Send backward"), m_sendBackwardAction));
    m_sendToBackAction->setIcon(makeSendToBackIcon(palette.textPrimary));
    m_sendToBackAction->setToolTip(withKey(tr("Send to back"), m_sendToBackAction));
    m_groupAction->setIcon(makeGroupIcon(palette.textPrimary));
    m_groupAction->setToolTip(
        withKey(tr("Group — lock the selected widgets' positions together"), m_groupAction));
    m_ungroupAction->setIcon(makeUngroupIcon(palette.textPrimary));
    m_ungroupAction->setToolTip(withKey(
        tr("Ungroup — let the selected widgets move independently again"), m_ungroupAction));
    // No explicit setToolTip(): QAction falls back to text(), which
    // QUndoStack keeps updated with the pending command's description
    // (e.g. "Undo Move Widget"); the shortcut still shows up in the menu/
    // button via QAction::shortcut(), it's just not spelled out in the text.
    m_undoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/true));
    m_redoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/false));
    m_fullscreenButton->setIcon(
        makeFullscreenIcon(palette.textPrimary, m_fullscreenButton->isChecked()));
    if (m_workspaceSwitcher) {
        m_workspaceSwitcher->updateIcons(palette.textPrimary);
        m_workspaceDock->updateIcons(palette.textPrimary, palette.background);
    }
    m_optionsButton->setIcon(makeOptionsIcon(palette.textPrimary));
    updateEditModeIcon();
    updateTogglePanelsIcon();
    syncBreakpointChrome();
}

void MainWindow::updateScreenSizeButtonIcon() {
    if (!m_screenSizeButton) {
        return;
    }
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const DashboardBreakpoint breakpoint = m_dashboardGrid->currentBreakpoint();
    switch (breakpoint) {
        case DashboardBreakpoint::Small:
            m_screenSizeButton->setIcon(makePhoneIcon(palette.textPrimary));
            m_screenSizeButton->setToolTip(tr("Screen size: Small"));
            break;
        case DashboardBreakpoint::Medium:
            m_screenSizeButton->setIcon(makeTabletIcon(palette.textPrimary));
            m_screenSizeButton->setToolTip(tr("Screen size: Medium"));
            break;
        case DashboardBreakpoint::Large:
            m_screenSizeButton->setIcon(makeNotebookIcon(palette.textPrimary));
            m_screenSizeButton->setToolTip(tr("Screen size: Large"));
            break;
    }
    // No QSignalBlocker here (there used to be one): the menu's own actions
    // connect to triggered(), never toggled(), so setChecked() below -- which
    // only ever emits toggled() -- had nothing to block in the first place.
    const QList<QAction*> actions = m_screenSizeMenu->actions();
    for (QAction* action : actions) {
        action->setChecked(breakpointFromActionData(action->data().toInt()) == breakpoint);
    }
}

void MainWindow::updateCanvasHeightButtons() {
    if (!m_canvasShrinkButton || !m_canvasGrowButton) {
        return;
    }
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const DashboardBreakpoint breakpoint = m_dashboardGrid->currentBreakpoint();
    // Growing/shrinking the canvas only means anything for a preview
    // breakpoint (see DashboardGrid::growCanvasHeight()) -- hide both
    // buttons on Notebook instead of leaving them uselessly clickable. Also
    // gated by editingActive() rather than just Developer mode: it's an
    // edit to the arrangement, same as dragging/resizing a widget, so it
    // stays locked behind the edit-mode padlock even while already logged
    // in as developer (unlike the screen-size button itself, which only
    // switches which layout is *shown*, not editable).
    const bool canvasHeightAdjustable = isPreviewBreakpoint(breakpoint) && editingActive();
    m_canvasShrinkButton->setIcon(makeMinusIcon(palette.textPrimary));
    m_canvasGrowButton->setIcon(makePlusIcon(palette.textPrimary));
    m_canvasShrinkButton->setVisible(canvasHeightAdjustable);
    m_canvasGrowButton->setVisible(canvasHeightAdjustable);
}

void MainWindow::syncBreakpointChrome() {
    updateScreenSizeButtonIcon();
    updateCanvasHeightButtons();
    // compactChromeActive() depends on previewActive(), which depends on the
    // breakpoint -- every call site that reaches this facade (breakpoint
    // changes, tab changes, applyUserMode(), theme refresh) is exactly the
    // set of places menuBar()/m_optionsButton/m_chromeTopBar visibility can
    // need to change too.
    updateChromeVisibility();
}

void MainWindow::onScreenSizeBreakpointSelected(DashboardBreakpoint breakpoint) {
    m_dashboardGrid->setBreakpoint(breakpoint);
    applyBreakpointViewport();
}

bool MainWindow::previewActive() const {
    // Only Developer mode's manual Phone/Tablet preview ever shows a device
    // frame: User mode's breakpoint already tracks the real screen (see
    // applyAutoBreakpoint()), so there's no smaller device left to simulate,
    // same as the old window-resize preview was Developer-only.
    return UserModeManager::instance().mode() == UserModeManager::UserMode::Developer &&
           isPreviewBreakpoint(m_dashboardGrid->currentBreakpoint());
}

bool MainWindow::compactChromeActive() const {
    return kUsesCompactChrome || previewActive();
}

void MainWindow::updateChromeVisibility() {
    const bool compact = compactChromeActive();
    // No window manager on Android (and none simulated in a preview): dialogs
    // go in-window there instead of opening as bare top-level windows.
    DialogPresenter::setEmbedded(compact);
    const bool isDeveloper =
        UserModeManager::instance().mode() == UserModeManager::UserMode::Developer;
    // Fullscreen hides the native menu bar to maximize screen space, for a
    // reason entirely independent of compactChromeActive() -- but the two
    // must not fight over the same widget: entering fullscreen while compact
    // chrome is already active must not "restore" a menu bar that was never
    // meant to be there once fullscreen ends, and leaving fullscreen while
    // still in a preview must not bring one back either. Either reason is
    // enough to keep it down; only when NEITHER applies does it come back.
    const bool fullscreen = m_fullscreenButton && m_fullscreenButton->isChecked();
    // Hide the whole tab row so its height returns to the dashboard in User
    // mode. Centralize this with fullscreen to avoid restoring it on exit.
    const bool tabBarVisible = developerUiActive() && !fullscreen;
    m_ribbon->setTabBarVisible(tabBarVisible);
    // kRibbonTopMargin only separates the tab strip from the menu bar above
    // it; with the strip hidden it would leave a bare band between the page
    // row (device status) and the top edge of the screen.
    if (m_appShell) {
        m_appShell->layout()->setContentsMargins(0, tabBarVisible ? kRibbonTopMargin : 0, 0, 0);
    }
    menuBar()->setVisible(!compact && !fullscreen);
    // Host the options menu at the left of the current tab's ribbon page on
    // EVERY tab (not only Dashboard's device-status row), so it stays in the
    // same spot while switching tabs. Log/BTP/Settings pages are built empty,
    // so they get the same row layout the button-group pages use on demand.
    QWidget* optionsRow = m_ribbon->pageAt(m_ribbon->currentIndex());
    if (optionsRow && !optionsRow->layout()) {
        auto* rowLayout = new QHBoxLayout(optionsRow);
        rowLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                      kRibbonPageMarginV);
        rowLayout->setSpacing(kRibbonGroupSpacing);
        rowLayout->addStretch();
    }
    if (!optionsRow) {
        optionsRow = m_chromeTopBar;
    }
    if (m_optionsButton->parentWidget() != optionsRow) {
        m_optionsButton->parentWidget()->layout()->removeWidget(m_optionsButton);
        static_cast<QHBoxLayout*>(optionsRow->layout())->insertWidget(0, m_optionsButton);
    }
    m_optionsButton->setVisible(compact);
    // Compact chrome gives the whole bottom bar to workspace navigation:
    // m_statusRow (fullscreen toggle, status text, workspace switcher,
    // screen-size selector) makes way for m_workspaceDock. Status messages
    // still land in the Notifications history.
    m_statusRow->setVisible(!compact);
    if (m_workspaceDock) {
        m_workspaceDock->setVisible(compact);
    }
    // The screen-size selector must stay reachable in a Developer preview
    // (it's the way back out of Phone/Tablet), so it follows the options
    // button up to the right end of the current ribbon page; back to its
    // spot in m_statusRow, just left of the size grip, otherwise.
    // Skipped until the constructor's tail has seated it in m_statusRow
    // (m_workspaceDock is built right before that).
    QWidget* screenSizeRow = compact ? optionsRow : m_statusRow;
    if (m_workspaceDock && m_screenSizeButton->parentWidget() != screenSizeRow) {
        m_screenSizeButton->parentWidget()->layout()->removeWidget(m_screenSizeButton);
        if (compact) {
            screenSizeRow->layout()->addWidget(m_screenSizeButton);
        } else {
            restoreScreenSizeButtonToStatusRow();
        }
    }
    // Keep manual size selection available while previewing the User interface.
    m_screenSizeButton->setVisible(isDeveloper);
    m_chromeTopBar->setVisible(compact && optionsRow == m_chromeTopBar);

    // User mode's branding (Developer's "View as user" preview included).
    if (m_brandCornerMark) {
        m_brandCornerMark->setVisible(!developerUiActive());
        positionBrandCornerMark();
    }
}

void MainWindow::removeRibbonTab(int index) {
    if (m_optionsButton->parentWidget() == m_ribbon->pageAt(index)) {
        m_optionsButton->parentWidget()->layout()->removeWidget(m_optionsButton);
        static_cast<QHBoxLayout*>(m_chromeTopBar->layout())->insertWidget(0, m_optionsButton);
    }
    // Same rescue for the compact-chrome screen-size selector; the next
    // updateChromeVisibility() moves it on to whichever page is current.
    if (m_screenSizeButton->parentWidget() == m_ribbon->pageAt(index)) {
        m_screenSizeButton->parentWidget()->layout()->removeWidget(m_screenSizeButton);
        restoreScreenSizeButtonToStatusRow();
    }
    m_ribbon->removeTab(index);
}

void MainWindow::restoreScreenSizeButtonToStatusRow() {
    // Its constructor-time spot: right of m_workspaceSwitcher, left of the
    // size grip.
    auto* statusRowLayout = static_cast<QHBoxLayout*>(m_statusRow->layout());
    statusRowLayout->insertWidget(statusRowLayout->indexOf(m_workspaceSwitcher) + 1,
                                  m_screenSizeButton);
}

void MainWindow::applyBreakpointViewport() {
    const DashboardBreakpoint breakpoint = m_dashboardGrid->currentBreakpoint();
    if (!previewActive()) {
        m_devicePreviewFrame->setDeviceSize(QSize());
        m_dashboardGrid->setMinimumHeight(0);
        return;
    }

    // The device's own screen is a fixed size now (see DevicePreviewFrame's
    // item-5 rewrite: both its dimensions clamp to whatever room is actually
    // available, the way a real phone's screen is always shown whole) --
    // canvasHeightMultiplier() no longer stretches the DEVICE (that used to
    // make the frame itself grow past the available space and get wrapped
    // in an outer scroll area); it stretches the CONTENT inside it instead,
    // below, the same way a too-tall page scrolls on an actual phone.
    const QSize base =
        breakpoint == DashboardBreakpoint::Small ? kPhoneViewportSize : kTabletViewportSize;
    m_devicePreviewFrame->setDeviceSize(base);

    const double multiplier = m_dashboardGrid->canvasHeightMultiplier(breakpoint);
    if (multiplier > 0.0) {
        // 0.0 means growCanvasHeight() has never been clicked for this
        // breakpoint ("off") -- floored at 1.0 here so the very first grow
        // step reads as a visible jump rather than starting from zero, same
        // as this used to floor the device's own height. m_dashboardScrollArea
        // -- not the device frame -- is what's actually available for canvas
        // content once the chrome around it (m_chromeTopBar, the ribbon,
        // m_statusRow) is accounted for.
        const int visibleHeight = m_dashboardScrollArea->viewport()->height();
        m_dashboardGrid->setMinimumHeight(qRound(visibleHeight * qMax(1.0, multiplier)));
    } else {
        m_dashboardGrid->setMinimumHeight(0);
    }
}

void MainWindow::updateEditModeIcon() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    // Always textPrimary, like m_fullscreenButton's own icon (see
    // makeFullscreenIcon's call site below) -- QToolButton:checked's QSS
    // paints the button's background @accent@ (stylesheet.cpp), so drawing
    // the glyph itself in palette.accent when checked (as this used to)
    // made it disappear into its own background. On/off is conveyed by the
    // padlock's shape (open/closed), not by recoloring it.
    m_editModeButton->setIcon(makeLockIcon(palette.textPrimary, !m_editModeEnabled));
    m_editModeButton->setToolTip(m_editModeEnabled
                                     ? tr("Disable editing — lock the dashboard layout")
                                     : tr("Enable editing — rearrange the dashboard layout"));
}

void MainWindow::updateTogglePanelsIcon() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    // Always textPrimary -- same reasoning as updateEditModeIcon() above.
    m_togglePanelsButton->setIcon(makePanelsIcon(palette.textPrimary, m_panelsVisible));
    m_togglePanelsButton->setToolTip(m_panelsVisible ? tr("Hide the Layers/Properties panels")
                                                     : tr("Show the Layers/Properties panels"));
}

void MainWindow::onRibbonTabChanged(int index) {
    m_dashboardTabActive = index == m_dashboardTabIndex;
    m_devicesTabActive = index == m_devicesTabIndex;
    // Matched by ribbon page pointer rather than by index, same reasoning as
    // m_openLogTabs: the OTA tab is closable, so its index shifts whenever
    // another closable tab is added/removed. Computed here (not just below,
    // where activeContent needs it too) so it's already correct by the time
    // updateDeviceSelectionActions() runs a few lines down.
    QWidget* currentPage = m_ribbon->pageAt(index);
    m_otaTabActive = currentPage != nullptr && currentPage == m_otaTabPage;
    m_dashboardGrid->setEditMode(editingActive());
    m_addWidgetAction->setEnabled(editingActive());
    m_togglePanelsButton->setEnabled(editingActive());
    // The canvas +/- buttons are gated by editingActive() too (see its own
    // comment) -- switching tabs can flip it just like the padlock can.
    syncBreakpointChrome();
    // Ctrl+Z/Ctrl+Y (m_undoAction/m_redoAction) are created from m_undoGroup,
    // not either stack directly -- flip which one is "active" here so they
    // always undo/redo whatever the visible tab actually shows. The
    // Dashboard tab displays the dashboard in both its read-only and
    // editable modes, so either way it falls through to its stack.
    m_undoGroup->setActiveStack(m_devicesTabActive ? m_devicesGrid->undoStack()
                                                   : m_dashboardGrid->undoStack());
    updatePanelVisibility();
    updateSelectionActions();
    updateDeviceSelectionActions();

    if (index == m_dashboardTabIndex) {
        refreshDeviceStatusLabel();
    }

    // Swaps the whole content area between the canvas, the Devices grid and
    // whichever log tab is now current; the Dashboard tab keeps showing the
    // canvas in both its modes, only Devices/a log tab swap away from it
    // (m_dashboardTabActive above already goes false here on its own, so
    // edit mode/panels don't need any extra handling for those). Log tabs
    // are matched by their ribbon page pointer rather than by index -- see
    // m_openLogTabs.
    QWidget* activeContent = m_subscriptionsWorkspaceActive
                                 ? static_cast<QWidget*>(m_subscriptionsTable)
                                 : static_cast<QWidget*>(m_dashboardScrollArea);
    if (index == m_devicesTabIndex) {
        activeContent = m_devicesGrid;
    } else if (m_otaTabActive) {
        activeContent = m_otaTab;
    } else if (currentPage != nullptr && currentPage == m_btpMonitorTabPage) {
        activeContent = m_btpMonitorTab;
    } else if (currentPage != nullptr && currentPage == m_settingsTabPage) {
        activeContent = m_settingsTab;
    } else if (currentPage != nullptr) {
        for (const OpenLogTab& tab : m_openLogTabs) {
            if (tab.ribbonPage == currentPage) {
                activeContent = tab.viewer;
                break;
            }
        }
    }
    m_contentStack->setCurrentWidget(activeContent);

    // Only generate OTA status-poll HTTP traffic while its tab is actually
    // the visible one.
    if (m_otaTab != nullptr) {
        m_otaTab->setActive(m_otaTabActive);
    }
}

void MainWindow::onEditModeToggled(bool enabled) {
    m_editModeEnabled = enabled;
    updateEditModeIcon();
    // Same follow-up calls onRibbonTabChanged() makes for m_dashboardTabActive
    // -- editingActive() folds both flags together, so flipping either one
    // needs the same refresh.
    m_dashboardGrid->setEditMode(editingActive());
    m_addWidgetAction->setEnabled(editingActive());
    m_togglePanelsButton->setEnabled(editingActive());
    // The canvas +/- buttons are locked behind the padlock too -- see their
    // own comment in updateCanvasHeightButtons().
    syncBreakpointChrome();
    updatePanelVisibility();
    updateSelectionActions();
}

void MainWindow::onTogglePanelsClicked(bool visible) {
    m_panelsVisible = visible;
    updateTogglePanelsIcon();
    updatePanelVisibility();
}

void MainWindow::onSelectionChanged(const QString&) {
    updateSelectionActions();
    refreshPropertiesPanel();
    refreshLayersPanel();
    updatePanelVisibility();
}

void MainWindow::updatePanelVisibility() {
    // Hiding a panel mid-drag would yank it out from under the cursor --
    // m_dockController re-triggers this itself (via dragFinished()) once the
    // gesture ends, so any visibility change that landed during it isn't
    // lost, just deferred.
    if (m_dockController->isDragging()) {
        return;
    }
    // m_panelsVisible (the show/hide toggle) is the direct, sole control for
    // these panels -- selection used to also open them on its own, but that
    // made them pop in and out as the selection changed, on top of the
    // toggle's own effect, which read as two things fighting over the same
    // panels. Either way, both stay hidden outright unless editingActive()
    // -- the lock being off (or the Devices tab being current) always wins,
    // regardless of what m_panelsVisible itself remembers.
    const bool showPanels = editingActive() && m_panelsVisible;
    const bool showProperties = showPanels;
    const bool showLayers = showPanels;
    m_propertiesPanel->setVisible(showProperties);
    m_layersPanel->setVisible(showLayers);
    // Keep the toggle button's own checked look in sync with whether panels
    // are actually showing right now, not just with m_panelsVisible -- while
    // locked (editingActive() false) it's disabled anyway, but without this
    // it would stay visually checked (from before the lock closed) instead
    // of reading as off like the hidden panels it no longer controls.
    // Blocked so this doesn't loop back into onTogglePanelsClicked() and
    // stomp the very m_panelsVisible preference it's supposed to reflect.
    {
        const QSignalBlocker blocker(m_togglePanelsButton);
        m_togglePanelsButton->setChecked(showPanels);
    }
    // Both panels are floated over m_dashboardGrid rather than laid out
    // beside it (see positionOverlayPanels()), so re-showing one has to
    // explicitly reclaim the top of the stack -- a plain setVisible(true)
    // doesn't change sibling stacking order.
    if (showProperties) {
        m_propertiesPanel->raise();
    }
    if (showLayers) {
        m_layersPanel->raise();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_contentRow && event->type() == QEvent::Resize) {
        positionOverlayPanels();
    } else if ((watched == m_appShell || watched == m_ribbon) &&
               (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        positionBrandCornerMark();
    } else if (watched == m_dashboardScrollArea->viewport() && event->type() == QEvent::Resize) {
        // Re-derives the canvas-height-multiplier pixel math, which reads
        // this viewport's current height -- see applyBreakpointViewport().
        applyBreakpointViewport();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::positionOverlayPanels() {
    m_dockController->relayout();
}

void MainWindow::positionBrandCornerMark() {
    if (!m_brandCornerMark) {
        return;
    }
    // Taller than the ribbon row (kRibbonPageHeight) so the wordmark stays
    // legible; capped to a share of the width so a phone-sized window keeps
    // most of its top row. Its middle trace runs along the ribbon's bottom
    // edge -- the border between the top row and the dashboard.
    constexpr int kBrandCornerMarkHeight = 51;
    const int ribbonBottom = m_ribbon->mapTo(m_appShell, QPoint(0, m_ribbon->height())).y();
    m_brandCornerMark->reposition(kBrandCornerMarkHeight, m_appShell->width() * 2 / 5,
                                  ribbonBottom);
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!m_floatingPanelsPositioned) {
        m_floatingPanelsPositioned = true;
        m_dockController->applyFloatingPositions();
    }
}

void MainWindow::moveEvent(QMoveEvent* event) {
    QMainWindow::moveEvent(event);
    // Ignore moves before the first applyFloatingPositions() -- any floating
    // panel is still sitting wherever its constructor left it, so there's
    // nothing meaningful to carry along yet.
    //
    // WindowNoState is the guard for "this is a user dragging the window",
    // which is the only move trackWindowMoved() is meant to follow. A
    // maximize or fullscreen transition also emits moveEvent, with a delta
    // that jumps the window to the screen corner -- following that dragged
    // floating panels off-screen, and the transition back doesn't produce a
    // symmetric delta to undo it (onFullscreenToggled() restores via
    // restoreGeometry(), not by moving). A window can't be dragged while
    // maximized or fullscreen anyway, so nothing legitimate is lost here.
    if (m_floatingPanelsPositioned && windowState() == Qt::WindowNoState) {
        m_dockController->trackWindowMoved(event->pos() - event->oldPos());
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    applyAutoBreakpoint();
}

void MainWindow::updateSelectionActions() {
    const bool hasAnySelection = editingActive() && m_dashboardGrid->selectedCount() > 0;
    const bool hasSingleSelection = editingActive() && !m_dashboardGrid->selectedItemId().isEmpty();
    m_removeAction->setEnabled(hasAnySelection);
    m_copyAction->setEnabled(hasSingleSelection);
    m_pasteAction->setEnabled(editingActive() && m_dashboardGrid->canPaste());
    m_bringToFrontAction->setEnabled(hasSingleSelection);
    m_bringForwardAction->setEnabled(hasSingleSelection);
    m_sendBackwardAction->setEnabled(hasSingleSelection);
    m_sendToBackAction->setEnabled(hasSingleSelection);
    m_groupAction->setEnabled(editingActive() && m_dashboardGrid->selectedCount() >= 2);
    m_ungroupAction->setEnabled(editingActive() && m_dashboardGrid->selectionHasGroup());
}

void MainWindow::refreshPropertiesPanel() {
    const bool hasSelection = !m_dashboardGrid->selectedItemId().isEmpty();
    m_propertiesPanel->setSelection(hasSelection, m_dashboardGrid->selectedItemTypeId(),
                                    m_dashboardGrid->selectedItemDisplayName(),
                                    m_dashboardGrid->selectedItemKey(),
                                    m_dashboardGrid->selectedItemConfig());
}

void MainWindow::refreshLayersPanel() {
    m_layersPanel->setItems(m_dashboardGrid->layerEntries(), m_dashboardGrid->selectedItemId());
}

void MainWindow::onAddWidget() {
    // Drops in the first registered type; DashboardGrid::addItem() selects
    // it immediately, so the properties panel comes up already showing it
    // — the type (and name/key) is picked there, not in a dialog upfront.
    const QVector<WidgetTypeInfo> types = WidgetRegistry::instance().availableTypes();
    if (types.isEmpty()) {
        return;
    }
    m_dashboardGrid->addItem(types.first().typeId);
}

void MainWindow::onAddDevice() {
    // Drops in a placeholder mock device -- no picker dialog, same shape as
    // onAddWidget() above. DevicesGrid owns opening DeviceConfigDialog
    // itself (gear click on the new card), so renaming just happens there;
    // this slot doesn't reach into that flow.
    Device device;
    device.name = tr("New Device");
    m_devicesGrid->addDevice(device);
}

void MainWindow::updateDeviceSelectionActions() {
    const bool devicesTabHasSelection = m_devicesTabActive && m_devicesGrid->selectedCount() > 0;
    const bool otaTabHasSelection =
        m_otaTabActive && m_otaTab != nullptr && !m_otaTab->selectedDeviceId().isEmpty();
    m_removeDeviceAction->setEnabled(devicesTabHasSelection || otaTabHasSelection);
}

void MainWindow::onRemoveDeviceRequested() {
    // Both tabs share these actions (see buildRibbon()/onOpenOtaTab()), but
    // each owns its own selection state -- DevicesGrid's card selection and
    // OtaTab's row selection are entirely separate, so "remove" has to ask
    // whichever tab is actually visible rather than always going through
    // DevicesGrid::removeSelected().
    if (m_devicesTabActive) {
        m_devicesGrid->removeSelected();
    } else if (m_otaTabActive && m_otaTab != nullptr) {
        const QString id = m_otaTab->selectedDeviceId();
        if (!id.isEmpty()) {
            m_devicesGrid->removeDevice(id);
        }
    }
}

quint32 MainWindow::deviceSelfSourceId(const Device& device) const {
    // A hub-channel device's identity is its persisted target -- known even
    // while disconnected, since it is what the child was configured to talk
    // to. Everything else only becomes known live, from its own HELLO_RESULT
    // (Device::btpId, formatted "0x...." by BtpBackend).
    if (device.transportType == TransportType::HubChannel) {
        return device.peerSourceId;
    }
    if (device.btpId.isEmpty()) {
        return 0;
    }
    return quint32(device.btpId.toULongLong(nullptr, 0));
}

void MainWindow::refreshPropertiesPanelDevices() {
    const QVector<Device> devices = m_devicesGrid->devices();
    QVector<DeviceOption> options;
    options.reserve(devices.size());
    for (const Device& device : devices) {
        // See DeviceOption::selfSourceId -- deviceSelfSourceId() is shared
        // with hubPeersFor() so the two definitions of "this device's own
        // source_id" cannot drift apart.
        DeviceOption option{device.id, device.name, {}, deviceSelfSourceId(device)};
        if (DeviceConnection* connection = m_deviceConnections.value(device.id)) {
            option.catalogTopics = connection->backend()->catalogTopics();
        }
        options.append(option);
    }
    m_propertiesPanel->setAvailableDevices(options);

    // The serial monitors' terminal tabs are labelled by device name, so they
    // need the same list -- follows every add/remove/rename/catalog refresh
    // that lands here.
    QHash<QString, QString> deviceNames;
    for (const DeviceOption& option : options) {
        deviceNames.insert(option.id, option.name);
    }
    if (m_serialWidgetBridge) {
        m_serialWidgetBridge->setDeviceNames(deviceNames);
    }
}

void MainWindow::releaseHubPeerWatch(const QString& deviceId) {
    const auto it = m_hubPeerWatches.constFind(deviceId);
    if (it == m_hubPeerWatches.constEnd()) {
        return;
    }
    // Drop the SUBSCRIBE only if the connection is still around: on the
    // device-removed path it has already been taken out of the hash, and the
    // subscription died with its session anyway.
    if (it->handle != 0) {
        if (DeviceConnection* connection = m_deviceConnections.value(deviceId)) {
            connection->backend()->removeSubscriber(it->handle);
        }
    }
    m_hubPeerWatches.remove(deviceId);
}

bool MainWindow::ensureHubPeerWatch(const QString& parentDeviceId) {
    if (parentDeviceId.isEmpty()) {
        return false;
    }
    DeviceConnection* connection = m_deviceConnections.value(parentDeviceId);
    if (connection == nullptr) {
        return false;
    }

    HubPeersWatch& watch = m_hubPeerWatches[parentDeviceId];
    if (watch.handle != 0) {
        return true;  // already subscribed (survives a session drop -- see
                      // SubscriptionManager::onSessionLost)
    }

    // Resolution is retried on every call until it succeeds, because the
    // catalog it needs only exists once the manifest exchange has completed.
    // Scanned rather than looked up: DevicesGrid exposes no by-id accessor and
    // the list is a handful of entries.
    quint32 selfSourceId = 0;
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.id == parentDeviceId) {
            selfSourceId = deviceSelfSourceId(device);
            break;
        }
    }
    const QVector<CatalogTopicInfo> topics = connection->backend()->catalogTopics();
    for (const CatalogTopicInfo& topic : topics) {
        // Filtered by the hub's own source_id: a hub's session carries its
        // children's frames too, so its catalog is not guaranteed to describe
        // only itself. A robot behind the hub that happened to publish a topic
        // also called "hub.peers" must not be mistaken for the dongle's.
        if (topic.name != QLatin1String(kHubPeersTopicName) ||
            (selfSourceId != 0 && topic.sourceId != selfSourceId)) {
            continue;
        }

        QHash<quint16, QString> fieldIdToName;
        for (const CatalogTopicField& field : topic.fields) {
            fieldIdToName.insert(field.fieldId, field.name);
        }
        if (!watch.accumulator.resolve(fieldIdToName)) {
            // A topic named hub.peers whose schema is missing one of the six
            // arrays is not one this can decode. Left unsubscribed rather than
            // half-read: a peer row with no source_id is exactly what must
            // never reach Device::peerSourceId.
            continue;
        }

        watch.sourceId = topic.sourceId;
        watch.topicId = topic.topicId;
        watch.handle = connection->backend()->updateSubscriber(0, topic.sourceId, topic.topicId,
                                                               kHubPeersRequestedRateMillihz);
        break;
    }
    return watch.handle != 0;
}

QVector<HubPeer> MainWindow::hubPeersFor(const QString& parentDeviceId) {
    if (!ensureHubPeerWatch(parentDeviceId)) {
        return {};  // "Via:" not chosen, not connected, or manifest not in yet
    }
    return m_hubPeerWatches[parentDeviceId].accumulator.peers();
}

void MainWindow::syncHubPeerWatches() {
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.transportType == TransportType::HubChannel && device.connected &&
            !device.parentDeviceId.isEmpty()) {
            ensureHubPeerWatch(device.parentDeviceId);
        }
    }
}

void MainWindow::reconcileHubChildPresence() {
    // A hub child's robot is "live" when its own frames are still reaching this
    // TraceView -- a channel-B frame that passed AEAD open, no older than this.
    // The dongle publishes authenticated peer presence at up to 2 Hz and
    // declares a missing robot offline after 1.5 s. Keep direct robot frames
    // as the stronger signal, but only for a short grace period so a powered
    // off robot is visible to the operator promptly.
    constexpr qint64 kPeerFrameLiveMs = 2500;
    // hub.peers is already a debounced/authenticated signal; adding another
    // five one-second UI ticks hid a confirmed disconnection for far too long.
    constexpr int kOfflineTicksToConfirm = 1;

    syncHubPeerWatches();

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    bool anyChanged = false;
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.transportType != TransportType::HubChannel || !device.connected ||
            device.parentDeviceId.isEmpty() || device.peerSourceId == 0) {
            continue;
        }

        // (1) Ground truth: are the robot's own frames reaching us end to end?
        // This is verified locally (AEAD open on channel B) and is exactly what
        // the operator sees in the BTP traffic monitor. It does NOT depend on
        // the dongle's hub.peers view, which tracks authenticated channel-C
        // STATUS separately and can lag or disagree -- the false-red this whole
        // rewrite is about.
        qint64 lastPeerFrameMs = 0;
        if (DeviceConnection* c = m_deviceConnections.value(device.id)) {
            if (auto* btp = qobject_cast<BtpBackend*>(c->backend())) {
                lastPeerFrameMs = btp->lastPeerDataFrameMsSinceEpoch();
            }
        }
        const bool dataFlowing = lastPeerFrameMs != 0 && nowMs - lastPeerFrameMs < kPeerFrameLiveMs;

        // (2) Fallback: the dongle's hub.peers opinion, for the window before a
        // subscription is producing telemetry. Also the only source of the
        // robot's boot_id, which onPeerPresence() needs for reboot detection.
        bool hubKnown = false;
        bool hubOnline = false;
        quint32 bootId = device.peerBootId;
        qint8 rssi = device.peerRssi;
        quint32 rttMs = device.peerRttMs;
        const auto watchIt = m_hubPeerWatches.constFind(device.parentDeviceId);
        if (watchIt != m_hubPeerWatches.constEnd() && watchIt->handle != 0) {
            const QVector<HubPeer> peers = watchIt->accumulator.peers();
            if (!peers.isEmpty()) {
                hubKnown = true;
                for (const HubPeer& peer : peers) {
                    if (peer.sourceId == device.peerSourceId) {
                        hubOnline = peer.online;
                        bootId = peer.bootId;
                        rssi = peer.rssi;
                        rttMs = peer.rttMs;
                        break;
                    }
                }
            }
        }

        // Combine the two positive signals (either means online), debounced on
        // the way down, sticky on `known`. Never a hard offline here: for a hub
        // child "connected" already means the dongle is relaying, so red is
        // reserved for the dongle link itself dropping (!device.connected).
        const HubChildPresence prev{device.peerOnline, device.peerPresenceKnown,
                                    m_hubChildOfflineTicks.value(device.id, 0)};
        const HubChildPresence v =
            hubChildPresence(dataFlowing, hubKnown, hubOnline, prev, kOfflineTicksToConfirm);
        m_hubChildOfflineTicks[device.id] = v.offlineTicks;
        const bool online = v.online;
        const bool known = v.known;

        if (online == device.peerOnline && known == device.peerPresenceKnown &&
            bootId == device.peerBootId && rssi == device.peerRssi && rttMs == device.peerRttMs) {
            continue;
        }
        // Announce only a genuine transition between two KNOWN states -- not the
        // first acquisition, and not "locating -> offline" (nothing was lost).
        if (known && device.peerPresenceKnown && online != device.peerOnline) {
            const QString name = device.name.isEmpty() ? tr("(unnamed)") : device.name;
            postStatus(online ? tr("%1: robot is responding again").arg(name)
                              : tr("%1: robot stopped responding (hub link still up)").arg(name),
                       5000, online ? StatusSeverity::Success : StatusSeverity::Warning, name);
        }
        m_devicesGrid->setDevicePeerState(device.id, online, known, bootId, rssi, rttMs);
        // The dashboard cells' own dot only knows connected/not -- give a hub
        // child's charts a live dot only while the robot's data is really
        // arriving, not merely while the cable to the dongle is in.
        m_dashboardGrid->setDeviceConnected(device.id, online);
        if (DeviceConnection* c = m_deviceConnections.value(device.id)) {
            c->backend()->onPeerPresence(online, bootId);
        }
        anyChanged = true;
    }
    if (anyChanged) {
        refreshDeviceStatusLabel();
    }
}

void MainWindow::onHubPeerFieldSample(const QString& deviceId, const TelemetryFieldBinding& binding,
                                      quint64 /*timestampUs*/, double value) {
    // Every device's Backend feeds this, so the first job is to drop the
    // overwhelming majority of samples: anything from a device with no
    // hub.peers watch, and anything from another topic on a device that has
    // one. That is a hash lookup per sample on devices that never become a
    // hub, which is why hooking this unconditionally in
    // createDeviceConnection() is affordable.
    const auto watchIt = m_hubPeerWatches.find(deviceId);
    if (watchIt == m_hubPeerWatches.end() || watchIt->handle == 0 ||
        binding.sourceId != watchIt->sourceId || binding.topicId != watchIt->topicId) {
        return;
    }
    watchIt->accumulator.append(binding.fieldId, binding.elementIndex, value);
}

void MainWindow::refreshDeviceStatusLabel() {
    if (!m_deviceStatusLabel) {
        return;
    }
    const QVector<Device> devices = m_devicesGrid->devices();
    if (devices.isEmpty()) {
        m_deviceStatusLabel->setText(tr("No devices configured"));
        return;
    }

    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    QStringList parts;
    for (const Device& device : devices) {
        // Same three states as the device card (topico 35 D.2): amber marks a
        // link that opened but has no BTP session -- "connected and mute".
        const DeviceLinkState linkState = deviceLinkState(device);
        const QColor dotColor = (linkState == DeviceLinkState::Live)      ? palette.success
                                : (linkState == DeviceLinkState::Offline) ? palette.danger
                                                                          : palette.warning;
        const QString name = device.name.isEmpty() ? tr("(unnamed)") : device.name;
        parts.append(QString("<span style='color:%1;'>&#9679;</span> %2")
                         .arg(dotColor.name(), name.toHtmlEscaped()));
    }
    m_deviceStatusLabel->setText(parts.join("&nbsp;&nbsp;&nbsp;&nbsp;"));
}

void MainWindow::applyDeviceTarget(DeviceConnection* connection, const Device& device) {
    if (connection == nullptr) {
        return;
    }
    if (m_loadingProject && !AppSettings::instance().autoConnectOnProjectOpen()) {
        return;
    }
    if (device.transportType == TransportType::HubChannel) {
        // A child addresses its robot by source_id, never by the channel
        // index the dongle publishes -- that index is a display label in
        // arrival order and is not stable across a dongle reboot (see
        // Device::peerSourceId). A parent that does not exist yet resolves to
        // nullptr here, which connectVia() reads as "not configured";
        // reattachHubChildren() runs again once it does exist.
        DeviceConnection* parent = m_deviceConnections.value(device.parentDeviceId);
        connection->connectVia(parent, hubChannelSourceId(device.id), device.peerSourceId,
                               deriveChannelKey(device.peerPassword));

        // Tell the hub where this child's downstream traffic goes. Nothing on
        // the wire can carry that: a BTP header has no destination field, and
        // TELEMETRY/TERMINAL carry none in their payload either, so the dongle
        // has to be told out of band (bally_dongle's HubRegistry). Until this
        // call existed it was a human typing "hub -bind" into the dongle's
        // console, and forgetting it did not produce an error -- a child's
        // COMMAND_REQUEST was executed ON THE DONGLE and its keystrokes typed
        // into the dongle's own shell, so a freshly added robot looked
        // connected while commanding the wrong machine.
        //
        // Issued on every applyDeviceTarget(), which is also every
        // reattachHubChildren() -- and HubBinder re-issues its whole table on
        // each new session on top of that, because the dongle's copy is
        // RAM-only.
        if (parent != nullptr) {
            if (auto* parentBackend = qobject_cast<BtpBackend*>(parent->backend())) {
                parentBackend->bindHubChild(hubChannelSourceId(device.id), device.peerSourceId);
            }
        }
        return;
    }
    if (device.transportType == TransportType::Tcp) {
        // Same channel-B password as a hub child (Device::peerPassword) --
        // the user's own decision: one password per device covers hub, TCP
        // and, later, BLE. See BtpBackend::setDirectEndpointKey()'s comment
        // for why this is NOT routed through setHubEndpoint()/connectVia().
        connection->connectToTcp(device.tcpHost, device.tcpPort,
                                 deriveChannelKey(device.peerPassword));
        return;
    }
    if (device.transportType == TransportType::Ble) {
        // Same one-password-per-device channel-B key as Tcp above
        // (Device::peerPassword). connectToBle() itself no-ops without
        // TRACEVIEW_ENABLE_BLE (no BleTransport exists in that
        // configuration -- see deviceconnection.cpp's constructor), so no
        // build-time guard is needed here.
        connection->connectToBle(device.bleAddress, deriveChannelKey(device.peerPassword));
        return;
    }
    const QString target =
        device.transportType == TransportType::UsbHid ? device.usbPath : device.portName;
    connection->connectTo(target, device.baudRate);
}

#ifdef TRACEVIEW_ENABLE_BLE
void MainWindow::onBleScanToggled(bool start) {
    if (!start) {
        // deleteLater(), not delete: this can run from inside a signal
        // handler chain started by the discovery agent itself (the dialog's
        // Scan button toggling off, or handleConfigRequested()'s
        // unconditional stop after exec() returns -- see its own comment).
        if (m_bleDiscovery != nullptr) {
            m_bleDiscovery->deleteLater();
            m_bleDiscovery = nullptr;
        }
        return;
    }

    // A fresh scan starts from a clean list -- see m_bleDiscoveredDevices's
    // own comment. Stale entries from a much earlier scan (a robot that's
    // since been powered off, moved out of range, or reflashed with a new
    // address) would otherwise sit in the combo indefinitely, offering a
    // pick that can no longer actually connect.
    m_bleDiscoveredDevices.clear();
    if (m_bleDiscovery == nullptr) {
        m_bleDiscovery = new BleDiscoveryService(this);
        connect(m_bleDiscovery, &BleDiscoveryService::deviceDiscovered, this,
                [this](const BleDiscoveryService::DiscoveredDevice& found) {
                    for (const auto& [name, address] : m_bleDiscoveredDevices) {
                        Q_UNUSED(name);
                        if (address == found.address) {
                            return;
                        }
                    }
                    m_bleDiscoveredDevices.append({found.name, found.address});
                });
        connect(
            m_bleDiscovery, &BleDiscoveryService::errorOccurred, this,
            [this](const QString& message) { postStatus(message, 6000, StatusSeverity::Warning); });
    }
    m_bleDiscovery->start();
}
#endif

void MainWindow::reattachHubChildren() {
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.transportType != TransportType::HubChannel) {
            continue;
        }
        DeviceConnection* connection = m_deviceConnections.value(device.id);
        if (connection != nullptr) {
            applyDeviceTarget(connection, device);
        }
    }
}

DeviceConnection* MainWindow::createDeviceConnection(const Device& device) {
    auto* connection = new DeviceConnection(device.commType, device.transportType, this);
    connect(
        connection, &DeviceConnection::connectionStateChanged, this,
        [this, id = device.id](bool connected) { onDeviceConnectionStateChanged(id, connected); });
    // This device's script runtime's onConnectionChange(), same lookup-by-id
    // pattern as the fieldSample/terminalDataReceived hooks below.
    connect(connection, &DeviceConnection::connectionStateChanged, this,
            [this, id = device.id](bool connected) {
                if (DiagramScriptRuntime* runtime = m_scriptRuntimes.value(id)) {
                    runtime->handleConnectionChange(connected);
                }
            });

    // Everything that gives this device's transport bytes meaning lives
    // behind the Backend interface (backend/backend.h) -- concretely a
    // BtpBackend today, owned internally by DeviceConnection. Hooked here,
    // once per device, the same way MainWindow used to hook the app's one
    // Backend before the multi-device refactor.
    Backend* backend = connection->backend();
    connect(
        backend, &Backend::statusMessage, this,
        [this, name = device.name](const QString& text, int timeoutMs, StatusSeverity severity) {
            postStatus(text, timeoutMs, severity, name);
        });
    // This device's script runtime's onStatus() -- the same statusMessage
    // signal the status bar uses, so a script sees session established/
    // failed, subscription rejections, and sendCommand() results (CommandClient
    // -> BtpBackend -> here) without a separate command-result channel.
    connect(
        backend, &Backend::statusMessage, this,
        [this, id = device.id](const QString& text, int /*timeoutMs*/, StatusSeverity severity) {
            if (DiagramScriptRuntime* runtime = m_scriptRuntimes.value(id)) {
                runtime->handleStatus(text, severity);
            }
        });
    // BTP traffic monitor taps -- every frame this device's session sends or
    // receives, and every decode failure, tagged with the device. A hub child
    // and its parent both report the child's relayed frames (once each,
    // labelled by device); acceptable for a raw inspector.
    if (auto* btp = qobject_cast<BtpBackend*>(backend)) {
        connect(btp, &BtpBackend::frameObserved, this,
                [this, id = device.id, name = device.name](FrameDirection direction,
                                                           const BtpFrame& frame) {
                    m_frameLog->recordFrame(direction, id, name, frame);
                });
        connect(btp, &BtpBackend::frameDecodeFailed, this,
                [this, id = device.id, name = device.name](const QString& reason) {
                    m_frameLog->recordDecodeError(id, name, reason);
                });
    }
    connect(backend, &Backend::catalogChanged, this, &MainWindow::updateSubscriptionsWorkspace);
    connect(backend, &Backend::subscriptionsChanged, this, &MainWindow::updateSubscriptionsWorkspace);
    connect(backend, &Backend::statusReceived, this, &MainWindow::updateSubscriptionsWorkspace);
    // A manifest exchange completing/updating is when catalogTopics() first
    // has (or changes) the readable names chart/gauge config editors resolve
    // sourceId/topicId against -- refresh their cached DeviceOption list
    // rather than only doing so on device add/remove/rename. Also tells
    // DevicesGrid so this device's config dialog, if it's open (e.g. right
    // after clicking Connect), picks up the catalog once it actually arrives
    // instead of only on next open -- see DevicesGrid::notifyCatalogChanged().
    connect(backend, &Backend::catalogChanged, this, [this, id = device.id]() {
        refreshPropertiesPanelDevices();
        m_devicesGrid->notifyCatalogChanged(id);
    });
    // setDeviceIdentity(), not updateDevice() -- same reasoning as
    // onDeviceConnectionStateChanged()'s setDeviceConnected() call below: this
    // is the handshake reporting live state, not a user edit, so it must not
    // land on m_devicesGrid's undo stack.
    connect(connection, &DeviceConnection::deviceIdentified, this,
            [this, id = device.id](const QString& btpVersion, const QString& btpId) {
                m_devicesGrid->setDeviceIdentity(id, btpVersion, btpId);
                // Direct BLE identity revalidation (TAREFAS_TCP_BLE_ANDROID.txt
                // T31: "nome/endereço são apenas pistas; nova sessão valida o
                // robô"). Re-fetched rather than using the `device` this
                // lambda closed over, which is a snapshot from when the
                // connection was created and would never see an id learned on
                // an earlier session. Learns silently the first time (empty
                // blePeerUuid); a DIFFERENT id than the one already known means
                // this address now answers for a different robot, which is
                // exactly what an operator needs surfaced rather than silently
                // accepted or silently overwritten.
                if (!btpId.isEmpty()) {
                    const QVector<Device> current = m_devicesGrid->devices();
                    const auto it = std::find_if(current.begin(), current.end(),
                                                 [&id](const Device& d) { return d.id == id; });
                    if (it != current.end() && it->transportType == TransportType::Ble) {
                        if (it->blePeerUuid.isEmpty()) {
                            m_devicesGrid->setDeviceBlePeerUuid(id, btpId);
                        } else if (it->blePeerUuid != btpId) {
                            postStatus(tr("%1: this BLE address now answers as a different robot "
                                          "(expected %2, got %3)")
                                           .arg(it->name.isEmpty() ? id : it->name, it->blePeerUuid,
                                                btpId),
                                       8000, StatusSeverity::Warning);
                        }
                    }
                }
                // The session coming up (or dropping, which clears the pair)
                // is an amber<->green transition for the status-bar dots too
                // (topico 35 D.2).
                refreshDeviceStatusLabel();
            });
    connect(connection, &DeviceConnection::deviceInfoReported, this,
            [this, id = device.id](const QVector<DeviceInfoRecord>& info) {
                m_devicesGrid->setDeviceReportedInfo(id, info);
            });
    // Previously nothing surfaced this at all: a transport that fails to
    // open (e.g. QSerialPort::open() PermissionError on Linux when the user
    // isn't in the dialout/uucp group) left Connect looking like a no-op.
    connect(connection, &DeviceConnection::errorOccurred, this,
            [this, name = device.name](const QString& text) {
                postStatus(text, 6000, StatusSeverity::Warning, name);
            });
    // This device's script runtime's onDeviceInfo().
    connect(connection, &DeviceConnection::deviceInfoReported, this,
            [this, id = device.id](const QVector<DeviceInfoRecord>& info) {
                if (DiagramScriptRuntime* runtime = m_scriptRuntimes.value(id)) {
                    runtime->handleDeviceInfo(info);
                }
            });
    // Hooked for every device, not just the ones that turn out to be hubs:
    // whether this device publishes hub.peers is only knowable once its
    // manifest arrives, which is long after this runs. The slot's first act
    // is to drop everything it isn't watching (see onHubPeerFieldSample) --
    // a hash lookup per sample on devices that never become a hub.
    //
    // Backend::fieldSample carries no device id (its binding's source_id is
    // the *robot's*, which for a hub child is not this device's id), so the
    // lambda supplies it.
    connect(backend, &Backend::fieldSample, this,
            [this, id = device.id](const TelemetryFieldBinding& binding, quint64 timestampUs,
                                   double value) {
                onHubPeerFieldSample(id, binding, timestampUs, value);
            });
    // This device's script runtime (m_scriptRuntimes, created alongside it in
    // onDeviceAdded): every telemetry sample and terminal byte the Backend
    // produces, for as long as its onTelemetry/onTerminal wants to react to
    // them.
    connect(backend, &Backend::fieldSample, this,
            [this, id = device.id](const TelemetryFieldBinding& binding, quint64 timestampUs,
                                   double value) {
                if (DiagramScriptRuntime* runtime = m_scriptRuntimes.value(id)) {
                    runtime->handleTelemetry(binding.topicId, binding.fieldId, binding.elementIndex,
                                             value, timestampUs);
                }
            });
    connect(backend, &Backend::terminalDataReceived, this,
            [this, id = device.id](const QByteArray& data) {
                if (DiagramScriptRuntime* runtime = m_scriptRuntimes.value(id)) {
                    runtime->handleTerminal(QString::fromUtf8(data));
                }
            });
    return connection;
}

void MainWindow::onDeviceAdded(const Device& device) {
    // One DiagramScriptRuntime per device, for its whole lifetime -- created
    // here (not lazily from onDeviceScriptRequested) so onTelemetry/
    // onTerminal start reacting immediately, before the operator has ever
    // opened the script editor. Seeded from the persisted script right away,
    // covering both a freshly added device (empty) and one loaded from a
    // project (whatever was saved).
    auto* runtime = new DiagramScriptRuntime(this);
    runtime->setScript(device.script);
    m_scriptRuntimes.insert(device.id, runtime);
    connect(runtime, &DiagramScriptRuntime::sendCommandRequested, this,
            [this, id = device.id](const QString& text) {
                if (DeviceConnection* target = m_deviceConnections.value(id)) {
                    target->backend()->sendCommand(text.toUtf8());
                }
            });
    connect(runtime, &DiagramScriptRuntime::sendTerminalRequested, this,
            [this, id = device.id](const QString& text) {
                if (DeviceConnection* target = m_deviceConnections.value(id)) {
                    target->backend()->sendTerminalIn(text.toUtf8());
                }
            });

    DeviceConnection* connection = createDeviceConnection(device);
    m_deviceConnections.insert(device.id, connection);
    connection->setLineTerminator(device.lineTerminator);
    // A no-op if `device` has no target yet (a freshly added placeholder,
    // see onAddDevice()) -- otherwise opens now, or starts the ambient retry
    // loop, e.g. right after loading a saved project whose devices already
    // have one configured. Target is portName for Serial, usbPath for
    // UsbHid (baudRate is ignored by DeviceConnection in that case).
    if (!m_loadingProject || AppSettings::instance().autoConnectOnProjectOpen()) {
        applyDeviceTarget(connection, device);
    }
    // A hub arriving may be the parent some already-loaded child was waiting
    // for, and a child arriving needs its own parent resolved -- one call
    // covers both directions.
    reattachHubChildren();
    refreshDeviceStatusLabel();
    refreshPropertiesPanelDevices();
    refreshOtaTabDevices();
}

void MainWindow::onDeviceRemoved(const QString& id) {
    // Before take(): releaseHubPeerWatch() unsubscribes through the
    // connection, so it has to still be reachable in the hash.
    releaseHubPeerWatch(id);
    m_hubChildOfflineTicks.remove(id);
    delete m_scriptRuntimes.take(id);
    DeviceConnection* connection = m_deviceConnections.take(id);
    updateSubscriptionsWorkspace();
    if (!connection) {
        return;
    }
    connection->disconnectFrom();
    connection->deleteLater();
    // DevicesGrid refuses to remove a device that still has children, so a
    // removal reaching here cannot orphan one. This still re-runs because a
    // removed CHILD leaves the hash one entry smaller and the remaining
    // children should be re-resolved against the current map rather than a
    // stale one.
    reattachHubChildren();
    refreshDeviceStatusLabel();
    refreshPropertiesPanelDevices();
    refreshOtaTabDevices();
}

void MainWindow::onDeviceUpdated(const Device& device) {
    DeviceConnection* connection = m_deviceConnections.value(device.id);
    if (!connection) {
        return;
    }
    if (connection->transportType() != device.transportType) {
        // DeviceConnection's Transport/Backend pair is fixed at construction
        // (deviceconnection.h) -- can't repoint a live SerialManager-backed
        // connection at a HID path or vice versa. Rebuild it in place, same
        // teardown/build steps onDeviceRemoved()/onDeviceAdded() use, so
        // every signal connection keyed by this device's id (properties
        // panel, control widgets, ...) keeps resolving through
        // m_deviceConnections without a remove/re-add round trip through
        // the undo stack.
        // The watch holds a handle into the SubscriptionManager of the
        // backend about to be destroyed, so it cannot outlive it. Dropped
        // here rather than repointed: hubPeersFor() re-resolves against the
        // new backend's catalog on its next poll anyway.
        releaseHubPeerWatch(device.id);
        connection->disconnectFrom();
        connection->deleteLater();
        connection = createDeviceConnection(device);
        m_deviceConnections.insert(device.id, connection);
    }
    connection->setLineTerminator(device.lineTerminator);
    applyDeviceTarget(connection, device);
    reattachHubChildren();
    refreshDeviceStatusLabel();
    // Renaming/reconfiguring a device changes what every widget's own
    // Device combo should show as its selected entry's label.
    refreshPropertiesPanelDevices();
    refreshOtaTabDevices();
}

void MainWindow::onDeviceConnectToggleRequested(const QString& deviceId) {
    DeviceConnection* connection = m_deviceConnections.value(deviceId);
    if (!connection) {
        return;
    }
    if (connection->wantsConnection()) {
        connection->disconnectFrom();
        return;
    }
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.id == deviceId) {
            applyDeviceTarget(connection, device);
            break;
        }
    }
}

void MainWindow::onDeviceConnectionStateChanged(const QString& deviceId, bool connected) {
    // For a hub child, "transport connected" is only "the dongle carries this
    // child's frames" -- it says nothing about the robot. Both dots (the
    // dashboard cells' and the Devices-tab card's) wait for
    // reconcileHubChildPresence(), called below on connect, to say the robot is
    // actually there; painting them green here would flash a false "live" on
    // every reconnect.
    bool isHubChild = false;
    for (const Device& d : m_devicesGrid->devices()) {
        if (d.id == deviceId) {
            isHubChild = d.transportType == TransportType::HubChannel;
            break;
        }
    }

    // Every chart/gauge/control/terminal cell currently configured for this
    // device (config()["deviceId"]) -- not just the Devices tab's own card.
    m_dashboardGrid->setDeviceConnected(deviceId, connected && !isHubChild);
    // setDeviceConnected(), not updateDevice() -- this fires from live
    // connection state (including DeviceConnection's own ambient retry
    // loop), not a user edit, so it must not land on m_devicesGrid's undo
    // stack (a connection blinking would otherwise show up as an undoable
    // "Edit Device" step, and Ctrl+Z on the Devices tab would undo a status
    // dot instead of an actual edit).
    m_devicesGrid->setDeviceConnected(deviceId, connected);
    if (!connected) {
        // A dropped connection invalidates whatever the last session's
        // HELLO_RESULT reported -- a fresh reconnect re-identifies via
        // deviceIdentified() (see onDeviceAdded()) once its own handshake
        // completes, but nothing should show a stale version/id meanwhile.
        m_devicesGrid->setDeviceIdentity(deviceId, QString(), QString());
        // Same for the device's reported source_info: a stale firmware version
        // from a previous session (or a previous device on this port) must not
        // linger. Refilled on the next manifest once reconnected.
        m_devicesGrid->setDeviceReportedInfo(deviceId, {});
        // Same for the presence verdict: back to "unknown" (amber "locating"),
        // NOT a fake "online" -- a reconnect must re-earn green from the robot's
        // frames actually arriving again, not assume it.
        m_devicesGrid->setDevicePeerState(deviceId, false, false, 0, 0, 0);
        m_hubChildOfflineTicks.remove(deviceId);
    } else {
        // A hub child coming up needs its parent's hub.peers watch, and the
        // reconcile that follows, promptly -- not on the next 1 s tick.
        syncHubPeerWatches();
        reconcileHubChildPresence();
    }
    refreshDeviceStatusLabel();
}

void MainWindow::onPanelTypeChangeRequested(const QString& typeId) {
    m_dashboardGrid->changeSelectedType(typeId);
}

void MainWindow::onPanelNameChangeRequested(const QString& name) {
    m_dashboardGrid->renameSelected(name);
}

void MainWindow::onPanelKeyChangeRequested(const QString& key) {
    if (!m_dashboardGrid->setSelectedKey(key)) {
        postStatus(tr("Key \"%1\" is already used by another widget.").arg(key), 4000,
                   StatusSeverity::Warning);
    }
    // Resyncs the field either way: on success to the committed value (a
    // no-op visually), on rejection to snap the text back to what's
    // actually stored instead of leaving the rejected input showing.
    refreshPropertiesPanel();
}

void MainWindow::onPanelConfigChangeRequested(const QJsonObject& config) {
    m_dashboardGrid->changeSelectedConfig(config);
}

void MainWindow::onNewProject() {
    if (DialogPresenter::question(
            this, tr("New Project"),
            tr("Discard the current dashboard and start a new, empty project?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    ProjectStore::instance().reset();
    WorkspaceManager::instance().reset();
    loadDashboardJson(QJsonObject(), DashboardLoadBreakpoint::DeviceDefault);
    m_dashboardGrid->undoStack()->clear();
    m_devicesGrid->fromJson(QJsonObject());
    m_devicesGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
    postStatus(tr("Started a new project."), 3000);
}

void MainWindow::onSaveProject() {
    WorkspaceManager::instance().setDashboardFor(WorkspaceManager::instance().activeId(),
                                                 m_dashboardGrid->toJson());
    ProjectStore::instance().setSection("workspaces", WorkspaceManager::instance().toJson());
    ProjectStore::instance().setSection("devices", m_devicesGrid->toJson());

    QString path = ProjectStore::instance().currentPath();
    if (path.isEmpty()) {
        onSaveProjectAs();
        return;
    }

    if (!ProjectStore::instance().save()) {
        DialogPresenter::warning(this, tr("Save Project"), ProjectStore::instance().lastError());
        return;
    }
    addRecentFile(path);
}

void MainWindow::onSaveProjectAs() {
    WorkspaceManager::instance().setDashboardFor(WorkspaceManager::instance().activeId(),
                                                 m_dashboardGrid->toJson());
    ProjectStore::instance().setSection("workspaces", WorkspaceManager::instance().toJson());
    ProjectStore::instance().setSection("devices", m_devicesGrid->toJson());

    const QString path =
        QFileDialog::getSaveFileName(this, tr("Save Project As"), QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }

    if (!ProjectStore::instance().saveAs(path)) {
        DialogPresenter::warning(this, tr("Save Project"), ProjectStore::instance().lastError());
        return;
    }
    addRecentFile(path);
}

void MainWindow::onOpenProject() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open Project"), QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }
    openRecentFile(path);
}

void MainWindow::onOpenLogFile() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Open Log File"), QString(), tr("BTP Log (*.blog)"));
    if (path.isEmpty()) {
        return;
    }

    auto* viewer = new LogViewer(this);
    viewer->openFile(path);
    m_contentStack->addWidget(viewer);

    // Empty -- a log tab carries no ribbon buttons of its own, this page
    // just exists to give the tab a slot in Ribbon's own internal stack (and
    // a stable pointer m_openLogTabs can be looked back up by, see its
    // declaration in mainwindow.h).
    auto* page = new QWidget(this);
    page->setObjectName("ribbonPage");
    page->setFixedHeight(kRibbonPageHeight);

    const int index = m_ribbon->addTab(QFileInfo(path).fileName(), page, /*enabled=*/true, path,
                                       /*closable=*/true);
    m_openLogTabs.append({page, viewer});
    // Triggers Ribbon::currentTabChanged -> onRibbonTabChanged, which is
    // what actually swaps m_contentStack over to `viewer` (via the lookup
    // above) and resets edit mode/panels/undo group the same way switching
    // to any other non-Layout/Devices tab does.
    m_ribbon->setCurrentIndex(index);
}

void MainWindow::onLogTabCloseRequested(int index) {
    QWidget* page = m_ribbon->pageAt(index);
    // Checked first, before either this function or the singleton-tab close
    // handlers mutate the ribbon -- see onOtaTabCloseRequested()'s comment in
    // mainwindow.h for why this can't just be a second connection on the same
    // signal.
    if (page != nullptr && page == m_otaTabPage) {
        onOtaTabCloseRequested(index);
        return;
    }
    if (page != nullptr && page == m_btpMonitorTabPage) {
        onBtpMonitorTabCloseRequested(index);
        return;
    }
    if (page != nullptr && page == m_settingsTabPage) {
        onSettingsTabCloseRequested(index);
        return;
    }
    for (int i = 0; i < m_openLogTabs.size(); ++i) {
        if (m_openLogTabs[i].ribbonPage != page) {
            continue;
        }
        LogViewer* viewer = m_openLogTabs[i].viewer;
        // Removed before removeRibbonTab() below, which -- if this tab is
        // the current one -- synchronously re-enters onRibbonTabChanged()
        // for whichever tab becomes current next; that lookup must not find
        // this entry anymore.
        m_openLogTabs.removeAt(i);
        removeRibbonTab(index);
        m_contentStack->removeWidget(viewer);
        viewer->deleteLater();
        break;
    }
}

void MainWindow::onOpenOtaTab() {
    if (m_otaTab != nullptr) {
        for (int i = 0; i < m_ribbon->count(); ++i) {
            if (m_ribbon->pageAt(i) == m_otaTabPage) {
                m_ribbon->setCurrentIndex(i);
                break;
            }
        }
        return;
    }

    m_otaTab = new OtaTab(this);
    m_otaTab->setDevices(m_devicesGrid->devices());
    connect(m_otaTab, &OtaTab::passwordCacheChanged, this, &MainWindow::onOtaPasswordCacheChanged);
    // Same actions the Devices tab's page uses (buildRibbon()) -- adding a
    // device works identically regardless of which tab is visible, and
    // "remove" is dispatched by onRemoveDeviceRequested() to whichever tab's
    // own selection is current.
    connect(m_otaTab, &OtaTab::selectionChanged, this, &MainWindow::updateDeviceSelectionActions);
    m_contentStack->addWidget(m_otaTab);

    // Same shape as devicesPage in buildRibbon() -- one button group with
    // the shared Add/Remove Device actions, left-aligned.
    auto* page = new QWidget(this);
    page->setObjectName("ribbonPage");
    page->setFixedHeight(kRibbonPageHeight);
    auto* pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH,
                                   kRibbonPageMarginV);
    pageLayout->setSpacing(kRibbonGroupSpacing);
    pageLayout->addWidget(
        Ribbon::createButtonGroup(page, {m_addDeviceAction, m_removeDeviceAction}));
    pageLayout->addStretch();

    const int index =
        m_ribbon->addTab(tr("OTA Update"), page, /*enabled=*/true, QString(), /*closable=*/true);
    m_otaTabPage = page;
    m_ribbon->setCurrentIndex(index);
}

void MainWindow::onOtaTabCloseRequested(int index) {
    if (m_otaTab == nullptr || m_ribbon->pageAt(index) != m_otaTabPage) {
        return;
    }
    removeRibbonTab(index);
    m_contentStack->removeWidget(m_otaTab);
    m_otaTab->deleteLater();
    m_otaTab = nullptr;
    m_otaTabPage = nullptr;
}

void MainWindow::onOpenBtpMonitor() {
    if (m_btpMonitorTab != nullptr) {
        for (int i = 0; i < m_ribbon->count(); ++i) {
            if (m_ribbon->pageAt(i) == m_btpMonitorTabPage) {
                m_ribbon->setCurrentIndex(i);
                break;
            }
        }
        return;
    }

    m_btpMonitorTab = new BtpMonitorTab(
        m_frameLog,
        [this](const QString& deviceId) -> QString {
            for (const Device& device : m_devicesGrid->devices()) {
                if (device.id == deviceId) {
                    return device.peerPassword;
                }
            }
            return QString();
        },
        this);
    m_contentStack->addWidget(m_btpMonitorTab);

    // An empty ribbon page: this tab carries no ribbon buttons of its own, the
    // page just gives it a slot in Ribbon's stack and a stable lookup key
    // (m_btpMonitorTabPage) -- same trick as m_openLogTabs/m_otaTabPage.
    auto* page = new QWidget(this);
    page->setObjectName("ribbonPage");
    page->setFixedHeight(kRibbonPageHeight);

    const int index = m_ribbon->addTab(tr("BTP Traffic"), page, /*enabled=*/true, QString(),
                                       /*closable=*/true);
    m_btpMonitorTabPage = page;
    m_ribbon->setCurrentIndex(index);
}

void MainWindow::onBtpMonitorTabCloseRequested(int index) {
    if (m_btpMonitorTab == nullptr || m_ribbon->pageAt(index) != m_btpMonitorTabPage) {
        return;
    }
    removeRibbonTab(index);
    m_contentStack->removeWidget(m_btpMonitorTab);
    m_btpMonitorTab->deleteLater();
    m_btpMonitorTab = nullptr;
    m_btpMonitorTabPage = nullptr;
}

void MainWindow::onOpenSettingsTab() {
    if (m_settingsTab != nullptr) {
        for (int i = 0; i < m_ribbon->count(); ++i) {
            if (m_ribbon->pageAt(i) == m_settingsTabPage) {
                m_ribbon->setCurrentIndex(i);
                break;
            }
        }
        return;
    }

    m_settingsTab = new SettingsPage(this);
    connect(m_settingsTab, &SettingsPage::clearRecentProjectsRequested, this,
            &MainWindow::onClearRecentFiles);
    connect(m_settingsTab, &SettingsPage::restartRequested, this, [] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    });
    connect(m_settingsTab, &SettingsPage::checkForUpdatesRequested, this,
            [this] { checkForUpdates(/*manual=*/true); });
    m_contentStack->addWidget(m_settingsTab);

    // An empty ribbon page: this tab carries no ribbon buttons of its own, the
    // page just gives it a slot in Ribbon's stack and a stable lookup key
    // (m_settingsTabPage) -- same trick as m_otaTabPage / m_btpMonitorTabPage.
    auto* page = new QWidget(this);
    page->setObjectName("ribbonPage");
    page->setFixedHeight(kRibbonPageHeight);

    const int index = m_ribbon->addTab(tr("Settings"), page, /*enabled=*/true, QString(),
                                       /*closable=*/true);
    m_settingsTabPage = page;
    m_ribbon->setCurrentIndex(index);
}

void MainWindow::onSettingsTabCloseRequested(int index) {
    if (m_settingsTab == nullptr || m_ribbon->pageAt(index) != m_settingsTabPage) {
        return;
    }
    removeRibbonTab(index);
    m_contentStack->removeWidget(m_settingsTab);
    m_settingsTab->deleteLater();
    m_settingsTab = nullptr;
    m_settingsTabPage = nullptr;
}

void MainWindow::onDeviceScriptRequested(const QString& deviceId) {
    DiagramScriptRuntime* runtime = m_scriptRuntimes.value(deviceId);
    if (runtime == nullptr) {
        return;
    }
    QString label = deviceId;
    QString currentScript;
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& device : devices) {
        if (device.id == deviceId) {
            label = device.name;
            currentScript = device.script;
            break;
        }
    }

    DiagramBlockConfigDialog dialog(label, currentScript, runtime, this);
    if (DialogPresenter::exec(dialog, DialogPresenter::Style::Page) != QDialog::Accepted) {
        return;
    }
    for (const Device& existing : devices) {
        if (existing.id != deviceId) {
            continue;
        }
        Device updated = existing;
        updated.script = dialog.script();
        m_devicesGrid->updateDevice(updated);
        break;
    }
}

void MainWindow::onShowNotificationHistory() {
    if (!m_notificationWindow) {
        m_notificationWindow = new NotificationHistoryWindow(m_notificationLog, this);
    }
    DialogPresenter::show(m_notificationWindow, DialogPresenter::Style::Page);
}

void MainWindow::onOtaPasswordCacheChanged(const QString& deviceId, const QString& password,
                                           bool cache) {
    const QVector<Device> devices = m_devicesGrid->devices();
    for (const Device& existing : devices) {
        if (existing.id != deviceId) {
            continue;
        }
        Device updated = existing;
        updated.otaPassword = password;
        updated.cacheOtaPassword = cache;
        m_devicesGrid->updateDevice(updated);
        break;
    }
}

void MainWindow::refreshOtaTabDevices() {
    if (m_otaTab != nullptr) {
        m_otaTab->setDevices(m_devicesGrid->devices());
    }
}

void MainWindow::openRecentFile(const QString& path) {
    if (!ProjectStore::instance().load(path)) {
        DialogPresenter::warning(this, tr("Open Project"), ProjectStore::instance().lastError());
        return;
    }

    const QJsonObject workspacesSection = ProjectStore::instance().section("workspaces");
    if (workspacesSection.isEmpty()) {
        // Older project file, predating workspaces -- migrate its single
        // "dashboard" section into a lone Default workspace.
        WorkspaceManager::instance().reset();
        WorkspaceManager::instance().setDashboardFor(WorkspaceManager::instance().activeId(),
                                                     ProjectStore::instance().section("dashboard"));
    } else {
        WorkspaceManager::instance().fromJson(workspacesSection);
    }

    // Devices load first: each chart/gauge widget resolves its "Device"
    // config against m_deviceConnections the moment it's created below, and
    // that resolution never gets retried once a device connects later (see
    // MainWindow::refreshWidgetSubscription) -- so a widget created before
    // its device exists is permanently stuck with no subscription.
    // Absent in projects saved before device persistence existed --
    // fromJson(QJsonObject()) on an empty section just clears the list.
    m_loadingProject = true;
    m_devicesGrid->fromJson(ProjectStore::instance().section("devices"));
    m_loadingProject = false;
    m_devicesGrid->undoStack()->clear();
    loadDashboardJson(
        WorkspaceManager::instance().dashboardFor(WorkspaceManager::instance().activeId()),
        DashboardLoadBreakpoint::DeviceDefault);
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    refreshLayersPanel();
    refreshWorkspaceSwitcher();
    addRecentFile(path);
}

void MainWindow::addRecentFile(const QString& path) {
    QSettings settings;
    QStringList files = settings.value(kRecentFilesSettingsKey).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > AppSettings::instance().recentProjectsLimit()) {
        files.removeLast();
    }
    settings.setValue(kRecentFilesSettingsKey, files);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu() {
    m_recentFilesMenu->clear();

    const QSettings settings;
    const QStringList files = settings.value(kRecentFilesSettingsKey).toStringList();
    if (files.isEmpty()) {
        QAction* emptyAction = m_recentFilesMenu->addAction(tr("(No Recent Projects)"));
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& path : files) {
        QAction* action = m_recentFilesMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path]() { openRecentFile(path); });
    }

    m_recentFilesMenu->addSeparator();
    connect(m_recentFilesMenu->addAction(tr("Clear Recent Projects")), &QAction::triggered, this,
            &MainWindow::onClearRecentFiles);
}

void MainWindow::onClearRecentFiles() {
    QSettings settings;
    settings.remove(kRecentFilesSettingsKey);
    updateRecentFilesMenu();
}

void MainWindow::onAbout() {
    AboutDialog dialog(this);
    DialogPresenter::exec(dialog, DialogPresenter::Style::Card);
}

void MainWindow::onShowKeyboardShortcuts() {
    using Row = ShortcutsDialog::Row;
    using Section = ShortcutsDialog::Section;

    // An action's live binding(s), joined for the few actions that carry more
    // than one (Redo), rendered in the platform's own notation.
    const auto keysOf = [](const QAction* action) {
        QStringList parts;
        const QList<QKeySequence> sequences = action->shortcuts();
        for (const QKeySequence& sequence : sequences) {
            parts << sequence.toString(QKeySequence::NativeText);
        }
        return parts.join(QStringLiteral("  ·  "));
    };
    const auto stdKeys = [](QKeySequence::StandardKey key) {
        return QKeySequence(key).toString(QKeySequence::NativeText);
    };
    const auto chord = [](Qt::KeyboardModifiers mods, Qt::Key key) {
        return QKeySequence(QKeyCombination(mods, key)).toString(QKeySequence::NativeText);
    };

    QVector<Section> sections;

    sections.append(Section{tr("Project"),
                            {Row{tr("New project"), stdKeys(QKeySequence::New)},
                             Row{tr("Open project"), stdKeys(QKeySequence::Open)},
                             Row{tr("Save project"), stdKeys(QKeySequence::Save)},
                             Row{tr("Save project as"), stdKeys(QKeySequence::SaveAs)},
                             Row{tr("Open log offline"), keysOf(m_openLogFileAction)},
                             Row{tr("Upload firmware (OTA)"), keysOf(m_openOtaTabAction)},
                             Row{tr("BTP traffic monitor"), keysOf(m_openBtpMonitorAction)},
                             Row{tr("Settings"), keysOf(m_openSettingsTabAction)}}});

    sections.append(Section{
        tr("Layout & widgets"),
        {Row{tr("Add widget"), keysOf(m_addWidgetAction)},
         Row{tr("Remove selected"), keysOf(m_removeAction)},
         Row{tr("Copy widget"), keysOf(m_copyAction)},
         Row{tr("Paste widget"), keysOf(m_pasteAction)},
         Row{tr("Bring forward / to front"),
             keysOf(m_bringForwardAction) + QStringLiteral("  ·  ") + keysOf(m_bringToFrontAction)},
         Row{tr("Send backward / to back"),
             keysOf(m_sendBackwardAction) + QStringLiteral("  ·  ") + keysOf(m_sendToBackAction)},
         Row{tr("Group / ungroup"),
             keysOf(m_groupAction) + QStringLiteral("  ·  ") + keysOf(m_ungroupAction)},
         Row{tr("Undo"), keysOf(m_undoAction)}, Row{tr("Redo"), keysOf(m_redoAction)}}});

    sections.append(Section{tr("Devices"),
                            {Row{tr("Add device"), keysOf(m_addDeviceAction)},
                             Row{tr("Remove device"), keysOf(m_removeDeviceAction)}}});

    sections.append(Section{
        tr("Serial monitor terminal"),
        {Row{tr("Switch device tab"), chord(Qt::ControlModifier, Qt::Key_Left) +
                                          QStringLiteral("  ·  ") +
                                          chord(Qt::ControlModifier, Qt::Key_Right)},
         Row{tr("Copy selection"), chord(Qt::ControlModifier, Qt::Key_C) + QStringLiteral("  ·  ") +
                                       chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_C)},
         Row{tr("Send interrupt (SIGINT)"),
             tr("%1 (nothing selected)").arg(chord(Qt::ControlModifier, Qt::Key_C))},
         Row{tr("Paste into terminal"),
             chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_V)}}});

    sections.append(Section{
        tr("Navigation & view"),
        {Row{tr("Run / Layout / Devices tab"),
             chord(Qt::ControlModifier, Qt::Key_1) + QStringLiteral(" · ") +
                 chord(Qt::ControlModifier, Qt::Key_2) + QStringLiteral(" · ") +
                 chord(Qt::ControlModifier, Qt::Key_3)},
         Row{tr("Next / previous workspace"),
             chord(Qt::ControlModifier, Qt::Key_Tab) + QStringLiteral("  ·  ") +
                 chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Tab)},
         Row{tr("Fullscreen dashboard"), chord(Qt::NoModifier, Qt::Key_F11)},
         Row{tr("Exit fullscreen"), chord(Qt::NoModifier, Qt::Key_Escape)},
         Row{tr("Notification history"), chord(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_H)},
         Row{tr("Keyboard shortcuts"), chord(Qt::NoModifier, Qt::Key_F1)}}});

    ShortcutsDialog dialog(sections, this);
    DialogPresenter::exec(dialog, DialogPresenter::Style::Page);
}

void MainWindow::onDonate() {
    DonateDialog dialog(this);
    DialogPresenter::exec(dialog, DialogPresenter::Style::Card);
}

void MainWindow::onDebug() {
    // Reuses the existing window (raised to the front) instead of stacking
    // up a new synthetic-data feed/timer every click -- m_debugChartsWindow
    // resets to null on its own once the user closes it (WA_DeleteOnClose +
    // QPointer, see mainwindow.h).
    if (!m_debugChartsWindow) {
        m_debugChartsWindow = new DebugChartsWindow(this);
    }
    DialogPresenter::show(m_debugChartsWindow, DialogPresenter::Style::Page);
}

void MainWindow::onFullscreenToggled(bool checked) {
    setUpdatesEnabled(false);
    if (checked) {
        // Qt saves both the normal geometry and the maximized state.
        // Capture before hiding chrome or entering fullscreen.
        m_preFullscreenGeometry = saveGeometry();
        m_wasMaximized = isMaximized();
        // menuBar()->hide() used to be unconditional here; now routed through
        // updateChromeVisibility() so it ANDs with compactChromeActive()
        // instead of fighting it (see that function's own comment) -- reads
        // m_fullscreenButton->isChecked(), which the toggled() signal this
        // slot handles has already updated to `checked` by this point.
        updateChromeVisibility();
        showFullScreen();
        m_fullscreenButton->setToolTip(tr("Exit fullscreen (F11 / Esc)"));
    } else {
        // Same reasoning as the hide() above, in reverse: this must NOT
        // unconditionally bring the menu bar back if compactChromeActive()
        // is why it was down in the first place (Android, or a Developer-
        // mode preview left running through the fullscreen toggle).
        updateChromeVisibility();
        // A single setWindowState() call transitions straight from
        // WindowFullScreen to the target state, so the Windows backend never
        // passes through the intermediate "normal" bounds that caused the
        // maximize flash.
        setWindowState(m_wasMaximized ? Qt::WindowMaximized : Qt::WindowNoState);
        restoreGeometry(m_preFullscreenGeometry);
        m_fullscreenButton->setToolTip(tr("Fullscreen dashboard (F11)"));
    }
    setUpdatesEnabled(true);
    updateRibbonIcons();
    // The transition just moved/resized the window without moveEvent
    // carrying floating panels along (see moveEvent()'s own comment). That's
    // the right call for the panels' own positions, but a panel that was
    // already near a screen edge can end up unreachable once the window
    // jumps -- and, entering fullscreen, sitting under the now-fullscreen
    // window. Deferred one turn so the window-state change has actually
    // landed and screen geometry reads true.
    QMetaObject::invokeMethod(
        this, [this] { m_dockController->clampFloatingPanelsToScreen(); },
        Qt::QueuedConnection);
}

void MainWindow::maybeCheckForUpdatesOnStartup() {
    AppSettings& settings = AppSettings::instance();
    if (!settings.updateAutoCheckEnabled()) {
        return;
    }
    checkForUpdates(/*manual=*/false);
}

void MainWindow::checkForUpdates(bool manual) {
    if (m_updateChecker->checkInFlight()) {
        return;
    }
    m_updateCheckWasManual = manual;
    m_updateChecker->checkForUpdates();
}

void MainWindow::onUpdateAvailable(const UpdateInfo& info) {
    AppSettings& settings = AppSettings::instance();
    settings.setUpdateLastCheckEpochMs(QDateTime::currentMSecsSinceEpoch());
    if (m_settingsTab != nullptr) {
        m_settingsTab->setUpdateStatusText(tr("Update available: v%1").arg(info.version));
    }

    // A silently-run startup check respects a version the user already
    // dismissed; a manual "Check now" click always shows the dialog again --
    // the user asked, so it's not a repeat interruption.
    if (!m_updateCheckWasManual && info.version == settings.updateSkippedVersion()) {
        return;
    }

    UpdateAvailableDialog dialog(info, this);
    connect(&dialog, &UpdateAvailableDialog::skipRequested, &settings,
            [info] { AppSettings::instance().setUpdateSkippedVersion(info.version); });
    connect(&dialog, &UpdateAvailableDialog::updateRequested, this,
            [this, info] { startUpdateDownload(info); });
    DialogPresenter::exec(dialog, DialogPresenter::Style::Card);
}

void MainWindow::onUpdateUpToDate() {
    AppSettings::instance().setUpdateLastCheckEpochMs(QDateTime::currentMSecsSinceEpoch());
    if (m_settingsTab != nullptr) {
        m_settingsTab->setUpdateStatusText(tr("Up to date (v%1)").arg(kVersion));
    }
    if (m_updateCheckWasManual) {
        postStatus(tr("TraceView is up to date."), 4000, StatusSeverity::Info);
    }
}

void MainWindow::onUpdateCheckFailed(const QString& reason) {
    // Keep the displayed last-check time tied to the last successful check.
    if (m_updateCheckWasManual) {
        postStatus(tr("Update check failed: %1").arg(reason), 5000, StatusSeverity::Warning);
    }
}

void MainWindow::startUpdateDownload(const UpdateInfo& info) {
#if defined(Q_OS_LINUX)
    if (qEnvironmentVariableIsEmpty("APPIMAGE")) {
        DialogPresenter::information(this, tr("Update"),
                                     tr("Automatic installation requires running an AppImage. "
                                        "Download the AppImage from the release page."));
        QDesktopServices::openUrl(info.releaseUrl);
        return;
    }
#endif
    if (!info.assetUrl.isValid()) {
        DialogPresenter::information(
            this, tr("Update"),
            tr("This release has no download for this platform. Opening the release "
               "page instead."));
        QDesktopServices::openUrl(info.releaseUrl);
        return;
    }
    if (!info.checksumsUrl.isValid()) {
        DialogPresenter::warning(
            this, tr("Update"),
            tr("This release has no SHA256SUMS.txt to verify the download against, so "
               "it can't be installed automatically. Opening the release page instead."));
        QDesktopServices::openUrl(info.releaseUrl);
        return;
    }
    postStatus(tr("Downloading TraceView %1...").arg(info.version), 4000);
    m_updateDownloader->download(info.assetUrl, info.assetName, info.checksumsUrl);
}

void MainWindow::onUpdateDownloadFinished(const QString& filePath) {
    QString reason;
    if (UpdateInstaller::install(filePath, &reason)) {
        QCoreApplication::quit();
        return;
    }
    DialogPresenter::warning(this, tr("Update"), reason);
}

void MainWindow::onUpdateDownloadFailed(const QString& reason) {
    DialogPresenter::warning(this, tr("Update"), reason);
}

}  // namespace traceview
