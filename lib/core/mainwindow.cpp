#include "mainwindow.h"

#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include "aboutdialog.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetregistry.h"
#include "project/projectstore.h"
#include "ribbon.h"
#include "traceview/thememanager.h"
#include "traceview/version.h"

namespace traceview {

namespace {
constexpr const char* kProjectFileFilter = "TraceView Project (*.tvproj)";
constexpr int kRibbonIconSize = 16;

// Flat, hand-drawn (not font-glyph) icons so they render crisply and
// consistently at small toolbar sizes, colored from the active theme.

QIcon makeGridIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);

    const int gap = 2;
    const int cellSize = (kRibbonIconSize - 3 * gap) / 2;
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            const int x = gap + col * (cellSize + gap);
            const int y = gap + row * (cellSize + gap);
            painter.drawRoundedRect(QRect(x, y, cellSize, cellSize), 1, 1);
        }
    }
    return QIcon(pixmap);
}

QIcon makePlusIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const int margin = kRibbonIconSize / 4;
    const int mid = kRibbonIconSize / 2;
    painter.drawLine(mid, margin, mid, kRibbonIconSize - margin);
    painter.drawLine(margin, mid, kRibbonIconSize - margin, mid);
    return QIcon(pixmap);
}
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
    m_configureAction = new QAction("Configure Layout", this);
    m_configureAction->setCheckable(true);
    connect(m_configureAction, &QAction::toggled, this, &MainWindow::onConfigureToggled);

    m_addWidgetAction = new QAction("Add Widget", this);
    m_addWidgetAction->setEnabled(false);
    connect(m_addWidgetAction, &QAction::triggered, this, &MainWindow::onAddWidget);

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    constexpr int kRibbonPageHeight = 30;

    auto* configurePage = new QWidget(this);
    configurePage->setFixedHeight(kRibbonPageHeight);
    auto* configureLayout = new QHBoxLayout(configurePage);
    configureLayout->setContentsMargins(6, 2, 6, 2);
    configureLayout->setSpacing(4);

    const auto makeToolButton = [configurePage](QAction* action) {
        auto* button = new QToolButton(configurePage);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
        return button;
    };
    configureLayout->addWidget(makeToolButton(m_configureAction));
    configureLayout->addWidget(makeToolButton(m_addWidgetAction));
    configureLayout->addStretch();

    auto* runPage = new QWidget(this);
    runPage->setFixedHeight(kRibbonPageHeight);

    auto* ribbon = new Ribbon(this);
    ribbon->addTab("Configure Project", configurePage);
    ribbon->addTab("Run", runPage, /*enabled=*/false, "Serial port configuration — coming soon");
    return ribbon;
}

void MainWindow::updateRibbonIcons() {
    const QColor color = ThemeManager::instance().currentTheme().textPrimary;
    m_configureAction->setIcon(makeGridIcon(color));
    m_addWidgetAction->setIcon(makePlusIcon(color));
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

void MainWindow::onAbout() {
    AboutDialog dialog(this);
    dialog.exec();
}

} // namespace traceview
