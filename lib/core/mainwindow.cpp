#include "mainwindow.h"

#include <QActionGroup>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>

#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace traceview {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QString("TraceView v%1").arg(kVersion));
    resize(1024, 640);

    buildMenus();

    auto* placeholder = new QLabel(
        "TraceView\n\nTelemetry dashboard for line-following robots.\n"
        "Serial/ESP-NOW telemetry ingestion is not implemented yet.",
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
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

} // namespace traceview
