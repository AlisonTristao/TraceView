#include "labpanel.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
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
#include "lab/module/blockeditor.h"
#include "lab/module/labcard.h"
#include "lab/module/plantpage.h"
#include "lab/plant/labplantdevice.h"
#include "lab/scenes/diffdrivescene.h"
#include "lab/scenes/scenewidget.h"
#include "modules/appmodule.h"
#include "theme/iconutils.h"
#include "traceview/thememanager.h"

namespace tracelab {

using code::BlockKind;
using code::CodeLibrary;

namespace {

constexpr int kLiveIntervalMs = 250;
constexpr int kPageMargin = 16;
constexpr int kCardWidth = 200;
constexpr int kRibbonIconSize = 16;  // core/ribbonicons.h
// The empty state, laid out like a Settings category page.
constexpr int kEmptyMarginH = 28;
constexpr int kEmptyMarginV = 24;
constexpr int kEmptySpacing = 20;
constexpr int kEmptyTextWidth = 640;

const char kPlantIcon[] = ":/tracelab/icons/plant.svg";
const char kFilterIcon[] = ":/tracelab/icons/filter.svg";
const char kControllerIcon[] = ":/tracelab/icons/controller.svg";

}  // namespace

LabPanel::LabPanel(traceview::ModuleHost* host, QWidget* parent)
    : QWidget(parent), m_host(host) {
    m_addPlantAction = new QAction(tr("Add plant"), this);
    m_addPlantAction->setToolTip(tr("Adds a simulated plant to this project's devices."));
    m_addControllerAction = new QAction(tr("Add controller"), this);
    m_addControllerAction->setToolTip(tr("Adds a controller written in C that closes the loop on "
                                         "the plant shown below."));
    m_addSceneAction = new QAction(tr("Add scene"), this);
    m_addSceneAction->setToolTip(tr("Adds a Scene to the dashboard, bound to the plant and "
                                    "controller shown below."));
    m_groupSeparator = new QAction(this);
    m_groupSeparator->setSeparator(true);
    m_playAction = new QAction(tr("Play"), this);
    m_pauseAction = new QAction(tr("Pause"), this);
    m_resetAction = new QAction(tr("Reset"), this);
    m_stepAction = new QAction(tr("Step"), this);
    connect(m_addPlantAction, &QAction::triggered, this, &LabPanel::addPlant);
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
    // it is for, what to do), not like an empty chain.
    m_emptyState = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(kEmptyMarginH, kEmptyMarginV, kEmptyMarginH, kEmptyMarginV);
    emptyLayout->setSpacing(kEmptySpacing);
    auto* emptyTitle = new QLabel(QStringLiteral("<h2>TraceLab</h2>"), m_emptyState);
    emptyTitle->setTextFormat(Qt::RichText);
    emptyLayout->addWidget(emptyTitle);
    auto* emptyText = new QLabel(
        tr("No plant in this project yet. Open the line-follower benchmark, or build a loop of "
           "your own: a plant first, then the controller -- and filters between them, if you "
           "want -- written in C, with the code in front of you."),
        m_emptyState);
    emptyText->setWordWrap(true);
    emptyText->setMaximumWidth(kEmptyTextWidth);
    emptyLayout->addWidget(emptyText);
    auto* emptyButtons = new QHBoxLayout;
    auto* openBenchmark = new QPushButton(tr("Open the line-follower benchmark"), m_emptyState);
    connect(openBenchmark, &QPushButton::clicked, this, [this] {
        if (m_host) {
            m_host->openProject(lineFollowerProjectPath());
        }
    });
    auto* addPlantButton = new QPushButton(m_addPlantAction->text(), m_emptyState);
    addPlantButton->setToolTip(m_addPlantAction->toolTip());
    connect(addPlantButton, &QPushButton::clicked, m_addPlantAction, &QAction::trigger);
    emptyButtons->addWidget(openBenchmark);
    emptyButtons->addWidget(addPlantButton);
    emptyButtons->addStretch();
    emptyLayout->addLayout(emptyButtons);
    emptyLayout->addStretch();
    layout->addWidget(m_emptyState);

    // The loop and the page of the picked block.
    m_main = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(m_main);
    mainLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    mainLayout->setSpacing(12);
    auto* top = new QHBoxLayout;
    auto* loopTitle = new QLabel(tr("Loop"), m_main);
    QFont loopFont = loopTitle->font();
    loopFont.setBold(true);
    loopTitle->setFont(loopFont);
    top->addWidget(loopTitle);
    auto* loopNote = new QLabel(tr("plant  >  filters  >  controller  >  back to the plant (loop.u)"),
                                m_main);
    loopNote->setObjectName(QStringLiteral("labNote"));
    top->addWidget(loopNote);
    top->addStretch();
    m_plantComboLabel = new QLabel(tr("Plant"), m_main);
    m_plantComboLabel->setObjectName(QStringLiteral("labFieldLabel"));
    top->addWidget(m_plantComboLabel);
    m_plantCombo = new QComboBox(m_main);
    m_plantCombo->setMinimumWidth(200);
    top->addWidget(m_plantCombo);
    mainLayout->addLayout(top);

    auto* chainScroll = new QScrollArea(m_main);
    m_chainScroll = chainScroll;
    chainScroll->setFrameShape(QFrame::NoFrame);
    chainScroll->setWidgetResizable(true);
    chainScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    chainScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* chainWidget = new QWidget(chainScroll);
    m_chain = new QHBoxLayout(chainWidget);
    m_chain->setContentsMargins(0, 2, 0, 2);
    m_chain->setSpacing(8);
    chainScroll->setWidget(chainWidget);
    mainLayout->addWidget(chainScroll);

    auto* divider = new QFrame(m_main);
    divider->setObjectName(QStringLiteral("sectionDivider"));
    divider->setFixedHeight(1);
    mainLayout->addWidget(divider);

    m_pages = new QStackedWidget(m_main);
    m_plantPage = new PlantPage(m_pages);
    m_blockEditor = new BlockEditor(m_pages);
    m_otherPage = new QWidget(m_pages);
    auto* otherLayout = new QVBoxLayout(m_otherPage);
    otherLayout->setContentsMargins(0, 0, 0, 0);
    m_otherText = new QLabel(m_otherPage);
    m_otherText->setWordWrap(true);
    m_otherText->setMaximumWidth(kEmptyTextWidth);
    otherLayout->addWidget(m_otherText);
    auto* removeOther = new QPushButton(tr("Remove it from the project"), m_otherPage);
    otherLayout->addWidget(removeOther, 0, Qt::AlignLeft);
    otherLayout->addStretch();
    connect(removeOther, &QPushButton::clicked, this, [this] {
        if (m_host && !m_otherId.isEmpty()) {
            m_host->removeModuleDevice(m_otherId);
        }
    });
    m_pages->addWidget(m_plantPage);
    m_pages->addWidget(m_blockEditor);
    m_pages->addWidget(m_otherPage);
    mainLayout->addWidget(m_pages, 1);
    layout->addWidget(m_main);

    connect(m_blockEditor, &BlockEditor::removeRequested, this, [this](int index) {
        if (LabControllerDevice* c = controller()) {
            c->removeFilter(index);
            pick(index > 0 ? Pick::Filter : Pick::Plant, index - 1);
        }
    });
    connect(m_plantCombo, qOverload<int>(&QComboBox::activated), this, [this] {
        if (!m_blockEditor->maybeSave()) {
            return;
        }
        m_chainSignature.clear();
        rebuildChain();
        pick(Pick::Plant, 0);
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
}

QList<QAction*> LabPanel::ribbonActions() const {
    return {m_addPlantAction, m_addControllerAction, m_addSceneAction, m_groupSeparator,
            m_playAction, m_pauseAction, m_resetAction, m_stepAction};
}

QString LabPanel::lineFollowerProjectPath() {
    return QStringLiteral(":/tracelab/linefollower.tvproj");
}

void LabPanel::applyTheme() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    // Labels and notes in the secondary text color, the stat tiles on the
    // header strip's tone, the "+" blocks dashed.
    setStyleSheet(QStringLiteral("QLabel#labFieldLabel, QLabel#labNote, QLabel#labArrow { color: %1; }"
                                 "QFrame#labStat { background-color: %2; border-radius: 4px; }"
                                 "QToolButton#labAddBlock { border: 1px dashed %3; border-radius: 6px;"
                                 " padding: 10px 14px; color: %1; background: transparent; }"
                                 "QToolButton#labAddBlock:hover { border-color: %4; color: %5; }"
                                 "QToolButton#labAddBlock::menu-indicator { image: none; }")
                      .arg(palette.textSecondary.name(), palette.surfaceAlt.name(),
                           palette.borderStrong.name(), palette.accent.name(),
                           palette.textPrimary.name()));
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
    const bool many = m_plantCombo->count() > 1;
    m_plantCombo->setVisible(many);
    m_plantComboLabel->setVisible(many);
    const bool any = m_plantCombo->count() > 0;
    m_emptyState->setVisible(!any);
    m_main->setVisible(any);
    m_addControllerAction->setEnabled(any);
    m_addSceneAction->setEnabled(any);
    m_addPlantAction->setEnabled(!registry.plantTypes().isEmpty());

    rebuildChain();
    // A controller device just asked for is live now: show the block.
    if (!m_pendingController.isEmpty() && registry.controller(m_pendingController)) {
        m_pendingController.clear();
        if (m_pendingPick == LabControllerDevice::kController) {
            pick(Pick::Controller, 0);
        } else {
            pick(Pick::Filter, m_pendingPick);
        }
        return;
    }
    // What was picked may be gone.
    if ((m_pick == Pick::Controller && (!controller() || controller()->spec(LabControllerDevice::kController).isEmpty())) ||
        (m_pick == Pick::Filter && (!controller() || m_pickIndex >= controller()->filterCount())) ||
        (m_pick == Pick::Other && !registry.otherController(m_otherId).device)) {
        pick(Pick::Plant, 0);
    } else {
        pick(m_pick, m_pickIndex);
    }
}

// ---------------------------------------------------------------------------
// The chain

QMenu* LabPanel::blockMenu(bool controllerBlock) {
    auto* menu = new QMenu(this);
    const BlockKind kind = controllerBlock ? BlockKind::Controller : BlockKind::Filter;
    const QVector<code::CodeTemplate> templates = CodeLibrary::instance().templates(kind);
    if (!templates.isEmpty()) {
        menu->addSection(tr("Templates"));
        for (const code::CodeTemplate& t : templates) {
            QAction* action = menu->addAction(t.name);
            action->setToolTip(t.description);
            const QString id = t.id;
            connect(action, &QAction::triggered, this,
                    [this, controllerBlock, id] { addBlock(controllerBlock, id, QString()); });
        }
    }
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
    return menu;
}

void LabPanel::addBlock(bool controllerBlock, const QString& templateId, const QString& path) {
    if (!m_blockEditor->maybeSave()) {
        return;
    }
    CodeBlockSpec spec;
    spec.templateId = templateId;
    spec.path = path;
    if (LabControllerDevice* c = controller()) {
        if (controllerBlock) {
            c->setSpec(LabControllerDevice::kController, spec);
            pick(Pick::Controller, 0);
        } else {
            c->addFilter(spec);
            pick(Pick::Filter, c->filterCount() - 1);
        }
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
    if (LabDeviceRegistry::instance().controller(id)) {
        // Already live (it came up while being added).
        rebuildChain();
        if (pickAfter == LabControllerDevice::kController) {
            pick(Pick::Controller, 0);
        } else {
            pick(Pick::Filter, pickAfter);
        }
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
    QStringList signature{plantId(), c ? c->deviceId() : QString()};
    if (c) {
        signature.append(c->blockName(LabControllerDevice::kController));
        for (int i = 0; i < c->filterCount(); ++i) {
            signature.append(c->blockName(i));
        }
    }
    signature += others;
    const QString joined = signature.join(QLatin1Char('\n'));
    if (joined == m_chainSignature) {
        refreshCards();
        return;
    }
    m_chainSignature = joined;

    while (QLayoutItem* item = m_chain->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_cards.clear();
    if (!p) {
        return;
    }
    const auto addArrow = [this] {
        auto* arrow = new QLabel(QStringLiteral("→"), m_chain->parentWidget());
        arrow->setObjectName(QStringLiteral("labArrow"));
        QFont font = arrow->font();
        font.setPointSizeF(font.pointSizeF() * 1.6);
        arrow->setFont(font);
        m_chain->addWidget(arrow);
    };
    const auto addCard = [this](const char* icon, const QString& title, Pick what, int index) {
        Card card;
        card.card = new LabCard(QString::fromLatin1(icon), title, m_chain->parentWidget());
        card.card->setSelectable(true);
        card.card->setFixedWidth(kCardWidth);
        card.kind = new QLabel(card.card);
        card.kind->setObjectName(QStringLiteral("labNote"));
        card.status = new QLabel(card.card);
        card.status->setWordWrap(true);
        card.card->body()->setSpacing(2);
        card.card->body()->addWidget(card.kind);
        card.card->body()->addWidget(card.status);
        card.pick = what;
        card.index = index;
        connect(card.card, &LabCard::clicked, this, [this, what, index] { pick(what, index); });
        m_chain->addWidget(card.card);
        m_cards.append(card);
    };
    const auto addButton = [this](const QString& text, QMenu* menu) {
        auto* button = new QToolButton(m_chain->parentWidget());
        button->setObjectName(QStringLiteral("labAddBlock"));
        button->setText(text);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        m_chain->addWidget(button);
    };

    addCard(kPlantIcon, p->typeName(), Pick::Plant, 0);
    for (int i = 0; c && i < c->filterCount(); ++i) {
        addArrow();
        addCard(kFilterIcon, c->blockName(i), Pick::Filter, i);
    }
    addArrow();
    addButton(tr("+ Filter"), blockMenu(false));
    addArrow();
    if (c && !c->spec(LabControllerDevice::kController).isEmpty()) {
        addCard(kControllerIcon, c->blockName(LabControllerDevice::kController), Pick::Controller,
                0);
    } else {
        addButton(tr("+ Controller"), blockMenu(true));
    }
    for (int i = 0; i < others.size(); ++i) {
        const LabDeviceRegistry::OtherController other = registry.otherController(others[i]);
        auto* gap = new QWidget(m_chain->parentWidget());
        gap->setFixedWidth(16);
        m_chain->addWidget(gap);
        addCard(kControllerIcon, other.name, Pick::Other, i);
        m_cards.last().card->setProperty("otherId", others[i]);
    }
    m_chain->addStretch();
    refreshCards();
    // As tall as the cards (a QScrollArea would otherwise take its own
    // default height and cut them), plus a horizontal scroll bar's room.
    // (The new cards are not shown yet: measured one by one.)
    int height = 0;
    for (const Card& card : std::as_const(m_cards)) {
        height = qMax(height, card.card->sizeHint().height());
    }
    const QMargins margins = m_chain->contentsMargins();
    auto* scroll = static_cast<QScrollArea*>(m_chainScroll);
    m_chainScroll->setFixedHeight(height + margins.top() + margins.bottom() +
                                  scroll->horizontalScrollBar()->sizeHint().height());
}

void LabPanel::refreshCards() {
    const QLocale locale;
    LabPlantDevice* p = plant();
    LabControllerDevice* c = controller();
    for (Card& card : m_cards) {
        const bool selected = card.pick == m_pick &&
                              (card.pick == Pick::Plant || card.pick == Pick::Controller ||
                               (card.pick == Pick::Other
                                    ? card.card->property("otherId").toString() == m_otherId
                                    : card.index == m_pickIndex));
        card.card->setSelected(selected);
        LabCard::Tone tone = LabCard::Tone::None;
        QString headerStatus;
        QString kind;
        QString status;
        switch (card.pick) {
            case Pick::Plant: {
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
                card.card->setTitle(p->typeName());
                tone = p->running() ? LabCard::Tone::Success : LabCard::Tone::Warning;
                headerStatus = p->running() ? tr("running") : tr("paused");
                status = tr("tick %1 · %2 s")
                             .arg(locale.toString(qlonglong(p->tick())))
                             .arg(locale.toString(p->time(), 'f', 1));
                if (blocks && !blocks->runtimeError(0).isEmpty()) {
                    tone = LabCard::Tone::Danger;
                    headerStatus = tr("stopped");
                    status = blocks->runtimeError(0).section(QStringLiteral(": "), 1);
                } else if (blocks) {
                    for (const code::CodeMessage& m : blocks->messages(0)) {
                        if (!m.warning) {
                            tone = LabCard::Tone::Danger;
                            headerStatus = tr("error");
                            status = m.line > 0 ? tr("does not compile (line %1)").arg(m.line)
                                                : tr("does not compile");
                            break;
                        }
                    }
                }
                break;
            }
            case Pick::Filter:
            case Pick::Controller: {
                const int block =
                    card.pick == Pick::Controller ? LabControllerDevice::kController : card.index;
                kind = card.pick == Pick::Controller ? tr("Controller · C")
                                                     : tr("Filter %1 · C").arg(card.index + 1);
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
                    tone = LabCard::Tone::Danger;
                    headerStatus = tr("stopped");
                    status = fault.section(QStringLiteral(": "), 1);
                } else if (error) {
                    tone = LabCard::Tone::Danger;
                    headerStatus = tr("error");
                    status = errorLine > 0 ? tr("does not compile (line %1)").arg(errorLine)
                                           : tr("does not compile");
                    if (c->compiled(block)) {
                        status += tr("; the previous version runs");
                    }
                } else if (!c->host()->configureError().isEmpty() &&
                           c->host()->configureError().startsWith(c->blockName(block))) {
                    tone = LabCard::Tone::Danger;
                    headerStatus = tr("not bound");
                    status = c->host()->configureError().section(QStringLiteral(": "), 1);
                } else if (!c->host()->plantReady()) {
                    tone = LabCard::Tone::Warning;
                    headerStatus = tr("waiting");
                    status = tr("waiting for the plant");
                } else {
                    tone = LabCard::Tone::Success;
                    headerStatus = tr("running");
                    status = tr("compiled");
                    if (card.pick == Pick::Controller && c->controller() &&
                        c->controller()->usesPreview()) {
                        status += tr(" · uses the preview");
                    }
                }
                break;
            }
            case Pick::Other:
                kind = tr("Controller · C++ (fixed)");
                tone = LabCard::Tone::Warning;
                headerStatus = tr("legacy");
                status = tr("Replace it with a controller in C.");
                break;
        }
        card.card->setStatus(tone, headerStatus);
        card.kind->setText(kind);
        if (card.status->text() != status) {
            card.status->setText(status);
            card.status->setToolTip(status);
        }
    }
}

void LabPanel::pick(Pick what, int index) {
    const bool sameBlock = (what == Pick::Controller && m_pick == Pick::Controller) ||
                           (what == Pick::Filter && m_pick == Pick::Filter && index == m_pickIndex);
    if (!sameBlock && m_pages->currentWidget() == m_blockEditor && !m_blockEditor->maybeSave()) {
        return;
    }
    LabControllerDevice* c = controller();
    if ((what == Pick::Filter && (!c || index < 0 || index >= c->filterCount())) ||
        (what == Pick::Controller && !c)) {
        what = Pick::Plant;
        index = 0;
    }
    m_pick = what;
    m_pickIndex = index;
    switch (what) {
        case Pick::Plant:
            m_plantPage->setPlant(plant());
            m_pages->setCurrentWidget(m_plantPage);
            break;
        case Pick::Filter:
        case Pick::Controller: {
            const int block = what == Pick::Controller ? LabControllerDevice::kController : index;
            if (m_blockEditor->device() != c || m_blockEditor->block() != block) {
                m_blockEditor->setBlock(c, block);
            }
            m_pages->setCurrentWidget(m_blockEditor);
            break;
        }
        case Pick::Other: {
            int i = 0;
            for (const Card& card : std::as_const(m_cards)) {
                if (card.pick == Pick::Other && i++ == index) {
                    m_otherId = card.card->property("otherId").toString();
                }
            }
            m_otherText->setText(
                tr("%1 is a controller written in C++ inside TraceView: it closes the loop on "
                   "this plant, but its code cannot be seen or changed here. Replace it with a "
                   "controller in C (\"+ Controller\" or \"Add controller\" on the ribbon, "
                   "after removing it: two controllers on one plant fight over its inputs).")
                    .arg(LabDeviceRegistry::instance().otherController(m_otherId).name));
            m_pages->setCurrentWidget(m_otherPage);
            break;
        }
    }
    refreshCards();
}

void LabPanel::refreshLive() {
    refreshCards();
    if (m_pages->currentWidget() == m_plantPage) {
        m_plantPage->refreshLive();
    } else if (m_pages->currentWidget() == m_blockEditor) {
        m_blockEditor->refreshLive();
    }
}

// ---------------------------------------------------------------------------
// The ribbon

void LabPanel::addPlant() {
    const QVector<LabDeviceRegistry::PlantType>& types = LabDeviceRegistry::instance().plantTypes();
    if (!m_host || types.isEmpty()) {
        return;
    }
    const LabDeviceRegistry::PlantType& type = types.first();
    m_host->addModuleDevice(type.typeId, tr("TraceLab: %1").arg(type.name), type.defaultConfig);
}

void LabPanel::addController() {
    if (!m_host || !plant()) {
        return;  // the action is disabled without a plant
    }
    const QVector<code::CodeTemplate> templates =
        CodeLibrary::instance().templates(BlockKind::Controller);
    if (LabControllerDevice* c = controller()) {
        if (c->spec(LabControllerDevice::kController).isEmpty() && !templates.isEmpty()) {
            addBlock(true, templates.first().id, QString());
        } else {
            pick(Pick::Controller, 0);
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
    DiffDriveScene scene;
    for (const SceneInput& input : scene.inputs()) {
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
                               QJsonObject{{"scene", QString::fromLatin1(DiffDriveScene::kTypeId)},
                                           {"bind", bind}});
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
