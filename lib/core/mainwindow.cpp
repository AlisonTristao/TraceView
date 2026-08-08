#include "mainwindow.h"

#include <QActionGroup>
#include <QFileDialog>
#include <QFrame>
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
#include <QSignalBlocker>
#include <QSpinBox>
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

QIcon makeSelectIcon(const QColor& color) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);

    const int m = 2;
    const int len = 4;
    const int n = kRibbonIconSize;
    painter.drawLine(m, m + len, m, m);
    painter.drawLine(m, m, m + len, m);
    painter.drawLine(n - m - len, m, n - m, m);
    painter.drawLine(n - m, m, n - m, m + len);
    painter.drawLine(m, n - m - len, m, n - m);
    painter.drawLine(m, n - m, m + len, n - m);
    painter.drawLine(n - m - len, n - m, n - m, n - m);
    painter.drawLine(n - m, n - m, n - m, n - m - len);
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

QIcon makeArrowIcon(const QColor& color, bool pointingLeft) {
    QPixmap pixmap(kRibbonIconSize, kRibbonIconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    const int margin = 3;
    QPolygon arrow;
    if (pointingLeft) {
        arrow << QPoint(kRibbonIconSize - margin, margin) << QPoint(margin, kRibbonIconSize / 2)
              << QPoint(kRibbonIconSize - margin, kRibbonIconSize - margin);
    } else {
        arrow << QPoint(margin, margin) << QPoint(kRibbonIconSize - margin, kRibbonIconSize / 2)
              << QPoint(margin, kRibbonIconSize - margin);
    }
    painter.drawPolyline(arrow);
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

    m_undoAction = new QAction("Undo", this);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::undo);

    m_redoAction = new QAction("Redo", this);
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, m_dashboardGrid, &DashboardGrid::redo);

    connect(m_dashboardGrid, &DashboardGrid::selectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_dashboardGrid, &DashboardGrid::historyChanged, this, &MainWindow::onHistoryChanged);

    updateRibbonIcons();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this](const ThemePalette&) { updateRibbonIcons(); });

    constexpr int kRibbonPageHeight = 40;

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

    constexpr int kRibbonButtonSize = 24;
    const auto makeToolButton = [](QWidget* parent, QAction* action) {
        auto* button = new QToolButton(parent);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(kRibbonIconSize, kRibbonIconSize));
        button->setFixedSize(kRibbonButtonSize, kRibbonButtonSize);
        return button;
    };
    const auto makeGroup = [](QWidget* parent, QHBoxLayout** outLayout) {
        auto* frame = new QFrame(parent);
        frame->setObjectName("ribbonGroup");
        frame->setFixedHeight(kRibbonPageHeight - 4); // matches configureLayout's 2px top/bottom margins
        auto* layout = new QHBoxLayout(frame);
        layout->setContentsMargins(3, 3, 3, 3); // symmetric padding around the buttons on all 4 sides
        layout->setSpacing(3);
        *outLayout = layout;
        return frame;
    };

    QHBoxLayout* toolsLayout = nullptr;
    auto* toolsGroup = makeGroup(configurePage, &toolsLayout);
    toolsLayout->addWidget(makeToolButton(toolsGroup, m_positionAction));
    toolsLayout->addWidget(makeToolButton(toolsGroup, m_addWidgetAction));
    toolsLayout->addWidget(makeToolButton(toolsGroup, m_removeAction));
    toolsLayout->addWidget(makeToolButton(toolsGroup, m_editTypeAction));

    QHBoxLayout* historyLayout = nullptr;
    auto* historyGroup = makeGroup(configurePage, &historyLayout);
    historyLayout->addWidget(makeToolButton(historyGroup, m_undoAction));
    historyLayout->addWidget(makeToolButton(historyGroup, m_redoAction));

    QHBoxLayout* precisionLayout = nullptr;
    auto* precisionGroup = makeGroup(configurePage, &precisionLayout);

    m_precisionSpin = new QSpinBox(precisionGroup);
    m_precisionSpin->setRange(1, 64);
    m_precisionSpin->setValue(m_dashboardGrid->precision());
    m_precisionSpin->setMinimumWidth(88); // generous on purpose — was clipping "64" before
    m_precisionSpin->setFixedHeight(kRibbonPageHeight - 12); // roomier than the icon buttons so the number isn't clipped
    m_precisionSpin->setToolTip("Grid precision — snap granularity for positioning/resizing (cells are always square)");

    precisionLayout->addWidget(new QLabel("Grid", precisionGroup));
    precisionLayout->addWidget(m_precisionSpin);

    connect(m_precisionSpin, &QSpinBox::valueChanged, m_dashboardGrid, &DashboardGrid::setPrecision);
    connect(m_dashboardGrid, &DashboardGrid::precisionChanged, this, [this](int precision) {
        const QSignalBlocker blockPrecision(m_precisionSpin);
        m_precisionSpin->setValue(precision);
    });

    configureLayout->addWidget(toolsGroup);
    configureLayout->addWidget(historyGroup);
    configureLayout->addWidget(precisionGroup);
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
    m_undoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/true));
    m_undoAction->setToolTip("Undo");
    m_redoAction->setIcon(makeArrowIcon(palette.textPrimary, /*pointingLeft=*/false));
    m_redoAction->setToolTip("Redo");
}

void MainWindow::onRibbonTabChanged(int index) {
    m_configureTabActive = index == m_configureTabIndex;
    m_dashboardGrid->setEditMode(m_configureTabActive);
    m_addWidgetAction->setEnabled(m_configureTabActive);
    m_positionAction->setEnabled(m_configureTabActive);
    m_precisionSpin->setEnabled(m_configureTabActive);
    updateSelectionActions();
}

void MainWindow::onSelectionChanged(const QString&) {
    updateSelectionActions();
}

void MainWindow::onHistoryChanged() {
    m_undoAction->setEnabled(m_dashboardGrid->canUndo());
    m_redoAction->setEnabled(m_dashboardGrid->canRedo());
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
