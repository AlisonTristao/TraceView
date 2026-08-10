#include "mainwindow.h"

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "project/projectstore.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"
#include "protocol/telemetrycatalog.h"
#include "protocol/telemetryfieldrouter.h"
#include "propertiespanel.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "serialmanager.h"
#include "serialwidgetbridge.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace traceview {

namespace {
constexpr const char* kProjectFileFilter = "TraceView Project (*.tvproj)";
constexpr const char* kRecentFilesSettingsKey = "recentFiles/paths";
constexpr int kMaxRecentFiles = 10;
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("TraceView v%1").arg(kVersion));
    resize(1024, 640);

    buildMenus();

    m_serialManager = new SerialManager(this);
    m_dashboardGrid = new DashboardGrid(this);

    // BTP v1 client stack (topico 14): raw bytes -> BtpSession (COBS decode
    // + envelope/CRC validation + reassembly) -> ProtocolRouter (dispatch by
    // MessageType) -> TelemetryFieldRouter (schema decode, fan out by
    // field). m_telemetryCatalog is intentionally empty here -- there is no
    // manifest/discovery exchange yet (topico 16), so nothing assumes which
    // source_id is on the other end of the wire; wiring a chart/gauge
    // widget's own fieldSample subscription onto m_telemetryFieldRouter is
    // topico 15's job.
    m_btpSession = new BtpSession(this);
    m_protocolRouter = new ProtocolRouter(this);
    m_telemetryCatalog = new TelemetryCatalog();
    m_telemetryFieldRouter = new TelemetryFieldRouter(m_telemetryCatalog, this);
    connect(m_serialManager, &SerialManager::dataReceived, m_btpSession, &BtpSession::feedBytes);
    connect(m_btpSession, &BtpSession::bytesToWrite, m_serialManager, &SerialManager::write);
    connect(m_serialManager, &SerialManager::connectionStateChanged, this, [this](bool connected) {
        if (connected) {
            m_btpSession->reset();
        }
    });
    connect(m_btpSession, &BtpSession::frameReceived, m_protocolRouter, &ProtocolRouter::onFrameReceived);
    connect(m_protocolRouter, &ProtocolRouter::telemetrySampleReceived, m_telemetryFieldRouter,
            &TelemetryFieldRouter::onTelemetrySample);

    // Wires control-widget commands and the serial monitor/terminal to the
    // same connection (BACKEND_TODO.txt Tasks 9/10; terminal rewired onto
    // BTP TERMINAL_IN/OUT in topico 19); no further interaction needed here
    // once wired. Outbound control-widget commands still go straight to
    // SerialManager as raw literal text (docs/PROTOCOL.md "Outbound: control
    // commands") -- migrating them onto the BTP COMMAND channel is out of
    // scope for topicos 14/19 and left for a later one.
    new SerialWidgetBridge(m_serialManager, m_btpSession, m_protocolRouter, m_dashboardGrid, this);

    Ribbon* ribbon = buildRibbon();
    buildPropertiesPanel();

    // Canvas + properties panel side by side, both below the ribbon — so
    // the panel is exactly as tall as the canvas instead of a QDockWidget,
    // which spans the full window height (menu bar to status bar) and
    // would sit alongside the ribbon too, not just the canvas.
    auto* contentRow = new QWidget(this);
    auto* contentLayout = new QHBoxLayout(contentRow);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_dashboardGrid, /*stretch=*/1);
    contentLayout->addWidget(m_propertiesPanel);

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(ribbon);
    centralLayout->addWidget(contentRow, /*stretch=*/1);
    setCentralWidget(central);
}

MainWindow::~MainWindow() {
    delete m_telemetryCatalog;
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* newAction = fileMenu->addAction("&New Project");
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::onNewProject);

    auto* openAction = fileMenu->addAction("&Open Project...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenProject);

    m_recentFilesMenu = fileMenu->addMenu("Open &Recent");
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction("&Save Project");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);

    auto* saveAsAction = fileMenu->addAction("Save Project &As...");
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveProjectAs);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* themeMenu = viewMenu->addMenu("&Theme");

    auto* group = new QActionGroup(this);
    group->setExclusive(true);

    const QString currentId = ThemeManager::instance().currentTheme().id;
    for (const ThemePalette& palette : ThemeManager::instance().availableThemes()) {
        auto* action = themeMenu->addAction(palette.displayName);
        action->setCheckable(true);
        action->setChecked(palette.id == currentId);
        action->setData(palette.id);
        group->addAction(action);

        connect(action, &QAction::triggered, this, [id = palette.id]() {
            ThemeManager::instance().setTheme(id);
        });
    }

    auto* aboutAction = menuBar()->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

Ribbon* MainWindow::buildRibbon() {
    m_positionAction = new QAction("Position", this);
    m_positionAction->setCheckable(true);
    m_positionAction->setChecked(true);
    m_positionAction->setEnabled(false);
    connect(m_positionAction, &QAction::toggled, this, [this](bool checked) {
        if (!checked) {
            m_positionAction->setChecked(true); // selection is always the active tool while editing
        }
    });

    m_addWidgetAction = new QAction("Add", this);
    m_addWidgetAction->setEnabled(false);
    connect(m_addWidgetAction, &QAction::triggered, this, &MainWindow::onAddWidget);

    m_removeAction = new QAction("Remove", this);
    m_removeAction->setEnabled(false);
    m_removeAction->setShortcut(QKeySequence::Delete);
    connect(m_removeAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::removeSelected);

    m_copyAction = new QAction("Copy", this);
    m_copyAction->setEnabled(false);
    m_copyAction->setShortcut(QKeySequence::Copy);
    connect(m_copyAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::copySelected);

    m_pasteAction = new QAction("Paste", this);
    m_pasteAction->setEnabled(false);
    m_pasteAction->setShortcut(QKeySequence::Paste);
    connect(m_pasteAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::pasteItem);

    // createUndoAction()/createRedoAction() wire up triggered/enabled state
    // (and a dynamic "Undo <command text>" label) directly from the stack —
    // no manual canUndo()/canRedo() syncing needed.
    m_undoAction = m_dashboardGrid->undoStack()->createUndoAction(this, "Undo");
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction = m_dashboardGrid->undoStack()->createRedoAction(this, "Redo");
    m_redoAction->setShortcut(QKeySequence::Redo);

    connect(m_dashboardGrid, &DashboardGrid::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_dashboardGrid->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::refreshPropertiesPanel);
    // The clipboard can change from a copySelected() call here, or from
    // another window/app entirely — either way, m_pasteAction's enabled
    // state needs to stay in sync with whether it's currently pasteable.
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::updateSelectionActions);

    auto* runPage = new QWidget(this);
    runPage->setObjectName("ribbonPage");
    runPage->setFixedHeight(kRibbonPageHeight);
    auto* runLayout = new QHBoxLayout(runPage);
    runLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    runLayout->setSpacing(kRibbonGroupSpacing);

    m_portCombo = new QComboBox(runPage);
    m_portCombo->setToolTip("Serial port");
    m_portCombo->setMinimumWidth(110);

    m_refreshPortsButton = new QToolButton(runPage);
    m_refreshPortsButton->setText(QString::fromUtf8("\xE2\x9F\xB3")); // ⟳
    m_refreshPortsButton->setToolTip("Refresh port list");
    m_refreshPortsButton->setAutoRaise(true);
    m_refreshPortsButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    connect(m_refreshPortsButton, &QToolButton::clicked, this, &MainWindow::refreshSerialPorts);

    // Reuses the same baud list SerialMonitorWidget used to offer for its
    // now-removed per-widget connect bar (Tarefa 3) -- one global connection
    // means one place to pick the baud rate.
    m_baudCombo = new QComboBox(runPage);
    m_baudCombo->addItems({"9600", "19200", "38400", "57600", "115200"});
    m_baudCombo->setCurrentText("9600");
    m_baudCombo->setToolTip("Baud rate");

    // Terminator appended to control-widget commands only (docs/PROTOCOL.md
    // "Outbound: control commands", BACKEND_TODO.txt Task 9) -- unlike
    // port/baud this isn't a QSerialPort property, so it stays editable
    // while connected instead of being locked alongside them below.
    m_lineTerminatorCombo = new QComboBox(runPage);
    m_lineTerminatorCombo->addItem("None", int(LineTerminator::None));
    m_lineTerminatorCombo->addItem(QString::fromUtf8("LF (\\n)"), int(LineTerminator::Lf));
    m_lineTerminatorCombo->addItem(QString::fromUtf8("CR (\\r)"), int(LineTerminator::Cr));
    m_lineTerminatorCombo->addItem(QString::fromUtf8("CRLF (\\r\\n)"), int(LineTerminator::CrLf));
    m_lineTerminatorCombo->setCurrentIndex(1); // Lf, matching SerialManager's default
    m_lineTerminatorCombo->setToolTip("Line terminator appended to control-widget commands (push button/toggle/"
                                       "slider). Doesn't affect the serial terminal's raw keystrokes.");
    connect(m_lineTerminatorCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onLineTerminatorChanged);

    m_connectButton = new QPushButton("Connect", runPage);
    m_connectButton->setCheckable(true);
    connect(m_connectButton, &QPushButton::toggled, this, &MainWindow::onSerialConnectToggled);

    m_fullscreenButton = new QToolButton(runPage);
    m_fullscreenButton->setCheckable(true);
    m_fullscreenButton->setAutoRaise(true);
    m_fullscreenButton->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
    m_fullscreenButton->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
    m_fullscreenButton->setToolTip("Fullscreen dashboard (F11)");
    connect(m_fullscreenButton, &QToolButton::toggled, this, &MainWindow::onFullscreenToggled);

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

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    runLayout->addWidget(m_portCombo);
    runLayout->addWidget(m_refreshPortsButton);
    runLayout->addWidget(m_baudCombo);
    runLayout->addWidget(m_lineTerminatorCombo);
    runLayout->addWidget(m_connectButton);
    runLayout->addStretch();
    runLayout->addWidget(m_fullscreenButton);

    refreshSerialPorts();
    connect(m_serialManager, &SerialManager::connectionStateChanged, this,
            &MainWindow::onSerialConnectionStateChanged);
    connect(m_serialManager, &SerialManager::errorOccurred, this, &MainWindow::onSerialErrorOccurred);

    auto* configurePage = new QWidget(this);
    configurePage->setObjectName("ribbonPage");
    configurePage->setFixedHeight(kRibbonPageHeight);
    auto* configureLayout = new QHBoxLayout(configurePage);
    configureLayout->setContentsMargins(kRibbonPageMarginH, kRibbonPageMarginV, kRibbonPageMarginH, kRibbonPageMarginV);
    configureLayout->setSpacing(kRibbonGroupSpacing);

    configureLayout->addWidget(
        Ribbon::createButtonGroup(configurePage, {m_positionAction, m_addWidgetAction, m_removeAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_copyAction, m_pasteAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_undoAction, m_redoAction}));
    configureLayout->addStretch();

    auto* ribbon = new Ribbon(this);
    m_runTabIndex = ribbon->addTab("Run", runPage);
    m_configureTabIndex = ribbon->addTab("Layout", configurePage);

    connect(ribbon, &Ribbon::currentTabChanged, this, &MainWindow::onRibbonTabChanged);

    m_ribbon = ribbon;
    return ribbon;
}

void MainWindow::buildPropertiesPanel() {
    m_propertiesPanel = new PropertiesPanel(this);
    m_propertiesPanel->setAvailableTypes(WidgetRegistry::instance().availableTypes());

    connect(m_propertiesPanel, &PropertiesPanel::typeChangeRequested, this,
            &MainWindow::onPanelTypeChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::nameChangeRequested, this,
            &MainWindow::onPanelNameChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::keyChangeRequested, this, &MainWindow::onPanelKeyChangeRequested);
    connect(m_propertiesPanel, &PropertiesPanel::configChangeRequested, this,
            &MainWindow::onPanelConfigChangeRequested);

    // Only relevant while editing the layout — matches m_addWidgetAction/
    // m_positionAction, which also start disabled until the Layout tab is
    // active (see onRibbonTabChanged).
    m_propertiesPanel->hide();
}

void MainWindow::updateRibbonIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_positionAction->setIcon(makeSelectIcon(palette.textPrimary));
    m_positionAction->setToolTip("Position — select a widget to move/resize it");
    m_addWidgetAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addWidgetAction->setToolTip("Add widget");
    m_removeAction->setIcon(makeMinusIcon(palette.danger));
    m_removeAction->setToolTip(QString("Remove selected widget (%1)")
                                    .arg(m_removeAction->shortcut().toString(QKeySequence::NativeText)));
    m_copyAction->setIcon(makeCopyIcon(palette.textPrimary));
    m_copyAction->setToolTip(
        QString("Copy selected widget (%1)").arg(m_copyAction->shortcut().toString(QKeySequence::NativeText)));
    m_pasteAction->setIcon(makePasteIcon(palette.textPrimary));
    m_pasteAction->setToolTip(
        QString("Paste as a new widget (%1)").arg(m_pasteAction->shortcut().toString(QKeySequence::NativeText)));
    // No explicit setToolTip(): QAction falls back to text(), which
    // QUndoStack keeps updated with the pending command's description
    // (e.g. "Undo Move Widget"); the shortcut still shows up in the menu/
    // button via QAction::shortcut(), it's just not spelled out in the text.
    m_undoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/true));
    m_redoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/false));
    m_fullscreenButton->setIcon(makeFullscreenIcon(palette.textPrimary, m_fullscreenButton->isChecked()));
}

void MainWindow::onRibbonTabChanged(int index) {
    m_configureTabActive = index == m_configureTabIndex;
    m_dashboardGrid->setEditMode(m_configureTabActive);
    m_addWidgetAction->setEnabled(m_configureTabActive);
    m_positionAction->setEnabled(m_configureTabActive);
    m_propertiesPanel->setVisible(m_configureTabActive);
    updateSelectionActions();

    if (index == m_runTabIndex) {
        refreshSerialPorts();
    }
}

void MainWindow::refreshSerialPorts() {
    // Preserve the current pick across a refresh when it's still present --
    // rebuilding the list would otherwise silently reset it to whatever
    // QSerialPortInfo happens to return first.
    const QString current = m_portCombo->currentText();
    m_portCombo->clear();
    m_portCombo->addItems(m_serialManager->availablePorts());
    const int index = m_portCombo->findText(current);
    if (index >= 0) {
        m_portCombo->setCurrentIndex(index);
    }
}

void MainWindow::onSerialConnectToggled(bool checked) {
    if (checked) {
        if (!m_serialManager->open(m_portCombo->currentText(), m_baudCombo->currentText().toInt())) {
            // open() already reported the failure via errorOccurred(); just
            // snap the button back to reflect that the connection didn't
            // happen (re-enters this slot with checked=false, which calls
            // close() on an already-closed port -- a harmless no-op).
            m_connectButton->setChecked(false);
        }
        return;
    }
    m_serialManager->close();
}

void MainWindow::onSerialConnectionStateChanged(bool connected) {
    m_connectButton->setText(connected ? "Disconnect" : "Connect");
    {
        const QSignalBlocker blocker(m_connectButton);
        m_connectButton->setChecked(connected);
    }
    m_portCombo->setEnabled(!connected);
    m_refreshPortsButton->setEnabled(!connected);
    m_baudCombo->setEnabled(!connected);
}

void MainWindow::onSerialErrorOccurred(const QString& message) {
    statusBar()->showMessage(message, 5000);
}

void MainWindow::onLineTerminatorChanged(int) {
    m_serialManager->setLineTerminator(LineTerminator(m_lineTerminatorCombo->currentData().toInt()));
}

void MainWindow::onSelectionChanged(const QString&) {
    updateSelectionActions();
    refreshPropertiesPanel();
}

void MainWindow::updateSelectionActions() {
    const bool hasSelection = m_configureTabActive && !m_dashboardGrid->selectedItemId().isEmpty();
    m_removeAction->setEnabled(hasSelection);
    m_copyAction->setEnabled(hasSelection);
    m_pasteAction->setEnabled(m_configureTabActive && m_dashboardGrid->canPaste());
}

void MainWindow::refreshPropertiesPanel() {
    const bool hasSelection = !m_dashboardGrid->selectedItemId().isEmpty();
    m_propertiesPanel->setSelection(hasSelection, m_dashboardGrid->selectedItemTypeId(),
                                     m_dashboardGrid->selectedItemDisplayName(), m_dashboardGrid->selectedItemKey(),
                                     m_dashboardGrid->selectedItemConfig());
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

void MainWindow::onPanelTypeChangeRequested(const QString& typeId) {
    m_dashboardGrid->changeSelectedType(typeId);
}

void MainWindow::onPanelNameChangeRequested(const QString& name) {
    m_dashboardGrid->renameSelected(name);
}

void MainWindow::onPanelKeyChangeRequested(const QString& key) {
    if (!m_dashboardGrid->setSelectedKey(key)) {
        statusBar()->showMessage(QString("Key \"%1\" is already used by another widget.").arg(key), 4000);
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
    if (QMessageBox::question(this, "New Project",
                               "Discard the current dashboard and start a new, empty project?",
                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    ProjectStore::instance().reset();
    m_dashboardGrid->fromJson(QJsonObject());
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    statusBar()->showMessage("Started a new project.", 3000);
}

void MainWindow::onSaveProject() {
    ProjectStore::instance().setSection("dashboard", m_dashboardGrid->toJson());

    QString path = ProjectStore::instance().currentPath();
    if (path.isEmpty()) {
        onSaveProjectAs();
        return;
    }

    if (!ProjectStore::instance().save()) {
        QMessageBox::warning(this, "Save Project", ProjectStore::instance().lastError());
        return;
    }
    addRecentFile(path);
}

void MainWindow::onSaveProjectAs() {
    ProjectStore::instance().setSection("dashboard", m_dashboardGrid->toJson());

    const QString path = QFileDialog::getSaveFileName(this, "Save Project As", QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }

    if (!ProjectStore::instance().saveAs(path)) {
        QMessageBox::warning(this, "Save Project", ProjectStore::instance().lastError());
        return;
    }
    addRecentFile(path);
}

void MainWindow::onOpenProject() {
    const QString path = QFileDialog::getOpenFileName(this, "Open Project", QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }
    openRecentFile(path);
}

void MainWindow::openRecentFile(const QString& path) {
    if (!ProjectStore::instance().load(path)) {
        QMessageBox::warning(this, "Open Project", ProjectStore::instance().lastError());
        return;
    }

    m_dashboardGrid->fromJson(ProjectStore::instance().section("dashboard"));
    m_dashboardGrid->undoStack()->clear();
    refreshPropertiesPanel();
    addRecentFile(path);
}

void MainWindow::addRecentFile(const QString& path) {
    QSettings settings;
    QStringList files = settings.value(kRecentFilesSettingsKey).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecentFiles) {
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
        QAction* emptyAction = m_recentFilesMenu->addAction("(No Recent Projects)");
        emptyAction->setEnabled(false);
        return;
    }

    for (const QString& path : files) {
        QAction* action = m_recentFilesMenu->addAction(QFileInfo(path).fileName());
        action->setToolTip(path);
        connect(action, &QAction::triggered, this, [this, path]() { openRecentFile(path); });
    }

    m_recentFilesMenu->addSeparator();
    connect(m_recentFilesMenu->addAction("Clear Recent Projects"), &QAction::triggered, this,
            &MainWindow::onClearRecentFiles);
}

void MainWindow::onClearRecentFiles() {
    QSettings settings;
    settings.remove(kRecentFilesSettingsKey);
    updateRecentFilesMenu();
}

void MainWindow::onAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

void MainWindow::onFullscreenToggled(bool checked) {
    // On Windows this deliberately never touches Qt::WindowFullScreen /
    // showFullScreen() at all - confirmed via an A/B test (temporarily
    // routing through vanilla Qt showFullScreen()/showNormal()/
    // showMaximized() with none of the WinAPI code below) that the flicker
    // this function exists to avoid reproduces identically with plain Qt,
    // with zero custom code involved. That matches a long-standing,
    // still-open category of Qt-on-Windows bug (QTBUG-51093, QTBUG-47247,
    // years of forum reports, still reproducing on Qt6/Windows 11) in the
    // QPA windows backend's fullscreen transition - not something fixable
    // by reordering native calls layered on top of it, only by avoiding
    // that codepath entirely.
    //
    // Instead, "fullscreen" here just means: a borderless window resized to
    // exactly cover the monitor. That's the same "borderless windowed
    // fullscreen" idiom games and other native Windows apps use for the
    // same reason - to Windows it's an ordinary SetWindowPos, no different
    // from a user dragging the window's edge, so none of Qt's dedicated (and
    // apparently fragile) fullscreen state machine is ever engaged.
    if (checked) {
        // Brackets the native resize below AND the chrome hide() calls
        // further down in one suspended-repaint block: the native resize
        // alone already paints once (a borderless window at monitor size,
        // chrome still visible), and menuBar()->hide()/setTabBarVisible()
        // each schedule their own relayout/repaint on top of that - three
        // independent paints landing as three visible steps unless nothing
        // is allowed to paint until all of it is done.
        //
        // setUpdatesEnabled(false) alone doesn't achieve that on Windows: it
        // only suppresses Qt's own paint-event scheduling, not the WM_PAINT
        // that SetWindowPos(..., SWP_FRAMECHANGED) below forces synchronously
        // as part of the frame-style change, which lands (and is visible)
        // before menuBar()->hide()/setTabBarVisible() even run - that's the
        // "goes fullscreen with the menu still there, then the ribbon tabs
        // disappear a moment later" two-step. WM_SETREDRAW is the native
        // counterpart that actually blocks painting for this HWND at the
        // Win32 level regardless of source, so it's what suspends that
        // in-between frame; the RedrawWindow() call once chrome is hidden
        // forces the single final repaint everything was waiting for.
        setUpdatesEnabled(false);
#ifdef Q_OS_WIN
        // GetWindowPlacement(), not isMaximized()/GetWindowRect(): it hands
        // back Windows' own canonical "restore to" rectangle
        // (rcNormalPosition) together with the current show command in one
        // atomic snapshot. Recomputing that rectangle ourselves (e.g. from
        // GetWindowRect while maximized) doesn't work - it's the maximized
        // rect, not the rect the window should return to - and deriving our
        // own "was maximized" via a separate call/Qt's cached isMaximized()
        // can race or drift from what SetWindowPlacement() will restore.
        // Capturing the whole placement and replaying it verbatim on exit
        // keeps Windows' own restore bookkeeping intact, so a later manual
        // un-maximize (double-click title bar, Win+Down) still lands on the
        // right size instead of on a placement we half-guessed.
        HWND hwnd = reinterpret_cast<HWND>(winId());
        SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
        WINDOWPLACEMENT placement{};
        placement.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &placement);
        m_wasMaximized = placement.showCmd == SW_SHOWMAXIMIZED;
        const RECT& normalRect = placement.rcNormalPosition;
        m_preFullscreenGeometry = QRect(normalRect.left, normalRect.top, normalRect.right - normalRect.left,
                                         normalRect.bottom - normalRect.top);

        // Strip the frame and cover whichever monitor the window is
        // currently on, in one atomic SetWindowPos. MonitorFromWindow()/
        // GetMonitorInfoW() report physical pixel bounds - the same
        // coordinate space winId()'s HWND already operates in as a
        // per-monitor-DPI-aware window - so this lines up exactly with no
        // manual DPI math, unlike going through QScreen::geometry() (Qt's
        // logical/scaled coordinate space) would require. Plain
        // SetWindowPos never invokes Windows' min/max show animation (that
        // only triggers via ShowWindow's SW_MAXIMIZE/MINIMIZE/RESTORE
        // codes) so there's nothing to suppress here either - it's a single
        // ordinary resize.
        //
        // Deliberately NOT animated: an earlier version of this code
        // animated the grow/shrink via ~60 real SetWindowPos calls over
        // 200ms. That reintroduced two new problems the instant version
        // didn't have - visible stutter/stepping (each tick forces a full
        // relayout+repaint of the live dashboard, which doesn't reliably
        // fit inside one frame's budget) and, more seriously, a corrupted
        // taskbar after exiting (rapid resizes through the taskbar's screen
        // region appear to confuse Explorer's own fullscreen-window
        // detection). Both were absent with a single atomic resize, so this
        // stays a snap rather than a tween.
        LONG style = GetWindowLongW(hwnd, GWL_STYLE);
        style &= ~WS_OVERLAPPEDWINDOW;
        SetWindowLongW(hwnd, GWL_STYLE, style);

        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(MONITORINFO);
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
        const RECT& mr = monitorInfo.rcMonitor;
        SetWindowPos(hwnd, nullptr, mr.left, mr.top, mr.right - mr.left, mr.bottom - mr.top,
                     SWP_NOZORDER | SWP_FRAMECHANGED);
#else
        m_wasMaximized = isMaximized();
        m_preFullscreenGeometry = frameGeometry();
        showFullScreen();
#endif
        m_fullscreenButton->setToolTip("Exit fullscreen (F11 / Esc)");
        menuBar()->hide();
        m_ribbon->setTabBarVisible(false);
        SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        setUpdatesEnabled(true);
        updateRibbonIcons();
        return;
    }

    m_fullscreenButton->setToolTip("Fullscreen dashboard (F11)");
    // Same reasoning as the entry path: brackets the frame/placement
    // restore below and the chrome show() calls further down so nothing
    // paints until the window is at its final size AND its chrome is back,
    // instead of those landing as separate visible steps.
    setUpdatesEnabled(false);

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(winId());
    SendMessageW(hwnd, WM_SETREDRAW, FALSE, 0);
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style |= WS_OVERLAPPEDWINDOW;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    // SetWindowLongW's own docs: changing frame-related styles
    // (WS_OVERLAPPEDWINDOW includes WS_CAPTION/WS_THICKFRAME) only takes
    // effect once SetWindowPos is called with SWP_FRAMECHANGED. Without
    // this, Windows keeps computing the non-client area from the stale
    // borderless style, so the SW_SHOWMAXIMIZED placement below sizes the
    // window against the wrong frame metrics and its bottom edge ends up
    // extending under the taskbar. SWP_NOMOVE/SWP_NOSIZE keep this call from
    // moving/resizing anything itself - it exists purely to make the style
    // change above take effect before SetWindowPlacement runs.
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // Both the maximized and plain-restore cases go through
    // SetWindowPlacement rather than a plain SetWindowPos: rcNormalPosition
    // (what m_preFullscreenGeometry was captured from, on entry) is
    // documented to be in *workspace* coordinates - relative to the
    // monitor's work area, which excludes the taskbar - not screen
    // coordinates. GetWindowPlacement/SetWindowPlacement agree on that
    // convention between themselves, but a plain SetWindowPos expects
    // screen coordinates; feeding it workspace coordinates silently shifted
    // the restored window by the taskbar's thickness, leaving its bottom
    // edge hidden behind it. Going through SetWindowPlacement both ways
    // keeps the coordinate space consistent regardless of where the
    // taskbar is docked.
    //
    // Genuine maximize is additionally a distinct OS-tracked state (affects
    // Aero Snap, what double-click-titlebar toggles back to, etc.), not
    // just "resized to look like the maximized rect", so SW_SHOWMAXIMIZED
    // here isn't only about coordinates - it's the only way into that state
    // at all. Either showCmd goes through ShowWindow's codepath, which
    // re-invokes Windows' min/max show animation - suspend it for just this
    // call and restore the user's setting right after. There's no
    // multi-step sequence left for it to mask a gap in: the frame style
    // change above paints nothing by itself (see the entry path's
    // SetWindowLongW comment for why), so this placement call is the only
    // paint in the sequence.
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(WINDOWPLACEMENT);
    placement.showCmd = m_wasMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    placement.rcNormalPosition = RECT{m_preFullscreenGeometry.left(), m_preFullscreenGeometry.top(),
                                       m_preFullscreenGeometry.left() + m_preFullscreenGeometry.width(),
                                       m_preFullscreenGeometry.top() + m_preFullscreenGeometry.height()};

    ANIMATIONINFO animationInfo{};
    animationInfo.cbSize = sizeof(animationInfo);
    SystemParametersInfoW(SPI_GETANIMATION, sizeof(animationInfo), &animationInfo, 0);
    const int previousMinAnimate = animationInfo.iMinAnimate;
    animationInfo.iMinAnimate = 0;
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(animationInfo), &animationInfo, 0);

    SetWindowPlacement(hwnd, &placement);

    animationInfo.iMinAnimate = previousMinAnimate;
    SystemParametersInfoW(SPI_SETANIMATION, sizeof(animationInfo), &animationInfo, 0);
#else
    if (m_wasMaximized) {
        showMaximized();
    } else {
        showNormal();
        // m_preFullscreenGeometry is captured via frameGeometry() above, so
        // restore it the same way (frame included), not via setGeometry()
        // which would place just the client area at that rect.
        setFrameGeometry(m_preFullscreenGeometry);
    }
#endif
    menuBar()->show();
    m_ribbon->setTabBarVisible(true);
#ifdef Q_OS_WIN
    SendMessageW(hwnd, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
#endif
    setUpdatesEnabled(true);
    updateRibbonIcons();
}

} // namespace traceview
