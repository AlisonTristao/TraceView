#include "mainwindow.h"

#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStringList>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "project/projectstore.h"
#include "ribbon.h"
#include "ribbonicons.h"
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

    m_dashboardGrid = new DashboardGrid(this);

    Ribbon* ribbon = buildRibbon();

    auto* central = new QWidget(this);
    auto* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(ribbon);
    centralLayout->addWidget(m_dashboardGrid, /*stretch=*/1);
    setCentralWidget(central);
}

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* saveAction = fileMenu->addAction("&Save Project");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);
    auto* openAction = fileMenu->addAction("&Open Project");
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
    connect(m_removeAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::removeSelected);

    m_editTypeAction = new QAction("Edit", this);
    m_editTypeAction->setEnabled(false);
    connect(m_editTypeAction, &QAction::triggered, this, &MainWindow::onEditSelectedType);

    // createUndoAction()/createRedoAction() wire up triggered/enabled state
    // (and a dynamic "Undo <command text>" label) directly from the stack —
    // no manual canUndo()/canRedo() syncing needed.
    m_undoAction = m_dashboardGrid->undoStack()->createUndoAction(this, "Undo");
    m_redoAction = m_dashboardGrid->undoStack()->createRedoAction(this, "Redo");

    connect(m_dashboardGrid, &DashboardGrid::selectionChanged, this, &MainWindow::onSelectionChanged);

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    auto* runPage = new QWidget(this);
    runPage->setObjectName("ribbonPage");
    runPage->setFixedHeight(kRibbonPageHeight);
    auto* runLayout = new QHBoxLayout(runPage);
    runLayout->setContentsMargins(6, 2, 6, 2);
    auto* runLabel = new QLabel("Serial port configuration — coming soon", runPage);
    runLabel->setEnabled(false);
    runLayout->addWidget(runLabel);
    runLayout->addStretch();

    auto* configurePage = new QWidget(this);
    configurePage->setObjectName("ribbonPage");
    configurePage->setFixedHeight(kRibbonPageHeight);
    auto* configureLayout = new QHBoxLayout(configurePage);
    configureLayout->setContentsMargins(6, 2, 6, 2);
    configureLayout->setSpacing(6);

    configureLayout->addWidget(Ribbon::createButtonGroup(
        configurePage, {m_positionAction, m_addWidgetAction, m_removeAction, m_editTypeAction}));
    configureLayout->addWidget(Ribbon::createButtonGroup(configurePage, {m_undoAction, m_redoAction}));
    configureLayout->addStretch();

    auto* ribbon = new Ribbon(this);
    ribbon->addTab("Run", runPage);
    m_configureTabIndex = ribbon->addTab("Configure Project", configurePage);

    connect(ribbon, &Ribbon::currentTabChanged, this, &MainWindow::onRibbonTabChanged);

    return ribbon;
}

void MainWindow::updateRibbonIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_positionAction->setIcon(makeSelectIcon(palette.textPrimary));
    m_positionAction->setToolTip("Position — select a widget to move/resize it");
    m_addWidgetAction->setIcon(makePlusIcon(palette.textPrimary));
    m_addWidgetAction->setToolTip("Add widget");
    m_removeAction->setIcon(makeMinusIcon(palette.danger));
    m_removeAction->setToolTip("Remove selected widget");
    m_editTypeAction->setIcon(makePencilIcon(palette.accent));
    m_editTypeAction->setToolTip("Edit selected widget's type");
    // No explicit setToolTip(): QAction falls back to text(), which
    // QUndoStack keeps updated with the pending command's description
    // (e.g. "Undo Move Widget").
    m_undoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/true));
    m_redoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/false));
}

void MainWindow::onRibbonTabChanged(int index) {
    m_configureTabActive = index == m_configureTabIndex;
    m_dashboardGrid->setEditMode(m_configureTabActive);
    m_addWidgetAction->setEnabled(m_configureTabActive);
    m_positionAction->setEnabled(m_configureTabActive);
    updateSelectionActions();
}

void MainWindow::onSelectionChanged(const QString&) {
    updateSelectionActions();
}

void MainWindow::updateSelectionActions() {
    const bool enabled = m_configureTabActive && !m_dashboardGrid->selectedItemId().isEmpty();
    m_removeAction->setEnabled(enabled);
    m_editTypeAction->setEnabled(enabled);
}

bool MainWindow::pickWidgetType(const QString& title, const QString& preselectedTypeId, QString* outTypeId) {
    QStringList displayNames;
    QStringList typeIds;
    int preselectedIndex = 0;
    for (const WidgetTypeInfo& info : WidgetRegistry::instance().availableTypes()) {
        if (info.typeId == preselectedTypeId) {
            preselectedIndex = typeIds.size();
        }
        displayNames << info.displayName;
        typeIds << info.typeId;
    }
    if (displayNames.isEmpty()) {
        return false;
    }

    bool ok = false;
    const QString chosen =
        QInputDialog::getItem(this, title, "Type:", displayNames, preselectedIndex, false, &ok);
    if (!ok) {
        return false;
    }

    const int index = displayNames.indexOf(chosen);
    if (index < 0) {
        return false;
    }
    *outTypeId = typeIds[index];
    return true;
}

void MainWindow::onAddWidget() {
    QString typeId;
    if (pickWidgetType("Add Widget", QString(), &typeId)) {
        m_dashboardGrid->addItem(typeId);
    }
}

void MainWindow::onEditSelectedType() {
    QString typeId;
    if (pickWidgetType("Edit Widget Type", m_dashboardGrid->selectedItemTypeId(), &typeId)) {
        m_dashboardGrid->changeSelectedType(typeId);
    }
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
