#include "simulationpage.h"

#include <QAction>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "lab/module/labui.h"

namespace tracelab {

namespace {

constexpr int kFieldWidth = 180;  // a Settings field's
constexpr int kStatWidth = 96;

// The speed choices, in combo order.
struct Speed {
    RunMode mode;
    double scale;
};
const Speed kSpeeds[] = {
    {RunMode::RealTime, 1.0},
    {RunMode::RealTime, 2.0},
    {RunMode::AsFastAsPossible, 1.0},
    {RunMode::Lockstep, 1.0},
};

}  // namespace

SimulationPage::SimulationPage(const QList<QAction*>& runActions, QWidget* parent)
    : QWidget(parent) {
    QVBoxLayout* layout = ui::categoryPage(
        this, tr("Simulation"),
        tr("How the run goes: start, pause or step it, and how fast its clock ticks. The plant "
           "steps and the blocks on it sample at the sample rate."));

    // The run: how it stands and what drives it.
    auto* run = new LabSection(tr("Run"), this);
    m_status = new LabStatusLine(run);
    run->body()->addWidget(m_status);
    m_stats = new QHBoxLayout;
    m_stats->setSpacing(8);
    m_stats->addStretch();
    run->body()->addLayout(m_stats);
    addStat(tr("tick"));
    addStat(tr("time"));
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    for (QAction* action : runActions) {
        auto* button = new QToolButton(run);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        buttons->addWidget(button);
    }
    buttons->addStretch();
    run->body()->addLayout(buttons);
    layout->addWidget(run);

    // The clock.
    auto* clock = new LabSection(tr("Clock"), this);
    auto* clockForm = new QFormLayout;
    clockForm->setContentsMargins(0, 0, 0, 0);
    clockForm->setHorizontalSpacing(18);
    clockForm->setVerticalSpacing(10);
    clockForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    clock->body()->addLayout(clockForm);
    m_speedCombo = new QComboBox(clock);
    m_speedCombo->addItem(tr("1x (real time)"));
    m_speedCombo->addItem(tr("2x"));
    m_speedCombo->addItem(tr("As fast as possible"));
    m_speedCombo->addItem(tr("Lockstep"));
    m_speedCombo->setItemData(3, tr("Lockstep: every tick waits for the controller's answer, "
                                    "so the run does not depend on how fast it computes."),
                              Qt::ToolTipRole);
    m_speedCombo->setMinimumWidth(kFieldWidth);
    clockForm->addRow(ui::fieldLabel(tr("Speed"), clock), m_speedCombo);
    m_rateSpin = new QSpinBox(clock);
    m_rateSpin->setRange(1, 1000);
    m_rateSpin->setSuffix(tr(" Hz"));
    m_rateSpin->setMinimumWidth(kFieldWidth);
    m_rateSpin->setToolTip(tr("Step rate of the simulation: the plant steps and the controllers "
                              "on it sample at Ts = 1 / rate (tl_ts()). Changing it restarts "
                              "the run."));
    clockForm->addRow(ui::fieldLabel(tr("Sample rate"), clock), m_rateSpin);
    clock->body()->addWidget(
        ui::note(tr("Lockstep waits for the controller every tick: the run does not depend on "
                    "how fast it computes. A new sample rate restarts the run."),
                 clock));
    layout->addWidget(clock);
    layout->addStretch(1);

    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &SimulationPage::applySpeed);
    connect(m_rateSpin, &QSpinBox::editingFinished, this, &SimulationPage::applyRate);
    setPlant(nullptr);
}

void SimulationPage::addStat(const QString& caption) {
    auto* tile = new QFrame(this);
    tile->setObjectName(QStringLiteral("labStat"));
    auto* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 6, 12, 6);
    layout->setSpacing(0);
    auto* value = new QLabel(QStringLiteral("--"), tile);
    QFont font = value->font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * 1.15);
    }
    font.setBold(true);
    value->setFont(font);
    auto* label = new QLabel(caption, tile);
    label->setObjectName(QStringLiteral("labStatCaption"));
    layout->addWidget(value);
    layout->addWidget(label);
    tile->setMinimumWidth(kStatWidth);
    // Before the stretch that keeps them to the left.
    m_stats->insertWidget(m_stats->count() - 1, tile);
    m_statValues.append(value);
    m_statCaptions.append(label);
}

void SimulationPage::setPlant(LabPlantDevice* plant) {
    if (m_plant) {
        disconnect(m_plant, nullptr, this, nullptr);
    }
    m_plant = plant;
    setEnabled(plant != nullptr);
    if (plant) {
        connect(plant, &LabPlantDevice::settingsChanged, this, &SimulationPage::refreshClock);
        connect(plant, &LabPlantDevice::runningChanged, this, &SimulationPage::refreshLive);
    }
    refreshClock();
    refreshLive();
}

void SimulationPage::refreshClock() {
    if (!m_plant) {
        return;
    }
    m_updating = true;
    int speed = 0;
    if (m_plant->runMode() == RunMode::AsFastAsPossible) {
        speed = 2;
    } else if (m_plant->runMode() == RunMode::Lockstep) {
        speed = 3;
    } else if (m_plant->timeScale() > 1.5) {
        speed = 1;
    }
    m_speedCombo->setCurrentIndex(speed);
    if (!m_rateSpin->hasFocus()) {
        m_rateSpin->setValue(m_plant->sampleRate());
    }
    m_updating = false;
}

void SimulationPage::applySpeed() {
    if (m_updating || !m_plant) {
        return;
    }
    const int index = qBound(0, m_speedCombo->currentIndex(), int(std::size(kSpeeds)) - 1);
    m_plant->setRunMode(kSpeeds[index].mode, kSpeeds[index].scale);
}

void SimulationPage::applyRate() {
    if (m_updating || !m_plant || m_rateSpin->value() == m_plant->sampleRate()) {
        return;
    }
    m_plant->setSampleRate(m_rateSpin->value());
}

void SimulationPage::refreshLive() {
    const QLocale locale;
    if (!m_plant) {
        m_status->setStatus(QString(), 0);
        for (QLabel* value : std::as_const(m_statValues)) {
            value->setText(QStringLiteral("--"));
        }
        return;
    }
    m_status->setStatus(m_plant->running() ? tr("running") : tr("paused"),
                        m_plant->running() ? 1 : 0);
    const QVector<QPair<QString, QString>> items = m_plant->statusItems();
    while (m_statValues.size() < 2 + items.size()) {
        addStat(QString());
    }
    m_statValues[0]->setText(locale.toString(qlonglong(m_plant->tick())));
    m_statValues[1]->setText(tr("%1 s").arg(locale.toString(m_plant->time(), 'f', 2)));
    for (int i = 0; i < m_statValues.size() - 2; ++i) {
        const bool used = i < items.size();
        m_statValues[2 + i]->parentWidget()->setVisible(used);
        if (used) {
            m_statCaptions[2 + i]->setText(items[i].first);
            m_statValues[2 + i]->setText(items[i].second);
        }
    }
}

}  // namespace tracelab
