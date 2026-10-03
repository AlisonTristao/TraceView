#include "labpanel.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "lab/benchmarks/linefollower/device/lfdevices.h"
#include "lab/control/controllerhost.h"

namespace tracelab {

namespace {

constexpr int kStatusIntervalMs = 250;

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

}  // namespace

LabPanel::LabPanel(std::function<bool(const QString&)> openProject, QWidget* parent)
    : QWidget(parent), m_openProject(std::move(openProject)) {
    m_newAction = new QAction(tr("New simulation"), this);
    m_newAction->setToolTip(tr("Opens the line-follower preset: the simulated plant, the "
                               "controller and a dashboard with the scene and charts."));
    m_playAction = new QAction(tr("Play"), this);
    m_pauseAction = new QAction(tr("Pause"), this);
    m_resetAction = new QAction(tr("Reset"), this);
    m_stepAction = new QAction(tr("Step"), this);
    connect(m_newAction, &QAction::triggered, this, &LabPanel::newLineFollowerSimulation);
    connect(m_playAction, &QAction::triggered, this, &LabPanel::play);
    connect(m_pauseAction, &QAction::triggered, this, &LabPanel::pauseSimulation);
    connect(m_resetAction, &QAction::triggered, this, &LabPanel::resetSimulation);
    connect(m_stepAction, &QAction::triggered, this, &LabPanel::stepSimulation);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto* newRow = new QHBoxLayout;
    auto* newButton = new QPushButton(tr("New simulation: Line follower"), this);
    connect(newButton, &QPushButton::clicked, this, &LabPanel::newLineFollowerSimulation);
    newRow->addWidget(newButton);
    newRow->addStretch();
    layout->addLayout(newRow);

    m_emptyLabel = new QLabel(tr("No TraceLab device in this project. Start a new simulation, "
                                 "or add a TraceLab device on the Devices tab."),
                              this);
    m_emptyLabel->setWordWrap(true);
    layout->addWidget(m_emptyLabel);

    m_controls = new QWidget(this);
    auto* controlsLayout = new QHBoxLayout(m_controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(16);
    layout->addWidget(m_controls);

    // Run.
    auto* runGroup = new QGroupBox(tr("Run"), m_controls);
    auto* runForm = new QFormLayout(runGroup);
    m_plantCombo = new QComboBox(runGroup);
    runForm->addRow(tr("Plant:"), m_plantCombo);
    auto* buttons = new QHBoxLayout;
    for (QAction* action : {m_playAction, m_pauseAction, m_resetAction, m_stepAction}) {
        auto* button = new QPushButton(action->text(), runGroup);
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        buttons->addWidget(button);
    }
    runForm->addRow(buttons);
    m_speedCombo = new QComboBox(runGroup);
    m_speedCombo->addItems({tr("1x (real time)"), tr("2x"), tr("As fast as possible"),
                            tr("Lockstep (waits for the controller)")});
    runForm->addRow(tr("Speed:"), m_speedCombo);
    m_statusLabel = new QLabel(runGroup);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    runForm->addRow(tr("Status:"), m_statusLabel);
    controlsLayout->addWidget(runGroup);

    // Track and sensors.
    auto* trackGroup = new QGroupBox(tr("Track and sensors"), m_controls);
    auto* trackForm = new QFormLayout(trackGroup);
    m_seedSpin = new QSpinBox(trackGroup);
    m_seedSpin->setRange(-1, 2147483647);
    m_seedSpin->setToolTip(tr("-1 draws a random track on every reset."));
    trackForm->addRow(tr("Track seed:"), m_seedSpin);
    m_trackCombo = new QComboBox(trackGroup);
    m_trackCombo->addItem(tr("Lemniscate"));
    m_trackCombo->addItem(tr("Circle"));
    trackForm->addRow(tr("Track type:"), m_trackCombo);
    m_noiseCheck = new QCheckBox(tr("Sensor noise"), trackGroup);
    m_noiseCheck->setToolTip(tr("Synthetic noise on the gyro and the line sensors (the "
                                "encoders always count whole ticks)."));
    trackForm->addRow(m_noiseCheck);
    auto* applyTrack = new QPushButton(tr("Apply and reset"), trackGroup);
    trackForm->addRow(applyTrack);
    controlsLayout->addWidget(trackGroup);

    // Controller.
    auto* controllerGroup = new QGroupBox(tr("Controller"), m_controls);
    auto* controllerForm = new QFormLayout(controllerGroup);
    m_controllerCombo = new QComboBox(controllerGroup);
    controllerForm->addRow(tr("Device:"), m_controllerCombo);
    m_typeCombo = new QComboBox(controllerGroup);
    m_typeCombo->addItem(tr("qLPV MPC"), QStringLiteral("qlpv"));
    m_typeCombo->addItem(tr("GPC"), QStringLiteral("gpc"));
    m_typeCombo->addItem(tr("PD (line sensors)"), QStringLiteral("pd"));
    controllerForm->addRow(tr("Controller:"), m_typeCombo);
    m_filterSpin = new QDoubleSpinBox(controllerGroup);
    m_filterSpin->setRange(0.0, 1.0);
    m_filterSpin->setDecimals(3);
    m_filterSpin->setSingleStep(0.005);
    m_filterSpin->setSuffix(tr(" s"));
    m_filterSpin->setSpecialValueText(tr("off"));
    m_filterSpin->setToolTip(tr("First-order low-pass on the measured wheel speeds (the C "
                                "filter runtime); 0 turns it off."));
    controllerForm->addRow(tr("Wheel-speed filter:"), m_filterSpin);
    m_qLateralSpin = new QDoubleSpinBox(controllerGroup);
    m_qLateralSpin->setRange(0.0, 1e7);
    m_qLateralSpin->setDecimals(1);
    controllerForm->addRow(tr("qLPV lateral weight:"), m_qLateralSpin);
    m_schedulingCombo = new QComboBox(controllerGroup);
    m_schedulingCombo->addItem(tr("Frozen (rho of now, whole horizon)"), QStringLiteral("frozen"));
    m_schedulingCombo->addItem(tr("Warm start (last prediction)"), QStringLiteral("warm_start"));
    m_schedulingCombo->addItem(tr("Taylor (first order around now)"), QStringLiteral("taylor"));
    m_schedulingCombo->addItem(tr("Iterative (re-solve until rho settles)"),
                               QStringLiteral("iterative"));
    m_schedulingCombo->setToolTip(
        tr("How the qLPV MPC fills the scheduling variable rho over the horizon."));
    controllerForm->addRow(tr("qLPV rho scheduling:"), m_schedulingCombo);
    m_embeddingCombo = new QComboBox(controllerGroup);
    m_embeddingCombo->addItem(QStringLiteral("sinc"), QStringLiteral("sinc"));
    m_embeddingCombo->addItem(QStringLiteral("sin_cos"), QStringLiteral("sin_cos"));
    controllerForm->addRow(tr("qLPV embedding:"), m_embeddingCombo);
    m_iterationsSpin = new QSpinBox(controllerGroup);
    m_iterationsSpin->setRange(1, 50);
    m_iterationsSpin->setToolTip(tr("Most QPs per step with the iterative scheduling."));
    controllerForm->addRow(tr("qLPV iterations:"), m_iterationsSpin);
    m_gpcLambdaSpin = new QDoubleSpinBox(controllerGroup);
    m_gpcLambdaSpin->setRange(0.0, 10.0);
    m_gpcLambdaSpin->setDecimals(5);
    m_gpcLambdaSpin->setSingleStep(0.0005);
    controllerForm->addRow(tr("GPC control weight:"), m_gpcLambdaSpin);
    m_pdKpSpin = new QDoubleSpinBox(controllerGroup);
    m_pdKpSpin->setRange(0.0, 1e5);
    m_pdKpSpin->setDecimals(0);
    m_pdKpSpin->setSingleStep(100.0);
    m_pdKpSpin->setSuffix(tr(" %/m"));
    m_pdKpSpin->setToolTip(tr("Wheel speed difference per metre of line offset under the "
                              "sensor array."));
    controllerForm->addRow(tr("PD Kp:"), m_pdKpSpin);
    m_pdKdSpin = new QDoubleSpinBox(controllerGroup);
    m_pdKdSpin->setRange(0.0, 1e4);
    m_pdKdSpin->setDecimals(1);
    m_pdKdSpin->setSingleStep(5.0);
    m_pdKdSpin->setSuffix(tr(" %s/m"));
    controllerForm->addRow(tr("PD Kd:"), m_pdKdSpin);
    m_pdBaseSpin = new QDoubleSpinBox(controllerGroup);
    m_pdBaseSpin->setRange(-100.0, 100.0);
    m_pdBaseSpin->setDecimals(0);
    m_pdBaseSpin->setSingleStep(5.0);
    m_pdBaseSpin->setSuffix(tr(" %"));
    m_pdBaseSpin->setToolTip(tr("Command on both wheels before the PD correction: the speed."));
    controllerForm->addRow(tr("PD base speed:"), m_pdBaseSpin);
    controlsLayout->addWidget(controllerGroup);
    controlsLayout->addStretch();
    layout->addStretch();

    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &LabPanel::applySpeed);
    connect(applyTrack, &QPushButton::clicked, this, &LabPanel::applyTrackAndSensors);
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

    auto* statusTimer = new QTimer(this);
    statusTimer->setInterval(kStatusIntervalMs);
    connect(statusTimer, &QTimer::timeout, this, &LabPanel::refreshStatus);
    statusTimer->start();
    refreshDevices();
}

QList<QAction*> LabPanel::ribbonActions() const {
    return {m_newAction, m_playAction, m_pauseAction, m_resetAction, m_stepAction};
}

QString LabPanel::lineFollowerPresetPath() {
    return QStringLiteral(":/tracelab/linefollower.tvproj");
}

void LabPanel::newLineFollowerSimulation() {
    if (m_openProject) {
        m_openProject(lineFollowerPresetPath());
    }
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
    m_emptyLabel->setVisible(!any);
    m_controls->setVisible(any);
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
    lf::LinePlantDevice* p = plant();
    if (!p) {
        m_statusLabel->clear();
        return;
    }
    QString text = (p->running() ? tr("running") : tr("paused")) +
                   tr(", tick %1, t = %2 s, covered %3 %")
                       .arg(p->tick())
                       .arg(p->plant().time(), 0, 'f', 2)
                       .arg(p->plant().progress().coverage(), 0, 'f', 1);
    if (p->perturbation() != 0.0) {
        text += tr(", perturbation %1 %").arg(p->perturbation(), 0, 'f', 1);
    }
    if (lf::LineControllerDevice* c = controller()) {
        text += c->host()->plantReady() ? tr(", controller connected")
                                        : tr(", controller waiting for the plant");
    }
    m_statusLabel->setText(text);
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
