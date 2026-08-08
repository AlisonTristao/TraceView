#include "mainwindow.h"

#include <QActionGroup>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QStringList>
#include <QToolBar>

#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "project/projectstore.h"
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
    buildToolbar();

    m_dashboardGrid = new DashboardGrid(this);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(m_dashboardGrid);
    setCentralWidget(scrollArea);
}

void MainWindow::buildMenus() {
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
}

void MainWindow::buildToolbar() {
    auto* toolbar = addToolBar("Dashboard");
    toolbar->setMovable(false);

    auto* configureAction = toolbar->addAction("Configure Layout");
    configureAction->setCheckable(true);
    connect(configureAction, &QAction::toggled, this, &MainWindow::onConfigureToggled);

    m_addWidgetAction = toolbar->addAction("Add Widget");
    m_addWidgetAction->setEnabled(false);
    connect(m_addWidgetAction, &QAction::triggered, this, &MainWindow::onAddWidget);

    toolbar->addSeparator();

    auto* saveAction = toolbar->addAction("Save Project");
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveProject);

    auto* openAction = toolbar->addAction("Open Project");
    connect(openAction, &QAction::triggered, this, &MainWindow::onOpenProject);
}

void MainWindow::onConfigureToggled(bool enabled) {
    m_dashboardGrid->setEditMode(enabled);
    m_addWidgetAction->setEnabled(enabled);
}

void MainWindow::onAddWidget() {
    QStringList displayNames;
    QStringList typeIds;
    for (const WidgetTypeInfo& info : WidgetRegistry::instance().availableTypes()) {
        displayNames << info.displayName;
        typeIds << info.typeId;
    }
    if (displayNames.isEmpty()) {
        return;
    }

    bool ok = false;
    const QString chosen = QInputDialog::getItem(this, "Add Widget", "Type:", displayNames, 0, false, &ok);
    if (!ok) {
        return;
    }

    const int index = displayNames.indexOf(chosen);
    if (index < 0) {
        return;
    }
    m_dashboardGrid->addItem(typeIds[index]);
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

} // namespace traceview
