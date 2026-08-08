#include "mainwindow.h"

#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPolygon>
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

QIcon makeMinusIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const int margin = kRibbonIconSize / 4;
    const int mid = kRibbonIconSize / 2;
    painter.drawLine(margin, mid, kRibbonIconSize - margin, mid);
    return QIcon(pixmap);
}

QIcon makePencilIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPoint(3, kRibbonIconSize - 3), QPoint(kRibbonIconSize - 5, 5));

    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    QPolygon tip;
    tip << QPoint(kRibbonIconSize - 6, 3) << QPoint(kRibbonIconSize - 3, 3) << QPoint(kRibbonIconSize - 3, 6);
    painter.drawPolygon(tip);
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
    m_addWidgetAction = new QAction("Add", this);
    m_addWidgetAction->setEnabled(false);
    connect(m_addWidgetAction, &QAction::triggered, this, &MainWindow::onAddWidget);

    m_removeAction = new QAction("Remove", this);
    m_removeAction->setCheckable(true);
    m_removeAction->setEnabled(false);
    connect(m_removeAction, &QAction::toggled, this, &MainWindow::onRemoveModeToggled);

    m_editTypeAction = new QAction("Edit", this);
    m_editTypeAction->setCheckable(true);
    m_editTypeAction->setEnabled(false);
    connect(m_editTypeAction, &QAction::toggled, this, &MainWindow::onTypeEditModeToggled);

    connect(m_dashboardGrid, &DashboardGrid::widgetTypeEditRequested, this,
            &MainWindow::onWidgetTypeEditRequested);

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    constexpr int kRibbonPageHeight = 30;

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
    configureLayout->setSpacing(4);

    const auto makeToolButton = [configurePage](QAction* action) {
        auto* button = new QToolButton(configurePage);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
        return button;
    };
    configureLayout->addWidget(makeToolButton(m_addWidgetAction));
    configureLayout->addWidget(makeToolButton(m_removeAction));
    configureLayout->addWidget(makeToolButton(m_editTypeAction));
    configureLayout->addStretch();

    auto* ribbon = new Ribbon(this);
    ribbon->addTab("Run", runPage);
    m_configureTabIndex = ribbon->addTab("Configure Project", configurePage);

    connect(ribbon, &Ribbon::currentTabChanged, this, &MainWindow::onRibbonTabChanged);

    return ribbon;
}

void MainWindow::updateRibbonIcons() {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    m_addWidgetAction->setIcon(makePlusIcon(palette.textPrimary));
    m_removeAction->setIcon(makeMinusIcon(palette.danger));
    m_editTypeAction->setIcon(makePencilIcon(palette.accent));
}

void MainWindow::onRibbonTabChanged(int index) {
    const bool editable = index == m_configureTabIndex;
    m_dashboardGrid->setEditMode(editable);
    m_addWidgetAction->setEnabled(editable);
    m_removeAction->setEnabled(editable);
    m_editTypeAction->setEnabled(editable);
    if (!editable) {
        m_removeAction->setChecked(false);
        m_editTypeAction->setChecked(false);
    }
}

void MainWindow::onRemoveModeToggled(bool enabled) {
    if (enabled) {
        m_editTypeAction->setChecked(false);
    }
    m_dashboardGrid->setRemoveMode(enabled);
}

void MainWindow::onTypeEditModeToggled(bool enabled) {
    if (enabled) {
        m_removeAction->setChecked(false);
    }
    m_dashboardGrid->setTypeEditMode(enabled);
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

void MainWindow::onWidgetTypeEditRequested(const QString& itemId, const QString& currentTypeId) {
    QString typeId;
    if (pickWidgetType("Edit Widget Type", currentTypeId, &typeId)) {
        m_dashboardGrid->changeItemType(itemId, typeId);
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
