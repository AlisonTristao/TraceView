#include "mainwindow.h"

#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "project/projectstore.h"
#include "propertiespanel.h"
#include "ribbon.h"
#include "ribbonicons.h"
#include "serialdatarouter.h"
#include "serialmanager.h"
#include "serialwidgetbridge.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace traceview {

namespace {
constexpr const char* kProjectFileFilter = "TraceView Project (*.tvproj)";
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("TraceView v%1").arg(kVersion));
    resize(1024, 640);

    buildMenus();

    m_serialManager = new SerialManager(this);
    m_dashboardGrid = new DashboardGrid(this);
    // Routes decoded frames to widgets by key (BACKEND_TODO.txt Task 5); no
    // further interaction needed here once wired.
    new SerialDataRouter(m_serialManager, m_dashboardGrid, this);
    // Wires control-widget commands and the serial monitor's raw I/O to the
    // same connection (BACKEND_TODO.txt Tasks 9/10); no further interaction
    // needed here once wired.
    new SerialWidgetBridge(m_serialManager, m_dashboardGrid, this);

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

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* saveAction = fileMenu->addAction("&Save Project");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);
    auto* openAction = fileMenu->addAction("&Open Project");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenProject);

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

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

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

    runLayout->addWidget(m_portCombo);
    runLayout->addWidget(m_refreshPortsButton);
    runLayout->addWidget(m_baudCombo);
    runLayout->addWidget(m_lineTerminatorCombo);
    runLayout->addWidget(m_connectButton);
    runLayout->addStretch();

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

void MainWindow::onSaveProject() {
    ProjectStore::instance().setSection("dashboard", m_dashboardGrid->toJson());

    QString path = ProjectStore::instance().currentPath();
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, "Save Project", QString(), kProjectFileFilter);
        if (path.isEmpty()) {
            return;
        }
        if (!ProjectStore::instance().saveAs(path)) {
            QMessageBox::warning(this, "Save Project", ProjectStore::instance().lastError());
        }
        return;
    }

    if (!ProjectStore::instance().save()) {
        QMessageBox::warning(this, "Save Project", ProjectStore::instance().lastError());
    }
}

void MainWindow::onOpenProject() {
    const QString path = QFileDialog::getOpenFileName(this, "Open Project", QString(), kProjectFileFilter);
    if (path.isEmpty()) {
        return;
    }

    if (!ProjectStore::instance().load(path)) {
        QMessageBox::warning(this, "Open Project", ProjectStore::instance().lastError());
        return;
    }

    m_dashboardGrid->fromJson(ProjectStore::instance().section("dashboard"));
}

void MainWindow::onAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

} // namespace traceview
