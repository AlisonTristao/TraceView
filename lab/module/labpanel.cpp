#include "labpanel.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "lab/benchmarks/linefollower/device/lfdevices.h"
#include "lab/control/controllerhost.h"
#include "lab/module/labcard.h"
#include "lab/scenes/diffdrivescene.h"
#include "lab/scenes/scenewidget.h"
#include "modules/appmodule.h"
#include "theme/doublespinbox.h"
#include "theme/iconutils.h"
#include "traceview/thememanager.h"

namespace tracelab {

namespace {

constexpr int kStatusIntervalMs = 250;
// The cards page: the canvas margin and the gap between cards.
constexpr int kPageMargin = 16;
constexpr int kCardSpacing = 12;
// Every field at least this wide (Settings' kMinFieldWidth), wider when a
// value needs it -- see LabPanel::alignCards().
constexpr int kMinFieldWidth = 180;
constexpr int kStatSpacing = 8;
constexpr int kRibbonIconSize = 16;  // core/ribbonicons.h
// The empty state, laid out like a Settings category page.
constexpr int kEmptyMarginH = 28;
constexpr int kEmptyMarginV = 24;
constexpr int kEmptySpacing = 20;
constexpr int kEmptyTextWidth = 640;

// The speed choices, in combo order.
struct Speed {
    lf::RunMode mode;
    double scale;
};
const Speed kSpeeds[] = {
    {lf::RunMode::RealTime, 1.0},
    {lf::RunMode::RealTime, 2.0},
    {lf::RunMode::AsFastAsPossible, 1.0},
    {lf::RunMode::Lockstep, 1.0},
};

// A field whose arrows keep a step picked for its quantity instead of
// DoubleSpinBox's adaptive one.
void setFixedStep(QDoubleSpinBox* spin, double step) {
    spin->setStepType(QAbstractSpinBox::DefaultStepType);
    spin->setSingleStep(step);
}

// A form inside a card, labels on the left in the secondary text color.
QFormLayout* addForm(QBoxLayout* layout) {
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    layout->addLayout(form);
    return form;
}

void addField(QFormLayout* form, const QString& label, QWidget* field) {
    auto* text = new QLabel(label, field->parentWidget());
    text->setObjectName(QStringLiteral("labFieldLabel"));
    form->addRow(text, field);
}

// A combo item under a short name, with its long form as the item tooltip.
void addItem(QComboBox* combo, const QString& text, const QVariant& data,
             const QString& tooltip) {
    combo->addItem(text, data);
    if (!tooltip.isEmpty()) {
        combo->setItemData(combo->count() - 1, tooltip, Qt::ToolTipRole);
    }
}

// The fields of one controller kind: a divider, the kind's name and its
// own form, shown only while that kind is picked.
QWidget* addFieldGroup(QBoxLayout* body, const QString& title, QFormLayout** form) {
    auto* group = new QWidget;
    group->setObjectName(QStringLiteral("labFieldGroup"));
    auto* layout = new QVBoxLayout(group);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(8);
    auto* divider = new QFrame(group);
    divider->setObjectName(QStringLiteral("sectionDivider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);
    auto* heading = new QLabel(title, group);
    heading->setObjectName(QStringLiteral("labNote"));
    layout->addWidget(heading);
    *form = addForm(layout);
    body->addWidget(group);
    return group;
}

// A number tile (value over its caption) in `row`; returns the value label.
QLabel* addStat(QHBoxLayout* row, const QString& caption, QWidget* parent) {
    auto* tile = new QFrame(parent);
    tile->setObjectName(QStringLiteral("labStat"));
    auto* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(0);
    auto* value = new QLabel(QStringLiteral("--"), tile);
    QFont font = value->font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * 1.3);
    } else {
        font.setPixelSize(qRound(font.pixelSize() * 1.3));
    }
    value->setFont(font);
    auto* label = new QLabel(caption, tile);
    label->setObjectName(QStringLiteral("labNote"));
    layout->addWidget(value);
    layout->addWidget(label);
    row->addWidget(tile, 1);
    return value;
}

}  // namespace

LabPanel::LabPanel(traceview::ModuleHost* host, QWidget* parent)
    : QWidget(parent), m_host(host) {
    m_addPlantAction = new QAction(tr("Add plant"), this);
    m_addPlantAction->setToolTip(tr("Adds a simulated line-follower plant to this project's "
                                    "devices."));
    m_addControllerAction = new QAction(tr("Add controller"), this);
    m_addControllerAction->setToolTip(tr("Adds a controller host that closes the loop on the "
                                         "plant picked below."));
    m_addSceneAction = new QAction(tr("Add scene"), this);
    m_addSceneAction->setToolTip(tr("Adds a Scene to the dashboard, bound to the plant and "
                                    "controller picked below."));
    m_groupSeparator = new QAction(this);
    m_groupSeparator->setSeparator(true);
    m_playAction = new QAction(tr("Play"), this);
    m_pauseAction = new QAction(tr("Pause"), this);
    m_resetAction = new QAction(tr("Reset"), this);
    m_stepAction = new QAction(tr("Step"), this);
    connect(m_addPlantAction, &QAction::triggered, this, &LabPanel::addLineFollowerPlant);
    connect(m_addControllerAction, &QAction::triggered, this, &LabPanel::addController);
    connect(m_addSceneAction, &QAction::triggered, this, &LabPanel::addScene);
    connect(m_playAction, &QAction::triggered, this, &LabPanel::play);
    connect(m_pauseAction, &QAction::triggered, this, &LabPanel::pauseSimulation);
    connect(m_resetAction, &QAction::triggered, this, &LabPanel::resetSimulation);
    connect(m_stepAction, &QAction::triggered, this, &LabPanel::stepSimulation);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // No TraceLab device yet: the page reads like a Settings category (a
    // title, what it is for, what to do), not like an empty card grid.
    m_emptyState = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(kEmptyMarginH, kEmptyMarginV, kEmptyMarginH, kEmptyMarginV);
    emptyLayout->setSpacing(kEmptySpacing);
    auto* emptyTitle = new QLabel(QStringLiteral("<h2>TraceLab</h2>"), m_emptyState);
    emptyTitle->setTextFormat(Qt::RichText);
    emptyLayout->addWidget(emptyTitle);
    auto* emptyText = new QLabel(tr("No TraceLab device in this project yet. Open the "
                                    "line-follower benchmark, or build a lab of your own: a "
                                    "plant first, then a controller and a scene for it."),
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
    auto* addPlant = new QPushButton(m_addPlantAction->text(), m_emptyState);
    addPlant->setToolTip(m_addPlantAction->toolTip());
    connect(addPlant, &QPushButton::clicked, m_addPlantAction, &QAction::trigger);
    emptyButtons->addWidget(openBenchmark);
    emptyButtons->addWidget(addPlant);
    emptyButtons->addStretch();
    emptyLayout->addLayout(emptyButtons);
    emptyLayout->addStretch();
    layout->addWidget(m_emptyState);

    // The cards, on the canvas like the Devices tab's and as many abreast as
    // the width allows (layoutCards()).
    m_cardsScroll = new QScrollArea(this);
    m_cardsScroll->setFrameShape(QFrame::NoFrame);
    m_cardsScroll->setWidgetResizable(true);
    auto* cardsPage = new QWidget(m_cardsScroll);
    auto* pageLayout = new QVBoxLayout(cardsPage);
    pageLayout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
    m_cardsGrid = new QGridLayout;
    m_cardsGrid->setSpacing(kCardSpacing);
    auto* gridRow = new QHBoxLayout;
    gridRow->addLayout(m_cardsGrid);
    gridRow->addStretch();
    pageLayout->addLayout(gridRow);
    pageLayout->addStretch();
    m_cardsScroll->setWidget(cardsPage);
    layout->addWidget(m_cardsScroll);

    // Simulation: where it stands, then the run settings.
    m_simulationCard =
        new LabCard(QStringLiteral(":/tracelab/icons/plant.svg"), tr("Simulation"), cardsPage);
    auto* stats = new QHBoxLayout;
    stats->setSpacing(kStatSpacing);
    m_tickValue = addStat(stats, tr("tick"), m_simulationCard);
    m_timeValue = addStat(stats, tr("time"), m_simulationCard);
    m_coveredValue = addStat(stats, tr("covered"), m_simulationCard);
    m_simulationCard->body()->addLayout(stats);
    m_perturbationLabel = new QLabel(m_simulationCard);
    m_perturbationLabel->setObjectName(QStringLiteral("labNote"));
    m_perturbationLabel->hide();
    m_simulationCard->body()->addWidget(m_perturbationLabel);
    QFormLayout* runForm = addForm(m_simulationCard->body());
    m_plantCombo = new QComboBox(m_simulationCard);
    addField(runForm, tr("Plant"), m_plantCombo);
    // Short names, so the closed field never cuts one; the long form is the
    // item's tooltip in the open list.
    m_speedCombo = new QComboBox(m_simulationCard);
    addItem(m_speedCombo, tr("1x (real time)"), {}, {});
    addItem(m_speedCombo, tr("2x"), {}, {});
    addItem(m_speedCombo, tr("As fast as possible"), {}, {});
    addItem(m_speedCombo, tr("Lockstep"), {}, tr("Lockstep (waits for the controller)"));
    addField(runForm, tr("Speed"), m_speedCombo);
    m_rateSpin = new QSpinBox(m_simulationCard);
    m_rateSpin->setRange(1, 1000);
    m_rateSpin->setSuffix(tr(" Hz"));
    m_rateSpin->setToolTip(tr("Step rate of the simulation: the plant integrates and the "
                              "controllers on it sample at Ts = 1 / rate. Changing it "
                              "restarts the run."));
    addField(runForm, tr("Sample rate"), m_rateSpin);

    // Track and sensors: applied together, on a reset.
    auto* trackCard = new LabCard(QStringLiteral(":/tracelab/icons/scene.svg"),
                                  tr("Track and sensors"), cardsPage);
    QFormLayout* trackForm = addForm(trackCard->body());
    m_seedSpin = new QSpinBox(trackCard);
    m_seedSpin->setRange(-1, 2147483647);
    m_seedSpin->setToolTip(tr("-1 draws a random track on every reset."));
    addField(trackForm, tr("Track seed"), m_seedSpin);
    m_trackCombo = new QComboBox(trackCard);
    m_trackCombo->addItem(tr("Lemniscate"));
    m_trackCombo->addItem(tr("Circle"));
    addField(trackForm, tr("Track type"), m_trackCombo);
    m_noiseCheck = new QCheckBox(tr("Sensor noise"), trackCard);
    m_noiseCheck->setToolTip(tr("Synthetic noise on the gyro and the line sensors (the "
                                "encoders always count whole ticks)."));
    trackForm->addRow(m_noiseCheck);
    auto* applyRow = new QHBoxLayout;
    applyRow->addStretch();
    auto* applyTrack = new QPushButton(tr("Apply and reset"), trackCard);
    applyRow->addWidget(applyTrack);
    trackCard->body()->addLayout(applyRow);

    // Controller: what every kind shares, then only the picked kind's own.
    m_controllerCard = new LabCard(QStringLiteral(":/tracelab/icons/controller.svg"),
                                   tr("Controller"), cardsPage);
    QFormLayout* controllerForm = addForm(m_controllerCard->body());
    m_controllerCombo = new QComboBox(m_controllerCard);
    addField(controllerForm, tr("Device"), m_controllerCombo);
    m_typeCombo = new QComboBox(m_controllerCard);
    m_typeCombo->addItem(tr("qLPV MPC"), QStringLiteral("qlpv"));
    m_typeCombo->addItem(tr("GPC"), QStringLiteral("gpc"));
    m_typeCombo->addItem(tr("PD (line sensors)"), QStringLiteral("pd"));
    addField(controllerForm, tr("Controller"), m_typeCombo);
    m_filterSpin = new traceview::DoubleSpinBox(m_controllerCard);
    m_filterSpin->setRange(0.0, 1.0);
    m_filterSpin->setDecimals(3);
    setFixedStep(m_filterSpin, 0.005);
    m_filterSpin->setSuffix(tr(" s"));
    m_filterSpin->setSpecialValueText(tr("off"));
    m_filterSpin->setToolTip(tr("First-order low-pass on the measured wheel speeds (the C "
                                "filter runtime); 0 turns it off."));
    addField(controllerForm, tr("Wheel-speed filter"), m_filterSpin);

    QFormLayout* qlpvForm = nullptr;
    m_qlpvFields = addFieldGroup(m_controllerCard->body(), tr("qLPV MPC"), &qlpvForm);
    m_qLateralSpin = new traceview::DoubleSpinBox(m_qlpvFields);
    m_qLateralSpin->setRange(0.0, 1e7);
    m_qLateralSpin->setDecimals(1);
    addField(qlpvForm, tr("Lateral weight"), m_qLateralSpin);
    m_schedulingCombo = new QComboBox(m_qlpvFields);
    addItem(m_schedulingCombo, tr("Frozen"), QStringLiteral("frozen"),
            tr("Frozen (rho of now, whole horizon)"));
    addItem(m_schedulingCombo, tr("Warm start"), QStringLiteral("warm_start"),
            tr("Warm start (last prediction)"));
    addItem(m_schedulingCombo, tr("Taylor"), QStringLiteral("taylor"),
            tr("Taylor (first order around now)"));
    addItem(m_schedulingCombo, tr("Iterative"), QStringLiteral("iterative"),
            tr("Iterative (re-solve until rho settles)"));
    m_schedulingCombo->setToolTip(
        tr("How the qLPV MPC fills the scheduling variable rho over the horizon."));
    addField(qlpvForm, tr("Rho scheduling"), m_schedulingCombo);
    m_embeddingCombo = new QComboBox(m_qlpvFields);
    m_embeddingCombo->addItem(QStringLiteral("sinc"), QStringLiteral("sinc"));
    m_embeddingCombo->addItem(QStringLiteral("sin_cos"), QStringLiteral("sin_cos"));
    addField(qlpvForm, tr("Embedding"), m_embeddingCombo);
    m_iterationsSpin = new QSpinBox(m_qlpvFields);
    m_iterationsSpin->setRange(1, 50);
    m_iterationsSpin->setToolTip(tr("Most QPs per step with the iterative scheduling."));
    addField(qlpvForm, tr("Iterations"), m_iterationsSpin);

    QFormLayout* gpcForm = nullptr;
    m_gpcFields = addFieldGroup(m_controllerCard->body(), tr("GPC"), &gpcForm);
    m_gpcLambdaSpin = new traceview::DoubleSpinBox(m_gpcFields);
    m_gpcLambdaSpin->setRange(0.0, 10.0);
    m_gpcLambdaSpin->setDecimals(5);
    setFixedStep(m_gpcLambdaSpin, 0.0005);
    addField(gpcForm, tr("Control weight"), m_gpcLambdaSpin);

    QFormLayout* pdForm = nullptr;
    m_pdFields = addFieldGroup(m_controllerCard->body(), tr("PD (line sensors)"), &pdForm);
    m_pdKpSpin = new traceview::DoubleSpinBox(m_pdFields);
    m_pdKpSpin->setRange(0.0, 1e5);
    m_pdKpSpin->setDecimals(0);
    setFixedStep(m_pdKpSpin, 100.0);
    m_pdKpSpin->setSuffix(tr(" %/m"));
    m_pdKpSpin->setToolTip(tr("Wheel speed difference per metre of line offset under the "
                              "sensor array."));
    addField(pdForm, tr("Kp"), m_pdKpSpin);
    m_pdKdSpin = new traceview::DoubleSpinBox(m_pdFields);
    m_pdKdSpin->setRange(0.0, 1e4);
    m_pdKdSpin->setDecimals(1);
    setFixedStep(m_pdKdSpin, 5.0);
    m_pdKdSpin->setSuffix(tr(" %s/m"));
    addField(pdForm, tr("Kd"), m_pdKdSpin);
    m_pdBaseSpin = new traceview::DoubleSpinBox(m_pdFields);
    m_pdBaseSpin->setRange(-100.0, 100.0);
    m_pdBaseSpin->setDecimals(0);
    setFixedStep(m_pdBaseSpin, 5.0);
    m_pdBaseSpin->setSuffix(tr(" %"));
    m_pdBaseSpin->setToolTip(tr("Command on both wheels before the PD correction: the speed."));
    addField(pdForm, tr("Base speed"), m_pdBaseSpin);

    m_cards = {m_simulationCard, trackCard, m_controllerCard};
    alignCards();

    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LabPanel::applySpeed);
    connect(m_rateSpin, &QSpinBox::editingFinished, this, &LabPanel::applyRate);
    connect(applyTrack, &QPushButton::clicked, this, &LabPanel::applyTrackAndSensors);
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LabPanel::showControllerFields);
    for (QComboBox* combo : {m_typeCombo, m_schedulingCombo, m_embeddingCombo}) {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &LabPanel::applyController);
    }
    connect(m_iterationsSpin, &QSpinBox::editingFinished, this, &LabPanel::applyController);
    for (QDoubleSpinBox* spin :
         {m_filterSpin, m_qLateralSpin, m_gpcLambdaSpin, m_pdKpSpin, m_pdKdSpin, m_pdBaseSpin}) {
        connect(spin, &QDoubleSpinBox::editingFinished, this, &LabPanel::applyController);
    }
    connect(m_plantCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LabPanel::refreshFromDevices);
    connect(m_controllerCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LabPanel::refreshFromDevices);
    connect(&lf::LineDeviceRegistry::instance(), &lf::LineDeviceRegistry::devicesChanged, this,
            &LabPanel::refreshDevices);
    connect(&traceview::ThemeManager::instance(), &traceview::ThemeManager::themeChanged, this,
            &LabPanel::applyTheme);

    auto* statusTimer = new QTimer(this);
    statusTimer->setInterval(kStatusIntervalMs);
    connect(statusTimer, &QTimer::timeout, this, &LabPanel::refreshStatus);
    statusTimer->start();
    applyTheme();
    showControllerFields();
    refreshDevices();
}

QList<QAction*> LabPanel::ribbonActions() const {
    return {m_addPlantAction, m_addControllerAction, m_addSceneAction, m_groupSeparator,
            m_playAction, m_pauseAction, m_resetAction, m_stepAction};
}

void LabPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    layoutCards();
}

void LabPanel::applyTheme() {
    const traceview::ThemePalette& palette = traceview::ThemeManager::instance().currentTheme();
    // Labels and notes in the secondary text color, the stat tiles on the
    // header strip's tone, and the controller's field groups see-through
    // (a plain QWidget would otherwise paint the canvas color in the card).
    setStyleSheet(QStringLiteral("QLabel#labFieldLabel, QLabel#labNote { color: %1; }"
                                 "QFrame#labStat { background-color: %2; border-radius: 4px; }"
                                 "QWidget#labFieldGroup { background: transparent; }")
                      .arg(palette.textSecondary.name(), palette.surfaceAlt.name()));

    const auto icon = [&palette](const char* name) {
        return traceview::loadTintedIcon(
            QStringLiteral(":/tracelab/icons/%1.svg").arg(QLatin1String(name)),
            palette.textPrimary, kRibbonIconSize);
    };
    m_addPlantAction->setIcon(icon("plant"));
    m_addControllerAction->setIcon(icon("controller"));
    m_addSceneAction->setIcon(icon("scene"));
    m_playAction->setIcon(icon("play"));
    m_pauseAction->setIcon(icon("pause"));
    m_resetAction->setIcon(icon("reset"));
    m_stepAction->setIcon(icon("step"));
}

void LabPanel::alignCards() {
    // One label column and one field column across every card: the labels
    // as wide as the widest, the fields as wide as the widest content any of
    // them shows (device ids included, so call again when those change).
    int labelWidth = 0;
    const QList<QLabel*> labels = findChildren<QLabel*>(QStringLiteral("labFieldLabel"));
    for (QLabel* label : labels) {
        label->setMinimumWidth(0);
        labelWidth = qMax(labelWidth, label->sizeHint().width());
    }
    for (QLabel* label : labels) {
        label->setFixedWidth(labelWidth);
    }
    int fieldWidth = kMinFieldWidth;
    QList<QWidget*> fields;
    for (LabCard* card : std::as_const(m_cards)) {
        for (QComboBox* combo : card->findChildren<QComboBox*>()) {
            fields.append(combo);
            combo->setMinimumWidth(0);
            combo->setMaximumWidth(QWIDGETSIZE_MAX);
            fieldWidth = qMax(fieldWidth, combo->sizeHint().width());
        }
        for (QAbstractSpinBox* spin : card->findChildren<QAbstractSpinBox*>()) {
            fields.append(spin);
        }
    }
    for (QWidget* field : std::as_const(fields)) {
        field->setFixedWidth(fieldWidth);
    }
    int cardWidth = 0;
    for (LabCard* card : std::as_const(m_cards)) {
        card->setMinimumWidth(0);
        card->setMaximumWidth(QWIDGETSIZE_MAX);
        cardWidth = qMax(cardWidth, card->sizeHint().width());
    }
    for (LabCard* card : std::as_const(m_cards)) {
        card->setFixedWidth(cardWidth);
    }
    m_columns = 0;
    layoutCards();
}

void LabPanel::layoutCards() {
    if (m_cards.isEmpty()) {
        return;
    }
    const int cardWidth = m_cards.first()->width();
    const int available =
        width() - 2 * kPageMargin - m_cardsScroll->verticalScrollBar()->sizeHint().width();
    const int columns = qBound(1, (available + kCardSpacing) / (cardWidth + kCardSpacing),
                               int(m_cards.size()));
    if (columns == m_columns) {
        return;
    }
    m_columns = columns;
    for (LabCard* card : std::as_const(m_cards)) {
        m_cardsGrid->removeWidget(card);
    }
    for (int i = 0; i < m_cards.size(); ++i) {
        m_cardsGrid->addWidget(m_cards[i], i / columns, i % columns, Qt::AlignTop);
    }
}

void LabPanel::showControllerFields() {
    const QString type = m_typeCombo->currentData().toString();
    m_qlpvFields->setVisible(type == QLatin1String("qlpv"));
    m_gpcFields->setVisible(type == QLatin1String("gpc"));
    m_pdFields->setVisible(type == QLatin1String("pd"));
}

QString LabPanel::lineFollowerProjectPath() {
    return QStringLiteral(":/tracelab/linefollower.tvproj");
}

QString LabPanel::currentPlantId() const {
    const QString picked = m_plantCombo->currentData().toString();
    if (!picked.isEmpty()) {
        return picked;
    }
    const QStringList ids = lf::LineDeviceRegistry::instance().plantIds();
    return ids.size() == 1 ? ids.first() : QString();
}

void LabPanel::addLineFollowerPlant() {
    if (m_host) {
        m_host->addModuleDevice(QString::fromLatin1(lf::kPlantDeviceType),
                                tr("TraceLab: Line follower"), lf::PlantSettings().toJson());
    }
}

void LabPanel::addController() {
    const QString plantId = currentPlantId();
    if (!m_host || plantId.isEmpty()) {
        return;  // the action is disabled without a plant
    }
    lf::ControllerSettings settings;
    settings.plantDeviceId = plantId;
    m_host->addModuleDevice(QString::fromLatin1(lf::kControllerDeviceType),
                            tr("TraceLab: Controller"), settings.toJson());
}

void LabPanel::addScene() {
    const QString plantId = currentPlantId();
    if (!m_host || plantId.isEmpty()) {
        return;  // the action is disabled without a plant
    }
    // The controller bound to this plant, if any (ctl.* topics).
    QString controllerId;
    for (const QString& id : lf::LineDeviceRegistry::instance().controllerIds()) {
        const lf::LineControllerDevice* c = lf::LineDeviceRegistry::instance().controller(id);
        if (c != nullptr && (c->settings().plantDeviceId == plantId || controllerId.isEmpty())) {
            controllerId = id;
        }
    }
    // Bound by name (docs/DASHBOARD.md "Telemetry bindings"): MainWindow
    // fills the ids from the device's catalog.
    QJsonObject bind;
    DiffDriveScene scene;
    for (const SceneInput& input : scene.inputs()) {
        const bool fromController = input.defaultTopic.startsWith(QLatin1String("ctl."));
        const QString deviceId = fromController ? controllerId : plantId;
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

lf::LinePlantDevice* LabPanel::plant() const {
    return lf::LineDeviceRegistry::instance().plant(m_plantCombo->currentData().toString());
}

lf::LineControllerDevice* LabPanel::controller() const {
    return lf::LineDeviceRegistry::instance().controller(
        m_controllerCombo->currentData().toString());
}

void LabPanel::play() {
    if (lf::LinePlantDevice* p = plant()) {
        p->run();
    }
}

void LabPanel::pauseSimulation() {
    if (lf::LinePlantDevice* p = plant()) {
        p->pause();
    }
}

void LabPanel::resetSimulation() {
    if (lf::LinePlantDevice* p = plant()) {
        p->reset();
    }
}

void LabPanel::stepSimulation() {
    if (lf::LinePlantDevice* p = plant()) {
        p->pause();
        p->step();
    }
}

void LabPanel::refreshDevices() {
    const auto fill = [](QComboBox* combo, const QStringList& ids) {
        const QString current = combo->currentData().toString();
        const QSignalBlocker blocker(combo);
        combo->clear();
        for (const QString& id : ids) {
            combo->addItem(id, id);
        }
        const int index = combo->findData(current);
        combo->setCurrentIndex(index >= 0 ? index : (ids.isEmpty() ? -1 : 0));
    };
    fill(m_plantCombo, lf::LineDeviceRegistry::instance().plantIds());
    fill(m_controllerCombo, lf::LineDeviceRegistry::instance().controllerIds());
    const bool any = m_plantCombo->count() > 0 || m_controllerCombo->count() > 0;
    m_emptyState->setVisible(!any);
    m_cardsScroll->setVisible(any);
    // The device ids may need wider fields.
    alignCards();
    // A controller and a scene are built around a plant.
    const bool hasPlant = m_plantCombo->count() > 0;
    m_addControllerAction->setEnabled(hasPlant);
    m_addSceneAction->setEnabled(hasPlant);
    refreshFromDevices();
}

void LabPanel::refreshFromDevices() {
    m_updating = true;
    if (lf::LinePlantDevice* p = plant()) {
        const lf::PlantSettings& s = p->settings();
        int speed = 0;
        if (s.runMode == lf::RunMode::AsFastAsPossible) {
            speed = 2;
        } else if (s.runMode == lf::RunMode::Lockstep) {
            speed = 3;
        } else if (s.timeScale > 1.5) {
            speed = 1;
        }
        m_speedCombo->setCurrentIndex(speed);
        m_rateSpin->setValue(s.fps);
        m_seedSpin->setValue(int(qBound<qint64>(qint64(-1), qint64(s.track.seed), qint64(2147483647))));
        m_trackCombo->setCurrentIndex(s.track.type == lf::TrackType::Circle ? 1 : 0);
        m_noiseCheck->setChecked(s.sensorNoise);
    }
    if (lf::LineControllerDevice* c = controller()) {
        const lf::ControllerSettings& s = c->settings();
        m_typeCombo->setCurrentIndex(qMax(0, m_typeCombo->findData(s.type)));
        m_filterSpin->setValue(s.filterTau);
        m_qLateralSpin->setValue(s.qLateral);
        m_schedulingCombo->setCurrentIndex(qMax(0, m_schedulingCombo->findData(s.qlpvScheduling)));
        m_embeddingCombo->setCurrentIndex(qMax(0, m_embeddingCombo->findData(s.qlpvEmbedding)));
        m_iterationsSpin->setValue(s.qlpvIterations);
        m_gpcLambdaSpin->setValue(s.gpcLambda);
        m_pdKpSpin->setValue(s.pdKp);
        m_pdKdSpin->setValue(s.pdKd);
        m_pdBaseSpin->setValue(s.pdBase);
    }
    m_updating = false;
    refreshStatus();
}

void LabPanel::refreshStatus() {
    const QLocale locale;
    lf::LinePlantDevice* p = plant();
    if (!p) {
        m_simulationCard->setStatus(LabCard::Tone::None, QString());
        for (QLabel* value : {m_tickValue, m_timeValue, m_coveredValue}) {
            value->setText(QStringLiteral("--"));
        }
        m_perturbationLabel->hide();
    } else {
        m_simulationCard->setStatus(p->running() ? LabCard::Tone::Success : LabCard::Tone::Warning,
                                    p->running() ? tr("running") : tr("paused"));
        m_tickValue->setText(locale.toString(qlonglong(p->tick())));
        m_timeValue->setText(tr("%1 s").arg(locale.toString(p->plant().time(), 'f', 2)));
        m_coveredValue->setText(
            tr("%1 %").arg(locale.toString(p->plant().progress().coverage(), 'f', 1)));
        const bool perturbed = p->perturbation() != 0.0;
        if (perturbed) {
            m_perturbationLabel->setText(
                tr("Perturbation: %1 %").arg(locale.toString(p->perturbation(), 'f', 1)));
        }
        m_perturbationLabel->setVisible(perturbed);
    }
    if (lf::LineControllerDevice* c = controller()) {
        const bool ready = c->host()->plantReady();
        m_controllerCard->setStatus(ready ? LabCard::Tone::Success : LabCard::Tone::Warning,
                                    ready ? tr("connected") : tr("waiting for the plant"));
    } else {
        m_controllerCard->setStatus(LabCard::Tone::None, QString());
    }
}

void LabPanel::applySpeed() {
    if (m_updating) {
        return;
    }
    const int index = qBound(0, m_speedCombo->currentIndex(), int(std::size(kSpeeds)) - 1);
    if (lf::LinePlantDevice* p = plant()) {
        p->setRunMode(kSpeeds[index].mode, kSpeeds[index].scale);
    }
}

void LabPanel::applyRate() {
    if (m_updating) {
        return;
    }
    lf::LinePlantDevice* p = plant();
    const int fps = m_rateSpin->value();
    if (!p || p->settings().fps == fps) {
        return;
    }
    // The controllers closing the loop on this plant must sample at its Ts.
    const QString plantId = m_plantCombo->currentData().toString();
    for (const QString& id : lf::LineDeviceRegistry::instance().controllerIds()) {
        lf::LineControllerDevice* c = lf::LineDeviceRegistry::instance().controller(id);
        if (c != nullptr && c->settings().plantDeviceId == plantId) {
            lf::ControllerSettings s = c->settings();
            s.fps = fps;
            c->setSettings(s);
        }
    }
    lf::PlantSettings s = p->settings();
    s.fps = fps;
    p->setSettings(s);
    p->reset();
}

void LabPanel::applyTrackAndSensors() {
    lf::LinePlantDevice* p = plant();
    if (!p) {
        return;
    }
    lf::PlantSettings s = p->settings();
    s.track.seed = m_seedSpin->value();
    s.track.type = m_trackCombo->currentIndex() == 1 ? lf::TrackType::Circle
                                                     : lf::TrackType::Lemniscate;
    s.sensorNoise = m_noiseCheck->isChecked();
    p->setSettings(s);
    p->reset();
}

void LabPanel::applyController() {
    if (m_updating) {
        return;
    }
    lf::LineControllerDevice* c = controller();
    if (!c) {
        return;
    }
    lf::ControllerSettings s = c->settings();
    s.type = m_typeCombo->currentData().toString();
    s.filterTau = m_filterSpin->value();
    s.qLateral = m_qLateralSpin->value();
    s.qlpvScheduling = m_schedulingCombo->currentData().toString();
    s.qlpvEmbedding = m_embeddingCombo->currentData().toString();
    s.qlpvIterations = m_iterationsSpin->value();
    s.gpcLambda = m_gpcLambdaSpin->value();
    s.pdKp = m_pdKpSpin->value();
    s.pdKd = m_pdKdSpin->value();
    s.pdBase = m_pdBaseSpin->value();
    c->setSettings(s);
}

}  // namespace tracelab
