#include "labpanel.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "lab/code/codefiles.h"
#include "lab/control/codecontrolleradapter.h"
#include "lab/control/controllerhost.h"
#include "lab/control/labdevices.h"
#include "lab/module/blockpage.h"
#include "lab/module/codewindow.h"
#include "lab/module/loopdiagram.h"
#include "lab/module/labui.h"
#include "lab/module/plantpage.h"
#include "lab/module/simulationpage.h"
#include "lab/plant/labplantdevice.h"
#include "lab/scenes/diffdrivescene.h"
#include "lab/scenes/sceneregistry.h"
#include "lab/scenes/scenewidget.h"
#include "modules/appmodule.h"
#include "theme/iconutils.h"
#include "traceview/thememanager.h"

namespace tracelab {

using code::BlockKind;
using code::CodeLibrary;

namespace {

constexpr int kLiveIntervalMs = 250;
constexpr int kRibbonIconSize = 16;  // core/ribbonicons.h
// The menu down the left, as Settings' (lib/core/settingspage.cpp).
constexpr int kCategoryIconSize = 18;
constexpr int kCategoryRowHeight = 36;
constexpr int kFullCategoryListWidth = 186;
constexpr int kCompactCategoryListWidth = 56;
constexpr int kCompactLayoutMaxWidth = 700;
constexpr int kFullRootMargin = 18;
constexpr int kCompactRootMargin = 8;
constexpr int kFullBodySpacing = 18;
constexpr int kCompactBodySpacing = 8;
constexpr int kFullPageMarginH = 28;
constexpr int kFullPageMarginV = 24;
constexpr int kCompactPageMarginH = 10;
constexpr int kCompactPageMarginV = 12;
// The empty state, laid out like a Settings category page.
constexpr int kEmptyTextWidth = 640;

const char kPlantIcon[] = ":/tracelab/icons/plant.svg";
const char kFilterIcon[] = ":/tracelab/icons/filter.svg";
const char kControllerIcon[] = ":/tracelab/icons/controller.svg";

// The menu's glyphs, in Category order.
const char* const kCategoryIcons[] = {
    ":/tracelab/icons/overview.svg", ":/tracelab/icons/simulation.svg",
    ":/tracelab/icons/plant.svg",    ":/tracelab/icons/code.svg",
    ":/tracelab/icons/parameters.svg", ":/tracelab/icons/signals.svg",
    ":/tracelab/icons/scene.svg",
};

// The Code, Parameters and Signals pages' index in m_blockCategories.
int blockCategoryIndex(LabPanel::Category category) {
    return int(category) - int(LabPanel::Category::Code);
}

}  // namespace

LabPanel::LabPanel(traceview::ModuleHost* host, QWidget* parent)
    : QWidget(parent), m_host(host) {
    m_addPlantAction = new QAction(tr("Add plant"), this);
    m_addPlantAction->setToolTip(tr("Adds a simulated plant to this project's devices."));
    m_addControllerAction = new QAction(tr("Add controller"), this);
    m_addControllerAction->setToolTip(tr("Adds a controller written in C that closes the loop on "
                                         "the plant of this tab."));
    m_addSceneAction = new QAction(tr("Add scene"), this);
    m_addSceneAction->setToolTip(tr("Adds a Scene to the dashboard, bound to the plant and "
                                    "controller of this tab."));
    m_groupSeparator = new QAction(this);
    m_groupSeparator->setSeparator(true);
    m_playAction = new QAction(tr("Play"), this);
    m_pauseAction = new QAction(tr("Pause"), this);
    m_resetAction = new QAction(tr("Reset"), this);
    m_stepAction = new QAction(tr("Step"), this);
    connect(m_addPlantAction, &QAction::triggered, this, &LabPanel::addPlant);
    // Every kind of plant, from the button's menu (the button itself adds
    // the first).
    auto* plantMenu = new QMenu(this);
    connect(plantMenu, &QMenu::aboutToShow, this, [this, plantMenu] {
        plantMenu->clear();
        const QVector<LabDeviceRegistry::PlantType>& types =
            LabDeviceRegistry::instance().plantTypes();
        for (int i = 0; i < types.size(); ++i) {
            connect(plantMenu->addAction(types[i].name), &QAction::triggered, this,
                    [this, i] { addPlantOfType(i); });
        }
    });
    m_addPlantAction->setMenu(plantMenu);
    connect(m_addControllerAction, &QAction::triggered, this, &LabPanel::addController);
    connect(m_addSceneAction, &QAction::triggered, this, &LabPanel::addScene);
    connect(m_playAction, &QAction::triggered, this, &LabPanel::play);
    connect(m_pauseAction, &QAction::triggered, this, &LabPanel::pauseSimulation);
    connect(m_resetAction, &QAction::triggered, this, &LabPanel::resetSimulation);
    connect(m_stepAction, &QAction::triggered, this, &LabPanel::stepSimulation);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // No plant yet: the page reads like a Settings category (a title, what
    // it is for, what to do), not like an empty menu.
    m_emptyState = new QWidget(this);
    QVBoxLayout* emptyLayout = ui::categoryPage(
        m_emptyState, QStringLiteral("TraceLab"),
        tr("No plant in this project yet. Open a benchmark (the line follower or the Furuta "
           "pendulum), or build a loop of your own: a plant first, then the controller -- and "
           "filters between them, if you want -- written in C, with the code in front of you."));
    emptyLayout->itemAt(1)->widget()->setMaximumWidth(kEmptyTextWidth);
    auto* emptyButtons = new QHBoxLayout;
    auto* openBenchmark = new QPushButton(tr("Open the line-follower benchmark"), m_emptyState);
    connect(openBenchmark, &QPushButton::clicked, this, [this] {
        if (m_host) {
            m_host->openProject(lineFollowerProjectPath());
        }
    });
    auto* openFuruta = new QPushButton(tr("Open the Furuta pendulum benchmark"), m_emptyState);
    connect(openFuruta, &QPushButton::clicked, this, [this] {
        if (m_host) {
            m_host->openProject(furutaProjectPath());
        }
    });
    auto* addPlantButton = new QPushButton(m_addPlantAction->text(), m_emptyState);
    addPlantButton->setToolTip(m_addPlantAction->toolTip());
    addPlantButton->setMenu(m_addPlantAction->menu());
    emptyButtons->addWidget(openBenchmark);
    emptyButtons->addWidget(openFuruta);
    emptyButtons->addWidget(addPlantButton);
    emptyButtons->addStretch();
    emptyLayout->addLayout(emptyButtons);
    emptyLayout->addStretch(1);
    layout->addWidget(m_emptyState);

    // The menu and its pages.
    m_main = new QWidget(this);
    m_mainLayout = new QVBoxLayout(m_main);
    m_mainLayout->setContentsMargins(kFullRootMargin, kFullRootMargin, kFullRootMargin,
                                     kFullRootMargin);
    m_mainLayout->setSpacing(12);
    // More than one plant in the project: which one the pages are about.
    m_plantRow = new QWidget(m_main);
    auto* plantRowLayout = new QHBoxLayout(m_plantRow);
    plantRowLayout->setContentsMargins(0, 0, 0, 0);
    plantRowLayout->addWidget(ui::fieldLabel(tr("Plant"), m_plantRow));
    m_plantCombo = new QComboBox(m_plantRow);
    m_plantCombo->setMinimumWidth(200);
    plantRowLayout->addWidget(m_plantCombo);
    plantRowLayout->addStretch();
    m_mainLayout->addWidget(m_plantRow);

    m_bodyLayout = new QHBoxLayout;
    m_bodyLayout->setSpacing(kFullBodySpacing);
    buildMenu();
    m_bodyLayout->addWidget(m_categoryList, 0, Qt::AlignTop);
    m_pages = new QStackedWidget(m_main);
    m_bodyLayout->addWidget(m_pages, 1);
    m_mainLayout->addLayout(m_bodyLayout, 1);
    layout->addWidget(m_main);

    // The pages, in Category order.
    addPage(buildOverview());
    m_simulationPage = new SimulationPage({m_playAction, m_pauseAction, m_stepAction, m_resetAction});
    addPage(m_simulationPage);
    m_plantPage = new PlantPage;
    addPage(m_plantPage);
    addPage(buildBlockCategory(
        tr("Code"),
        tr("The code in C of a block of the loop: where it comes from, what the compiler said and "
           "what it wrote. It opens in VS Code or in a window of its own; saving it compiles it "
           "and the loop runs the new version."),
        BlockPage::CodePart, &m_blockCategories[0]));
    addPage(buildBlockCategory(
        tr("Parameters"),
        tr("The TL_PARAM values of a block, tuned while the loop runs."),
        BlockPage::ParametersPart, &m_blockCategories[1]));
    addPage(buildBlockCategory(
        tr("Signals"),
        tr("What a block can read -- the plant's outputs, the other blocks' results -- with "
           "their values now."),
        BlockPage::SignalsPart, &m_blockCategories[2]));
    addPage(buildScenes());

    connect(m_categoryList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            m_pages->setCurrentIndex(row);
            refreshLive();
        }
    });
    m_categoryList->setCurrentRow(0);

    connect(m_plantPage, &PlantPage::codeRequested, this, [this] {
        if (LabPlantDevice* p = plant(); p && p->codeBlocks()) {
            selectBlock(p->codeBlocks(), 0);
            showCategory(Category::Code);
        }
    });
    connect(m_blockCategories[0].page, &BlockPage::removeRequested, this, [this](int index) {
        if (LabControllerDevice* c = controller()) {
            c->removeFilter(index);
            rebuildChain();
            if (index > 0) {
                selectBlock(c, index - 1);
            } else if (LabPlantDevice* p = plant(); p && p->codeBlocks()) {
                selectBlock(p->codeBlocks(), 0);
            } else {
                selectBlock(c, LabControllerDevice::kController);
            }
        }
    });
    connect(m_plantCombo, qOverload<int>(&QComboBox::activated), this, [this] {
        m_chainSignature.clear();
        m_plantPage->setPlant(plant());
        m_simulationPage->setPlant(plant());
        rebuildChain();
    });
    connect(&LabDeviceRegistry::instance(), &LabDeviceRegistry::devicesChanged, this,
            &LabPanel::refreshDevices);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &LabPanel::applyTheme);

    auto* timer = new QTimer(this);
    timer->setInterval(kLiveIntervalMs);
    connect(timer, &QTimer::timeout, this, &LabPanel::refreshLive);
    timer->start();
    applyTheme();
    refreshDevices();
    applyCompactLayout(width() <= kCompactLayoutMaxWidth);
}

QList<QAction*> LabPanel::ribbonActions() const {
    return {m_addPlantAction, m_addControllerAction, m_addSceneAction, m_groupSeparator,
            m_playAction, m_pauseAction, m_resetAction, m_stepAction};
}

QString LabPanel::lineFollowerProjectPath() {
    return QStringLiteral(":/tracelab/linefollower.tvproj");
}

QString LabPanel::furutaProjectPath() {
    return QStringLiteral(":/tracelab/furuta.tvproj");
}

// ---------------------------------------------------------------------------
// The menu and its pages

void LabPanel::buildMenu() {
    m_categoryList = new QListWidget(m_main);
    m_categoryList->setObjectName(QStringLiteral("labMenu"));
    m_categoryList->setFixedWidth(kFullCategoryListWidth);
    m_categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryList->setIconSize(QSize(kCategoryIconSize, kCategoryIconSize));
    m_categoryList->setStyleSheet(QStringLiteral("QListWidget::item { padding: 7px 10px; }"));
    m_categoryList->setSpacing(3);
    m_categoryNames = {tr("Overview"), tr("Simulation"), tr("Plant"),  tr("Code"),
                       tr("Parameters"), tr("Signals"),  tr("Scenes")};
    for (const QString& name : std::as_const(m_categoryNames)) {
        auto* item = new QListWidgetItem(name, m_categoryList);
        item->setSizeHint(QSize(-1, kCategoryRowHeight));
        item->setToolTip(name);
    }
    fitCategoryListHeight();
}

void LabPanel::addPage(QWidget* page) {
    // Each page scrolls on its own, down only, as a Settings category.
    auto* scroll = new QScrollArea(m_pages);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    page->setAutoFillBackground(false);
    scroll->setWidget(page);
    m_pageLayouts.append(qobject_cast<QVBoxLayout*>(page->layout()));
    m_pages->addWidget(scroll);
}

QWidget* LabPanel::buildOverview() {
    auto* page = new QWidget;
    QVBoxLayout* layout = ui::categoryPage(
        page, tr("Overview"),
        tr("The loop of the plant, in order: the plant, the filters, the controller -- whose "
           "command goes back to the plant (loop.u). Click a block to see it; a \"+\" adds one."));

    auto* loop = new LabSection(tr("Loop"), page);
    auto* diagramScroll = new QScrollArea(loop);
    m_diagramScroll = diagramScroll;
    diagramScroll->setFrameShape(QFrame::NoFrame);
    diagramScroll->setWidgetResizable(false);
    diagramScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    diagramScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // As tall as the diagram (refreshDiagram()), never stretched.
    diagramScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    diagramScroll->viewport()->setAutoFillBackground(false);
    m_diagram = new LoopDiagram(diagramScroll);
    diagramScroll->setWidget(m_diagram);
    loop->body()->addWidget(diagramScroll);
    connect(m_diagram, &LoopDiagram::blockClicked, this, [this](int index) {
        if (index < 0 || index >= m_diagram->model().blocks.size()) {
            return;
        }
        const LoopDiagram::Block& block = m_diagram->model().blocks[index];
        switch (block.kind) {
            case LoopDiagram::Block::Plant:
                onCardClicked(CardKind::Plant, 0);
                break;
            case LoopDiagram::Block::Filter:
                onCardClicked(CardKind::Filter, block.index);
                break;
            case LoopDiagram::Block::Controller:
                onCardClicked(CardKind::Controller, 0);
                break;
            case LoopDiagram::Block::NoController:
                addController();
                break;
        }
    });
    // Blocks into the loop.
    auto* addRow = new QHBoxLayout;
    addRow->setSpacing(8);
    const auto addButton = [&](const QString& text, bool controllerBlock) {
        auto* button = new QToolButton(loop);
        button->setObjectName(QStringLiteral("labAddBlock"));
        button->setText(text);
        auto* menu = new QMenu(button);
        connect(menu, &QMenu::aboutToShow, this, [this, menu, controllerBlock] {
            menu->clear();
            fillBlockMenu(menu, controllerBlock);
        });
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        addRow->addWidget(button);
        return button;
    };
    m_addFilterButton = addButton(tr("+ Filter"), false);
    m_addControllerButton = addButton(tr("+ Controller"), true);
    addRow->addWidget(ui::note(tr("Point at a line: who writes it, who reads it. Click a block to "
                                  "open it."),
                               loop),
                      1);
    loop->body()->addLayout(addRow);
    layout->addWidget(loop);

    // A controller in C++ of the old projects, on this plant.
    m_legacySection = new LabSection(tr("Controller in C++"), page);
    m_legacyText = new QLabel(m_legacySection);
    m_legacyText->setWordWrap(true);
    m_legacyText->setMaximumWidth(kEmptyTextWidth);
    m_legacySection->body()->addWidget(m_legacyText);
    auto* removeLegacy = new QPushButton(tr("Remove it from the project"), m_legacySection);
    m_legacySection->body()->addWidget(removeLegacy, 0, Qt::AlignLeft);
    connect(removeLegacy, &QPushButton::clicked, this, [this] {
        if (m_host && !m_legacyId.isEmpty()) {
            m_host->removeModuleDevice(m_legacyId);
        }
    });
    layout->addWidget(m_legacySection);
    layout->addStretch(1);
    return page;
}

QWidget* LabPanel::buildBlockCategory(const QString& title, const QString& description, int part,
                                      BlockCategory* category) {
    auto* page = new QWidget;
    QVBoxLayout* layout = ui::categoryPage(page, title, description);

    // Which block, then its part.
    category->content = new QWidget(page);
    auto* contentLayout = new QVBoxLayout(category->content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(20);
    auto* blockRow = new QHBoxLayout;
    blockRow->setSpacing(12);
    blockRow->addWidget(ui::fieldLabel(tr("Block"), category->content));
    category->combo = new QComboBox(category->content);
    category->combo->setMinimumWidth(260);
    category->combo->setToolTip(tr("The block of the loop this page shows (the same on Code, "
                                   "Parameters and Signals)."));
    blockRow->addWidget(category->combo);
    if (part == BlockPage::CodePart) {
        // Another block into the loop, from here too.
        m_addBlockButton = new QPushButton(tr("Add a block"), category->content);
        auto* menu = new QMenu(m_addBlockButton);
        connect(menu, &QMenu::aboutToShow, this, [this, menu] {
            menu->clear();
            fillBlockMenu(menu->addMenu(tr("Filter")), false);
            LabControllerDevice* c = controller();
            const bool hasController = c && !c->spec(LabControllerDevice::kController).isEmpty();
            fillBlockMenu(menu->addMenu(hasController ? tr("Controller (replaces this one)")
                                                      : tr("Controller")),
                          true);
        });
        m_addBlockButton->setMenu(menu);
        blockRow->addWidget(m_addBlockButton);
    }
    blockRow->addStretch();
    contentLayout->addLayout(blockRow);
    category->page = new BlockPage(part, category->content);
    contentLayout->addWidget(category->page, 1);
    layout->addWidget(category->content, 1);
    connect(category->combo, qOverload<int>(&QComboBox::activated), this, [this](int index) {
        if (index >= 0 && index < m_blocks.size()) {
            selectBlock(m_blocks[index].owner, m_blocks[index].block);
        }
    });

    // No block in C: what would show here, and how to get one.
    category->none = new QWidget(page);
    auto* noneLayout = new QVBoxLayout(category->none);
    noneLayout->setContentsMargins(0, 0, 0, 0);
    noneLayout->setSpacing(12);
    auto* noneText = ui::note(tr("No block in C in this loop yet: the plant is built into "
                                 "TraceView. Add a controller written in C and it shows here."),
                              category->none);
    noneText->setMaximumWidth(kEmptyTextWidth);
    noneLayout->addWidget(noneText);
    auto* addController = new QPushButton(m_addControllerAction->text(), category->none);
    addController->setToolTip(m_addControllerAction->toolTip());
    connect(addController, &QPushButton::clicked, this, &LabPanel::addController);
    noneLayout->addWidget(addController, 0, Qt::AlignLeft);
    layout->addWidget(category->none);
    if (part != BlockPage::CodePart && part != BlockPage::SignalsPart) {
        layout->addStretch(1);
    }
    return page;
}

QWidget* LabPanel::buildScenes() {
    auto* page = new QWidget;
    QVBoxLayout* layout = ui::categoryPage(
        page, tr("Scenes"),
        tr("A scene animates the plant on the dashboard as it runs, bound by the signals' names "
           "to this plant and its controller (it can be bound to others in its settings)."));
    auto* section = new LabSection(tr("On the dashboard"), page);
    m_sceneText = new QLabel(section);
    m_sceneText->setWordWrap(true);
    m_sceneText->setMaximumWidth(kEmptyTextWidth);
    section->body()->addWidget(m_sceneText);
    m_addSceneButton = new QPushButton(tr("Add the scene to the dashboard"), section);
    ui::makePrimary(m_addSceneButton);
    connect(m_addSceneButton, &QPushButton::clicked, this, &LabPanel::addScene);
    section->body()->addWidget(m_addSceneButton, 0, Qt::AlignLeft);
    layout->addWidget(section);
    layout->addStretch(1);
    return page;
}

void LabPanel::showCategory(Category category) {
    m_categoryList->setCurrentRow(int(category));
}

LabPanel::Category LabPanel::currentCategory() const {
    return Category(qMax(0, m_categoryList->currentRow()));
}

CodeBlockOwner* LabPanel::currentBlockOwner() const {
    return m_blockIndex >= 0 && m_blockIndex < m_blocks.size() ? m_blocks[m_blockIndex].owner.data()
                                                                : nullptr;
}

int LabPanel::currentBlock() const {
    return m_blockIndex >= 0 && m_blockIndex < m_blocks.size() ? m_blocks[m_blockIndex].block : 0;
}

void LabPanel::applyTheme() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    // Notes, sections, the primary button, the "+" blocks dashed (labui.h).
    setStyleSheet(ui::styleSheet(palette));
    const auto icon = [&palette](const char* name) {
        return traceview::loadTintedIcon(
            QStringLiteral(":/tracelab/icons/%1.svg").arg(QLatin1String(name)),
            palette.textPrimary, kRibbonIconSize);
    };
    m_addPlantAction->setIcon(icon("plant"));
    m_addControllerAction->setIcon(icon("code"));
    m_addSceneAction->setIcon(icon("scene"));
    m_playAction->setIcon(icon("play"));
    m_pauseAction->setIcon(icon("pause"));
    m_resetAction->setIcon(icon("reset"));
    m_stepAction->setIcon(icon("step"));
    // The menu's glyphs: one for an idle row, one for the row the accent
    // fill sits behind (a QListWidget won't recolour a plain pixmap).
    for (int i = 0; i < m_categoryList->count() && i < int(std::size(kCategoryIcons)); ++i) {
        const QString path = QString::fromLatin1(kCategoryIcons[i]);
        QIcon itemIcon;
        itemIcon.addPixmap(traceview::loadTintedIcon(path, palette.textSecondary, kCategoryIconSize)
                               .pixmap(kCategoryIconSize, kCategoryIconSize),
                           QIcon::Normal);
        itemIcon.addPixmap(traceview::loadTintedIcon(path, palette.background, kCategoryIconSize)
                               .pixmap(kCategoryIconSize, kCategoryIconSize),
                           QIcon::Selected);
        m_categoryList->item(i)->setIcon(itemIcon);
    }
}

void LabPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    const bool compact = width() <= kCompactLayoutMaxWidth;
    if (compact != m_compact) {
        applyCompactLayout(compact);
    }
}

void LabPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Measured again once on screen, with the style sheet's frame in place.
    fitCategoryListHeight();
}

void LabPanel::fitCategoryListHeight() {
    m_categoryList->ensurePolished();
    const int rows = m_categoryList->count();
    const int spacing = m_categoryList->spacing();
    int contentHeight = rows * (kCategoryRowHeight + spacing) + spacing;
    if (rows > 0) {
        m_categoryList->doItemsLayout();
        m_categoryList->scrollToTop();
        const QRect lastRow = m_categoryList->visualItemRect(m_categoryList->item(rows - 1));
        if (lastRow.isValid()) {
            contentHeight = qMax(contentHeight, lastRow.bottom() + 1 + spacing);
        }
    }
    const QMargins margins = m_categoryList->contentsMargins();
    m_categoryList->setFixedHeight(contentHeight + margins.top() + margins.bottom() +
                                   2 * m_categoryList->frameWidth());
}

void LabPanel::applyCompactLayout(bool compact) {
    // As Settings on a phone: the menu down to its glyphs, the labels of a
    // form above their fields, the margins trimmed.
    m_compact = compact;
    m_categoryList->setFixedWidth(compact ? kCompactCategoryListWidth : kFullCategoryListWidth);
    for (int i = 0; i < m_categoryList->count() && i < m_categoryNames.size(); ++i) {
        m_categoryList->item(i)->setText(compact ? QString() : m_categoryNames.at(i));
    }
    const QFormLayout::RowWrapPolicy policy =
        compact ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows;
    for (QFormLayout* form : m_pages->findChildren<QFormLayout*>()) {
        form->setRowWrapPolicy(policy);
    }
    const int rootMargin = compact ? kCompactRootMargin : kFullRootMargin;
    m_mainLayout->setContentsMargins(rootMargin, rootMargin, rootMargin, rootMargin);
    m_bodyLayout->setSpacing(compact ? kCompactBodySpacing : kFullBodySpacing);
    const int pageMarginH = compact ? kCompactPageMarginH : kFullPageMarginH;
    const int pageMarginV = compact ? kCompactPageMarginV : kFullPageMarginV;
    for (QVBoxLayout* pageLayout : std::as_const(m_pageLayouts)) {
        if (pageLayout) {
            pageLayout->setContentsMargins(pageMarginH, pageMarginV, pageMarginH, pageMarginV);
            pageLayout->setSpacing(compact ? 12 : 20);
        }
    }
}

// ---------------------------------------------------------------------------
// Devices

QString LabPanel::plantId() const {
    const QString picked = m_plantCombo->currentData().toString();
    if (!picked.isEmpty() && LabDeviceRegistry::instance().plant(picked)) {
        return picked;
    }
    const QStringList ids = LabDeviceRegistry::instance().plantIds();
    return ids.isEmpty() ? QString() : ids.first();
}

LabPlantDevice* LabPanel::plant() const {
    return LabDeviceRegistry::instance().plant(plantId());
}

QString LabPanel::plantBenchmark() const {
    LabPlantDevice* p = plant();
    if (!p) {
        return QString();
    }
    CodeBlockOwner* blocks = p->codeBlocks();
    if (!blocks) {
        return QStringLiteral("linefollower");  // the line follower's C++ plant
    }
    const CodeBlockSpec spec = blocks->spec(0);
    QString id = spec.templateId;
    if (id.isEmpty() && !spec.path.isEmpty()) {
        id = CodeLibrary::originOf(CodeLibrary::readFile(spec.path));
    }
    return id.section(QLatin1Char('/'), 0, 0);
}

QString LabPanel::sceneType() const {
    const QString benchmark = plantBenchmark();
    return SceneRegistry::instance().contains(benchmark) ? benchmark
                                                         : QString::fromLatin1(DiffDriveScene::kTypeId);
}

LabControllerDevice* LabPanel::controller() const {
    LabDeviceRegistry& registry = LabDeviceRegistry::instance();
    const QStringList ids = registry.controllersOf(plantId());
    if (!ids.isEmpty()) {
        return registry.controller(ids.first());
    }
    // Not linked yet (the plant just came up): the one configured for it.
    for (const QString& id : registry.controllerIds()) {
        LabControllerDevice* c = registry.controller(id);
        if (c && c->plantDeviceId() == plantId()) {
            return c;
        }
    }
    return nullptr;
}

void LabPanel::persistDevices() {
    if (!m_host) {
        return;
    }
    LabDeviceRegistry& registry = LabDeviceRegistry::instance();
    for (const QString& id : registry.plantIds()) {
        LabPlantDevice* p = registry.plant(id);
        if (p && !m_persisted.contains(p)) {
            m_persisted.insert(p);
            connect(p, &LabPlantDevice::settingsChanged, this, [this, p] {
                const QString deviceId = LabDeviceRegistry::instance().idOf(p);
                if (!deviceId.isEmpty()) {
                    m_host->updateModuleDeviceConfig(deviceId, p->config());
                }
            });
            connect(p, &QObject::destroyed, this, [this, p] { m_persisted.remove(p); });
        }
    }
    for (const QString& id : registry.controllerIds()) {
        LabControllerDevice* c = registry.controller(id);
        if (c && !m_persisted.contains(c)) {
            m_persisted.insert(c);
            connect(c, &LabControllerDevice::configChanged, this, [this, c] {
                m_host->updateModuleDeviceConfig(c->deviceId(), c->config());
            });
            connect(c, &LabControllerDevice::blocksChanged, this, [this] { rebuildChain(); });
            connect(c, &QObject::destroyed, this, [this, c] { m_persisted.remove(c); });
        }
    }
}

void LabPanel::refreshDevices() {
    LabDeviceRegistry& registry = LabDeviceRegistry::instance();
    persistDevices();
    const QString current = m_plantCombo->currentData().toString();
    {
        const QSignalBlocker blocker(m_plantCombo);
        m_plantCombo->clear();
        for (const QString& id : registry.plantIds()) {
            LabPlantDevice* p = registry.plant(id);
            m_plantCombo->addItem(QStringLiteral("%1 (%2)").arg(p->typeName(), id), id);
        }
        const int index = m_plantCombo->findData(current);
        m_plantCombo->setCurrentIndex(index >= 0 ? index : (m_plantCombo->count() > 0 ? 0 : -1));
    }
    m_plantRow->setVisible(m_plantCombo->count() > 1);
    const bool any = m_plantCombo->count() > 0;
    m_emptyState->setVisible(!any);
    m_main->setVisible(any);
    m_addControllerAction->setEnabled(any);
    m_addSceneAction->setEnabled(any);
    m_addPlantAction->setEnabled(!registry.plantTypes().isEmpty());
    if (m_plantPage->plant() != plant()) {
        m_plantPage->setPlant(plant());
    }
    if (m_simulationPage->plant() != plant()) {
        m_simulationPage->setPlant(plant());
    }

    rebuildChain();
    // A controller device just asked for is live now: show its block.
    if (!m_pendingController.isEmpty() && registry.controller(m_pendingController)) {
        m_pendingController.clear();
        selectBlock(controller(), m_pendingPick);
        showCategory(Category::Code);
    }
}

// ---------------------------------------------------------------------------
// The loop

void LabPanel::fillBlockMenu(QMenu* menu, bool controllerBlock) {
    const BlockKind kind = controllerBlock ? BlockKind::Controller : BlockKind::Filter;
    // The plant's benchmark's templates first, then the other benchmarks'.
    const QString benchmark = plantBenchmark();
    QVector<code::CodeTemplate> own, others;
    for (const code::CodeTemplate& t : CodeLibrary::instance().templates(kind)) {
        const bool mine = benchmark.isEmpty() || t.id.section(QLatin1Char('/'), 0, 0) == benchmark;
        (mine ? own : others).append(t);
    }
    const auto addTemplates = [&](const QString& section, const QVector<code::CodeTemplate>& list) {
        if (list.isEmpty()) {
            return;
        }
        menu->addSection(section);
        for (const code::CodeTemplate& t : list) {
            QAction* action = menu->addAction(t.name);
            action->setToolTip(t.description);
            const QString id = t.id;
            connect(action, &QAction::triggered, this,
                    [this, controllerBlock, id] { addBlock(controllerBlock, id, QString()); });
        }
    };
    addTemplates(tr("Templates"), own);
    addTemplates(tr("Other benchmarks"), others);
    const QStringList files = CodeLibrary::userFiles(kind);
    if (!files.isEmpty()) {
        menu->addSection(tr("Your files"));
        for (const QString& path : files) {
            QAction* action = menu->addAction(QFileInfo(path).fileName());
            action->setToolTip(QDir::toNativeSeparators(path));
            connect(action, &QAction::triggered, this,
                    [this, controllerBlock, path] { addBlock(controllerBlock, QString(), path); });
        }
    }
    menu->addSeparator();
    connect(menu->addAction(tr("New (empty)...")), &QAction::triggered, this,
            [this, controllerBlock, kind] {
                bool ok = false;
                const QString name = QInputDialog::getText(
                    this, controllerBlock ? tr("New controller") : tr("New filter"),
                    tr("File name (saved in %1):")
                        .arg(QDir::toNativeSeparators(CodeLibrary::userDirectory(kind))),
                    QLineEdit::Normal,
                    controllerBlock ? QStringLiteral("my_controller") : QStringLiteral("my_filter"),
                    &ok);
                if (!ok || name.trimmed().isEmpty()) {
                    return;
                }
                QString error;
                const QString path =
                    CodeLibrary::instance().createFile(kind, QString(), name.trimmed(), &error);
                if (path.isEmpty()) {
                    QMessageBox::warning(this, tr("New block"), error);
                    return;
                }
                addBlock(controllerBlock, QString(), path);
            });
    connect(menu->addAction(tr("Open a file...")), &QAction::triggered, this,
            [this, controllerBlock, kind] {
                const QString path = QFileDialog::getOpenFileName(
                    this, tr("Open a block"), CodeLibrary::userDirectory(kind),
                    tr("C files (*.c);;All files (*)"));
                if (!path.isEmpty()) {
                    addBlock(controllerBlock, QString(), path);
                }
            });
}

void LabPanel::addBlock(bool controllerBlock, const QString& templateId, const QString& path) {
    // A new controller replaces the code an open window may be editing.
    if (CodeWindow* window = CodeWindow::find(controller(), LabControllerDevice::kController);
        controllerBlock && window && !window->maybeSave()) {
        return;
    }
    CodeBlockSpec spec;
    spec.templateId = templateId;
    spec.path = path;
    if (LabControllerDevice* c = controller()) {
        if (controllerBlock) {
            c->setSpec(LabControllerDevice::kController, spec);
        } else {
            c->addFilter(spec);
        }
        rebuildChain();
        selectBlock(c, controllerBlock ? LabControllerDevice::kController : c->filterCount() - 1);
        showCategory(Category::Code);
        return;
    }
    QJsonObject config{{"plantDeviceId", plantId()}};
    if (controllerBlock) {
        config.insert(QStringLiteral("controller"), spec.toJson());
    } else {
        QJsonArray filters;
        filters.append(spec.toJson());
        config.insert(QStringLiteral("filters"), filters);
    }
    addControllerDevice(config, controllerBlock ? LabControllerDevice::kController : 0);
}

void LabPanel::addControllerDevice(const QJsonObject& config, int pickAfter) {
    if (!m_host) {
        return;
    }
    m_pendingController.clear();
    const QString id = m_host->addModuleDevice(QString::fromLatin1(kLabControllerDeviceType),
                                               tr("TraceLab: Controller"), config);
    if (id.isEmpty()) {
        return;
    }
    if (LabControllerDevice* c = LabDeviceRegistry::instance().controller(id)) {
        // Already live (it came up while being added).
        rebuildChain();
        selectBlock(c, pickAfter);
        showCategory(Category::Code);
        return;
    }
    m_pendingController = id;
    m_pendingPick = pickAfter;
}

void LabPanel::rebuildChain() {
    LabDeviceRegistry& registry = LabDeviceRegistry::instance();
    LabPlantDevice* p = plant();
    LabControllerDevice* c = controller();
    QStringList others;
    for (const QString& id : registry.otherControllerIds()) {
        const LabDeviceRegistry::OtherController other = registry.otherController(id);
        if (other.plantDeviceId.isEmpty() || other.plantDeviceId == plantId()) {
            others.append(id);
        }
    }
    // Rebuilt only when what it shows changes (not on every status tick).
    QStringList signature{plantId(), c ? c->deviceId() : QString(),
                          p && p->codeBlocks() ? p->codeBlocks()->blockName(0) : QString()};
    if (c) {
        signature.append(c->blockName(LabControllerDevice::kController));
        for (int i = 0; i < c->filterCount(); ++i) {
            signature.append(c->blockName(i));
        }
    }
    signature += others;
    const QString joined = signature.join(QLatin1Char('\n'));
    if (joined == m_chainSignature) {
        refreshDiagram();
        return;
    }
    m_chainSignature = joined;

    rebuildBlocks();
    refreshScenes();

    // The legacy controller (the first one: two on a plant fight anyway).
    m_legacyId = others.value(0);
    m_legacySection->setVisible(!m_legacyId.isEmpty());
    if (!m_legacyId.isEmpty()) {
        m_legacyText->setText(
            tr("%1 is a controller written in C++ inside TraceView: it closes the loop on this "
               "plant, but its code cannot be seen or changed here. Replace it with a controller "
               "in C -- after removing it: two controllers on one plant fight over its inputs.")
                .arg(registry.otherController(m_legacyId).name));
    }

    // The add buttons' menus, filled now (and again when shown).
    for (QToolButton* button : {m_addFilterButton, m_addControllerButton}) {
        button->menu()->clear();
        fillBlockMenu(button->menu(), button == m_addControllerButton);
    }
    m_addControllerButton->setVisible(!c || c->spec(LabControllerDevice::kController).isEmpty());
    refreshDiagram();
}

void LabPanel::rebuildBlocks() {
    LabPlantDevice* p = plant();
    LabControllerDevice* c = controller();
    const CodeBlockOwner* selectedOwner = currentBlockOwner();
    const int selectedBlock = currentBlock();
    m_blocks.clear();
    if (p && p->codeBlocks()) {
        m_blocks.append({p->codeBlocks(), 0,
                         tr("Plant  ·  %1").arg(p->codeBlocks()->blockName(0))});
    }
    for (int i = 0; c && i < c->filterCount(); ++i) {
        m_blocks.append({c, i, tr("Filter %1  ·  %2").arg(i + 1).arg(c->blockName(i))});
    }
    int controllerRow = -1;
    if (c && !c->spec(LabControllerDevice::kController).isEmpty()) {
        controllerRow = m_blocks.size();
        m_blocks.append({c, LabControllerDevice::kController,
                         tr("Controller  ·  %1").arg(c->blockName(LabControllerDevice::kController))});
    }
    for (BlockCategory& category : m_blockCategories) {
        const QSignalBlocker blocker(category.combo);
        category.combo->clear();
        for (const BlockRef& ref : std::as_const(m_blocks)) {
            category.combo->addItem(ref.label);
        }
        category.content->setVisible(!m_blocks.isEmpty());
        category.none->setVisible(m_blocks.isEmpty());
    }
    // The block picked before, if it is still there; else the controller --
    // the block one writes -- or the first.
    int row = -1;
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].owner == selectedOwner && m_blocks[i].block == selectedBlock) {
            row = i;
        }
    }
    if (row < 0) {
        row = controllerRow >= 0 ? controllerRow : (m_blocks.isEmpty() ? -1 : 0);
    }
    m_blockIndex = -1;
    if (row >= 0) {
        selectBlock(m_blocks[row].owner, m_blocks[row].block);
    } else {
        for (BlockCategory& category : m_blockCategories) {
            category.page->setBlock(nullptr, 0);
        }
    }
}

void LabPanel::selectBlock(CodeBlockOwner* owner, int block) {
    int row = -1;
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].owner == owner && m_blocks[i].block == block) {
            row = i;
        }
    }
    if (row < 0) {
        return;
    }
    m_blockIndex = row;
    for (BlockCategory& category : m_blockCategories) {
        {
            const QSignalBlocker blocker(category.combo);
            category.combo->setCurrentIndex(row);
        }
        if (category.page->owner() != owner || category.page->block() != block) {
            category.page->setBlock(owner, block);
        }
    }
    refreshDiagram();
}

void LabPanel::onCardClicked(CardKind kind, int index) {
    switch (kind) {
        case CardKind::Plant:
            if (LabPlantDevice* p = plant(); p && p->codeBlocks()) {
                selectBlock(p->codeBlocks(), 0);
            }
            showCategory(Category::Plant);
            break;
        case CardKind::Filter:
            selectBlock(controller(), index);
            showCategory(Category::Code);
            break;
        case CardKind::Controller:
            selectBlock(controller(), LabControllerDevice::kController);
            showCategory(Category::Code);
            break;
        case CardKind::Other:
            break;  // its section is right under the loop
    }
}

LabPanel::BlockStatus LabPanel::blockStatus(CardKind what, int index) const {
    const QLocale locale;
    LabPlantDevice* p = plant();
    LabControllerDevice* c = controller();
    int tone = 0;
    QString headerStatus;
    QString kind;
    QString status;
    switch (what) {
        case CardKind::Plant: {
            kind = tr("Plant");
            if (!p) {
                break;
            }
            CodeBlockOwner* blocks = p->codeBlocks();
            if (blocks) {
                kind = p->worldName().isEmpty()
                           ? tr("Plant · C")
                           : tr("Plant · C · %1").arg(p->worldName());
            }
            tone = p->running() ? 1 : 2;
            headerStatus = p->running() ? tr("running") : tr("paused");
            status = tr("tick %1 · %2 s")
                         .arg(locale.toString(qlonglong(p->tick())))
                         .arg(locale.toString(p->time(), 'f', 1));
            if (blocks && !blocks->runtimeError(0).isEmpty()) {
                tone = 3;
                headerStatus = tr("stopped");
                status = blocks->runtimeError(0).section(QStringLiteral(": "), 1);
            } else if (blocks) {
                for (const code::CodeMessage& m : blocks->messages(0)) {
                    if (!m.warning) {
                        tone = 3;
                        headerStatus = tr("error");
                        status = m.line > 0 ? tr("does not compile (line %1)").arg(m.line)
                                            : tr("does not compile");
                        break;
                    }
                }
            }
            break;
        }
        case CardKind::Filter:
        case CardKind::Controller: {
            const int block = what == CardKind::Controller
                                  ? LabControllerDevice::kController
                                  : index;
            kind = what == CardKind::Controller ? tr("Controller · C")
                                                     : tr("Filter %1 · C").arg(index + 1);
            if (!c) {
                break;
            }
            const QString fault = c->runtimeError(block);
            bool error = false;
            int errorLine = 0;
            for (const code::CodeMessage& m : c->messages(block)) {
                if (!m.warning) {
                    error = true;
                    errorLine = m.line;
                    break;
                }
            }
            if (!fault.isEmpty()) {
                tone = 3;
                headerStatus = tr("stopped");
                status = fault.section(QStringLiteral(": "), 1);
            } else if (error) {
                tone = 3;
                headerStatus = tr("error");
                status = errorLine > 0 ? tr("does not compile (line %1)").arg(errorLine)
                                       : tr("does not compile");
                if (c->compiled(block)) {
                    status += tr("; the previous version runs");
                }
            } else if (!c->host()->configureError().isEmpty() &&
                       c->host()->configureError().startsWith(c->blockName(block))) {
                tone = 3;
                headerStatus = tr("not bound");
                status = c->host()->configureError().section(QStringLiteral(": "), 1);
            } else if (!c->host()->plantReady()) {
                tone = 2;
                headerStatus = tr("waiting");
                status = tr("waiting for the plant");
            } else {
                tone = 1;
                headerStatus = tr("running");
                status = tr("compiled");
                if (what == CardKind::Controller && c->controller() &&
                    c->controller()->usesPreview()) {
                    status += tr(" · uses the preview");
                }
            }
            break;
        }
        case CardKind::Other:
            kind = tr("Controller · C++ (fixed)");
            tone = 2;
            headerStatus = tr("legacy");
            status = tr("Replace it with a controller in C.");
            break;
    }
    return {tone, headerStatus, kind, status};
}

namespace {

// An array's elements ("s[0]".."s[14]") as one name with its size.
QVector<QPair<QString, int>> groupNames(const QStringList& names) {
    static const QRegularExpression element(QStringLiteral("^(.*)\\[(\\d+)\\]$"));
    QVector<QPair<QString, int>> groups;
    for (int i = 0; i < names.size();) {
        const QRegularExpressionMatch m = element.match(names[i]);
        if (m.hasMatch() && m.captured(2).toInt() == 0) {
            const QString base = m.captured(1);
            int count = 1;
            while (i + count < names.size() &&
                   names[i + count] == QStringLiteral("%1[%2]").arg(base).arg(count)) {
                ++count;
            }
            groups.append({base, count});
            i += count;
        } else {
            groups.append({names[i], 1});
            ++i;
        }
    }
    return groups;
}

// A scalar's value from a host's names and values; empty for an array or
// when unknown.
QString valueOf(const QStringList& names, const QVector<double>& values, const QString& name,
                int count) {
    if (count != 1) {
        return QString();
    }
    const int i = names.indexOf(name);
    if (i < 0 || i >= values.size() || !std::isfinite(values[i])) {
        return QString();
    }
    return QLocale().toString(values[i], 'g', 4);
}

}  // namespace

void LabPanel::refreshDiagram() {
    LabPlantDevice* p = plant();
    LabControllerDevice* c = controller();
    LoopDiagram::Model model;
    if (!p) {
        m_diagram->setModel(model);
        return;
    }
    QStringList measurements;
    QStringList commands;
    QStringList previews;
    const auto addSignal = [&](const QString& name, int count, const QString& unit,
                               LoopDiagram::Signal::Kind kind) {
        for (const LoopDiagram::Signal& s : std::as_const(model.signalList)) {
            if (s.name == name) {
                return false;
            }
        }
        LoopDiagram::Signal signal;
        signal.name = name;
        signal.count = count;
        signal.unit = unit;
        signal.kind = kind;
        model.signalList.append(signal);
        return true;
    };
    // The plant's outputs and inputs: as its code declares them (with their
    // units), or as the controller host was told.
    CodeBlockOwner* plantBlocks = p->codeBlocks();
    const code::CodeModule* plantModule = plantBlocks ? plantBlocks->module(0) : nullptr;
    if (plantModule) {
        for (const code::Declaration* decl : plantModule->declarations(code::DeclKind::Output)) {
            addSignal(decl->name, decl->count, decl->unit, LoopDiagram::Signal::Measurement);
            measurements.append(decl->name);
        }
        for (const code::Declaration* decl : plantModule->declarations(code::DeclKind::Input)) {
            addSignal(decl->name, decl->count, decl->unit, LoopDiagram::Signal::Command);
            commands.append(decl->name);
        }
    } else if (c) {
        for (const auto& [name, count] : groupNames(c->host()->measurementNames())) {
            addSignal(name, count, QString(), LoopDiagram::Signal::Measurement);
            measurements.append(name);
        }
        for (const auto& [name, count] : groupNames(c->host()->inputNames())) {
            addSignal(name, count, QString(), LoopDiagram::Signal::Command);
            commands.append(name);
        }
    }
    const code::CodeModule* controllerModule =
        c && !c->spec(LabControllerDevice::kController).isEmpty()
            ? c->module(LabControllerDevice::kController)
            : nullptr;
    if (controllerModule) {
        // The plant's preview of the track, when the controller asks for it.
        for (const code::Declaration* decl : controllerModule->declarations(code::DeclKind::Preview)) {
            addSignal(decl->name, decl->count, QString(), LoopDiagram::Signal::Preview);
            previews.append(decl->name);
        }
    }

    const auto selected = [this](CodeBlockOwner* owner, int block) {
        return owner && currentBlockOwner() == owner && currentBlock() == block &&
               currentCategory() != Category::Overview;
    };
    LoopDiagram::Block plantBlock;
    plantBlock.kind = LoopDiagram::Block::Plant;
    plantBlock.title = p->typeName();
    plantBlock.iconPath = QString::fromLatin1(kPlantIcon);
    plantBlock.reads = commands;
    plantBlock.writes = measurements + previews;
    {
        const BlockStatus status = blockStatus(CardKind::Plant, 0);
        plantBlock.subtitle = plantBlocks ? tr("Plant · C") : tr("Plant · C++");
        plantBlock.status = status.status;
        plantBlock.tone = status.tone;
    }
    plantBlock.selected = selected(plantBlocks, 0);
    model.blocks.append(plantBlock);

    for (int i = 0; c && i < c->filterCount(); ++i) {
        LoopDiagram::Block block;
        block.kind = LoopDiagram::Block::Filter;
        block.index = i;
        block.title = c->blockName(i);
        block.subtitle = tr("Filter %1 · C").arg(i + 1);
        block.iconPath = QString::fromLatin1(kFilterIcon);
        if (const code::CodeModule* module = c->module(i)) {
            for (const code::Declaration& decl : module->declarations()) {
                switch (decl.kind) {
                    case code::DeclKind::In:
                        block.reads.append(decl.name);
                        break;
                    case code::DeclKind::InOut:
                        block.reads.append(decl.name);
                        block.writes.append(decl.name);
                        break;
                    case code::DeclKind::Out:
                        addSignal(decl.name, decl.count, QString(), LoopDiagram::Signal::Created);
                        block.writes.append(decl.name);
                        break;
                    default:
                        break;
                }
            }
        }
        const BlockStatus status = blockStatus(CardKind::Filter, i);
        block.status = status.status;
        block.tone = status.tone;
        block.selected = selected(c, i);
        model.blocks.append(block);
    }

    LoopDiagram::Block controllerBlock;
    if (c && !c->spec(LabControllerDevice::kController).isEmpty()) {
        controllerBlock.kind = LoopDiagram::Block::Controller;
        controllerBlock.title = c->blockName(LabControllerDevice::kController);
        controllerBlock.subtitle = tr("Controller · C");
        controllerBlock.iconPath = QString::fromLatin1(kControllerIcon);
        if (controllerModule) {
            for (const code::Declaration& decl : controllerModule->declarations()) {
                if (decl.kind == code::DeclKind::In || decl.kind == code::DeclKind::Preview) {
                    controllerBlock.reads.append(decl.name);
                } else if (decl.kind == code::DeclKind::Out) {
                    controllerBlock.writes.append(decl.name);
                }
            }
        }
        const BlockStatus status = blockStatus(CardKind::Controller, 0);
        controllerBlock.status = status.status;
        controllerBlock.tone = status.tone;
        controllerBlock.selected = selected(c, LabControllerDevice::kController);
    } else {
        controllerBlock.kind = LoopDiagram::Block::NoController;
        controllerBlock.title = tr("No controller");
        controllerBlock.subtitle = tr("Click to add one");
    }
    model.blocks.append(controllerBlock);

    // The values now: as measured, and as the controller reads them.
    if (c) {
        const ControllerHost* host = c->host();
        for (LoopDiagram::Signal& signal : model.signalList) {
            signal.measured = valueOf(host->measurementNames(), host->lastMeasurements(),
                                      signal.name, signal.count);
            signal.final = valueOf(host->signalNames(), host->lastSignals(), signal.name,
                                   signal.count);
        }
    }

    m_diagram->setModel(model);
    m_diagramScroll->setFixedHeight(
        m_diagram->sizeHint().height() + 2 * m_diagramScroll->frameWidth() +
        (m_diagram->sizeHint().width() > m_diagramScroll->viewport()->width()
             ? m_diagramScroll->horizontalScrollBar()->sizeHint().height()
             : 0));
}

void LabPanel::refreshScenes() {
    const QString type = sceneType();
    QString name = SceneRegistry::instance().displayName(type);
    if (name.isEmpty()) {
        name = type;
    }
    m_sceneText->setText(
        controller() ? tr("The scene \"%1\", bound to this plant and its controller.").arg(name)
                     : tr("The scene \"%1\", bound to this plant (no controller on it yet: what "
                          "the scene reads from one stays unbound).")
                           .arg(name));
    m_addSceneButton->setEnabled(!plantId().isEmpty());
}

void LabPanel::refreshLive() {
    switch (currentCategory()) {
        case Category::Overview:
            refreshDiagram();
            break;
        case Category::Simulation:
            m_simulationPage->refreshLive();
            break;
        case Category::Plant:
            m_plantPage->refreshLive();
            break;
        case Category::Parameters:
        case Category::Signals:
            m_blockCategories[blockCategoryIndex(currentCategory())].page->refreshLive();
            break;
        case Category::Code:
        case Category::Scenes:
            break;
    }
    // The code's page always: what the block writes goes into its log.
    m_blockCategories[0].page->refreshLive();
}

// ---------------------------------------------------------------------------
// The ribbon

void LabPanel::addPlant() {
    const QVector<LabDeviceRegistry::PlantType>& types = LabDeviceRegistry::instance().plantTypes();
    if (!m_host || types.isEmpty()) {
        return;
    }
    addPlantOfType(0);
}

void LabPanel::addPlantOfType(int index) {
    const QVector<LabDeviceRegistry::PlantType>& types = LabDeviceRegistry::instance().plantTypes();
    if (!m_host || index < 0 || index >= types.size()) {
        return;
    }
    const LabDeviceRegistry::PlantType& type = types[index];
    m_host->addModuleDevice(type.typeId, tr("TraceLab: %1").arg(type.name), type.defaultConfig);
}

void LabPanel::addController() {
    if (!m_host || !plant()) {
        return;  // the action is disabled without a plant
    }
    QVector<code::CodeTemplate> templates =
        CodeLibrary::instance().templates(BlockKind::Controller);
    // The plant's benchmark's templates first.
    const QString benchmark = plantBenchmark();
    std::stable_partition(templates.begin(), templates.end(), [&](const code::CodeTemplate& t) {
        return t.id.section(QLatin1Char('/'), 0, 0) == benchmark;
    });
    if (LabControllerDevice* c = controller()) {
        if (c->spec(LabControllerDevice::kController).isEmpty() && !templates.isEmpty()) {
            addBlock(true, templates.first().id, QString());
        } else {
            selectBlock(c, LabControllerDevice::kController);
            showCategory(Category::Code);
        }
        return;
    }
    if (templates.isEmpty()) {
        QString error;
        const QString path =
            CodeLibrary::instance().createFile(BlockKind::Controller, QString(), QString(), &error);
        if (!path.isEmpty()) {
            addBlock(true, QString(), path);
        }
        return;
    }
    addBlock(true, templates.first().id, QString());
}

void LabPanel::addScene() {
    const QString plantDeviceId = plantId();
    if (!m_host || plantDeviceId.isEmpty()) {
        return;  // the action is disabled without a plant
    }
    LabControllerDevice* c = controller();
    const QString controllerId = c ? c->deviceId() : QString();
    // Bound by name (docs/DASHBOARD.md "Telemetry bindings"): MainWindow
    // fills the ids from the device's catalog.
    QJsonObject bind;
    const QString type = sceneType();
    const std::unique_ptr<SceneRenderer> scene = SceneRegistry::instance().create(type);
    if (!scene) {
        return;
    }
    for (const SceneInput& input : scene->inputs()) {
        const bool fromController = input.defaultTopic.startsWith(QLatin1String("ctl."));
        const QString deviceId = fromController ? controllerId : plantDeviceId;
        if (deviceId.isEmpty() || input.defaultTopic.isEmpty()) {
            continue;
        }
        QJsonObject entry{{"deviceId", deviceId}, {"topicName", input.defaultTopic}};
        if (!input.defaultField.isEmpty()) {
            entry.insert(QStringLiteral("fieldName"), input.defaultField);
        }
        bind.insert(input.name, entry);
    }
    m_host->addDashboardWidget(QString::fromLatin1(kSceneWidgetTypeId),
                               QJsonObject{{"scene", type}, {"bind", bind}});
}

void LabPanel::play() {
    if (LabPlantDevice* p = plant()) {
        p->run();
    }
}

void LabPanel::pauseSimulation() {
    if (LabPlantDevice* p = plant()) {
        p->pause();
    }
}

void LabPanel::resetSimulation() {
    if (LabPlantDevice* p = plant()) {
        p->reset();
    }
}

void LabPanel::stepSimulation() {
    if (LabPlantDevice* p = plant()) {
        p->pause();
        p->step();
    }
}

}  // namespace tracelab
