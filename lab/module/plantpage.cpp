#include "plantpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include "lab/code/codeblockowner.h"
#include "lab/module/blockpage.h"
#include "lab/module/labui.h"
#include "lab/plant/labplantdevice.h"
#include "theme/doublespinbox.h"

namespace tracelab {

namespace {

constexpr int kFieldWidth = 170;
constexpr int kColumnMinWidth = 300;
constexpr int kColumnMaxWidth = 420;
constexpr int kStatWidth = 84;

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

QFormLayout* makeForm(QWidget* parent) {
    auto* form = new QFormLayout(parent);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    return form;
}

}  // namespace

PlantPage::PlantPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    // The code's columns and the header; the plant's own column goes first.
    m_page = new BlockPage(this);
    m_page->setAutoHeader(false);
    layout->addWidget(m_page);

    m_stats = new QWidget(this);
    auto* statsLayout = new QHBoxLayout(m_stats);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(8);
    m_page->header()->addExtra(m_stats);
    addStat(tr("tick"));
    addStat(tr("time"));

    auto* column = new QWidget(this);
    column->setMaximumWidth(kColumnMaxWidth);
    column->setMinimumWidth(kColumnMinWidth);
    auto* columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(12);

    auto* clock = new LabSection(tr("Clock"), column);
    QFormLayout* clockForm = makeForm(nullptr);
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
    columnLayout->addWidget(clock);

    m_settingsSection = new LabSection(tr("Settings"), column);
    auto* settingsScroll = new QScrollArea(m_settingsSection);
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsScroll->viewport()->setAutoFillBackground(false);
    m_settingsBox = new QWidget(settingsScroll);
    m_settingsBox->setAutoFillBackground(false);
    auto* settingsLayout = new QVBoxLayout(m_settingsBox);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    m_settingsForm = makeForm(nullptr);
    settingsLayout->addLayout(m_settingsForm);
    // Right under the fields it applies.
    m_applyButton = new QPushButton(tr("Apply and reset"), m_settingsBox);
    m_applyButton->setEnabled(false);
    m_applyButton->setToolTip(tr("The settings take effect on a reset of the run."));
    settingsLayout->addSpacing(4);
    settingsLayout->addWidget(m_applyButton, 0, Qt::AlignLeft);
    settingsLayout->addStretch();
    settingsScroll->setWidget(m_settingsBox);
    m_settingsSection->body()->addWidget(settingsScroll, 1);
    columnLayout->addWidget(m_settingsSection, 1);
    m_page->addLeadingColumn(column, 3);

    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &PlantPage::applySpeed);
    connect(m_rateSpin, &QSpinBox::editingFinished, this, &PlantPage::applyRate);
    connect(m_applyButton, &QPushButton::clicked, this, &PlantPage::applySettings);
    setPlant(nullptr);
}

void PlantPage::addStat(const QString& caption) {
    auto* tile = new QFrame(m_stats);
    tile->setObjectName(QStringLiteral("labStat"));
    auto* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(12, 5, 12, 5);
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
    m_stats->layout()->addWidget(tile);
    m_statValues.append(value);
    m_statCaptions.append(label);
}

void PlantPage::setPlant(LabPlantDevice* plant) {
    if (m_plant) {
        disconnect(m_plant, nullptr, this, nullptr);
    }
    m_plant = plant;
    setEnabled(plant != nullptr);
    m_pending.clear();
    if (plant) {
        connect(plant, &LabPlantDevice::settingsChanged, this, [this] {
            refreshClock();
            if (m_pending.isEmpty()) {
                rebuildSettings();
            }
        });
        connect(plant, &LabPlantDevice::runningChanged, this, &PlantPage::refreshHeader);
    }
    CodeBlockOwner* blocks = plant ? plant->codeBlocks() : nullptr;
    if (m_page->owner() != blocks) {
        m_page->setBlock(blocks, 0);
    }
    const QString world = plant ? plant->worldName() : QString();
    m_settingsSection->setTitle(world.isEmpty() ? tr("Settings") : world);
    refreshClock();
    rebuildSettings();
    refreshLive();
}

void PlantPage::refreshHeader() {
    LabPageHeader* header = m_page->header();
    if (!m_plant) {
        header->setKind(tr("Plant"));
        header->setTitle(tr("No plant"));
        header->setStatus(QString(), 0);
        return;
    }
    CodeBlockOwner* blocks = m_plant->codeBlocks();
    header->setKind(blocks ? tr("Plant  ·  C") : tr("Plant  ·  built into TraceView (C++)"));
    header->setTitle(blocks ? blocks->blockName(0) : m_plant->typeName());
    // A fault in the plant's code says more than "paused".
    const CodeBlockOwner::Status status = blocks ? blocks->status(0) : CodeBlockOwner::Status{};
    if (blocks && status.tone >= 2) {
        header->setStatus(status.text, status.tone);
    } else if (m_plant->running()) {
        header->setStatus(tr("running"), 1);
    } else {
        header->setStatus(tr("paused"), 0);
    }
}

void PlantPage::refreshClock() {
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

void PlantPage::rebuildSettings() {
    while (m_settingsForm->rowCount() > 0) {
        m_settingsForm->removeRow(0);
    }
    m_pending.clear();
    m_applyButton->setEnabled(false);
    if (!m_plant) {
        return;
    }
    const QVector<PlantSetting> schema = m_plant->settingsSchema();
    if (schema.isEmpty()) {
        m_settingsForm->addRow(ui::note(tr("This plant has no settings."), m_settingsBox));
        return;
    }
    for (const PlantSetting& setting : schema) {
        const QString key = setting.key;
        const auto changed = [this, key](const QVariant& value) {
            if (!m_updating) {
                m_pending.insert(key, value);
                m_applyButton->setEnabled(true);
            }
        };
        QWidget* field = nullptr;
        switch (setting.type) {
            case PlantSetting::Type::Bool: {
                auto* check = new QCheckBox(m_settingsBox);
                check->setChecked(setting.value.toBool());
                connect(check, &QCheckBox::toggled, this, changed);
                field = check;
                break;
            }
            case PlantSetting::Type::Enum: {
                auto* combo = new QComboBox(m_settingsBox);
                combo->addItems(setting.options);
                combo->setCurrentIndex(setting.value.toInt());
                connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, changed);
                field = combo;
                break;
            }
            case PlantSetting::Type::Int: {
                auto* spin = new QSpinBox(m_settingsBox);
                spin->setRange(int(setting.minimum), int(setting.maximum));
                spin->setValue(setting.value.toInt());
                spin->setSuffix(setting.unit);
                connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, changed);
                field = spin;
                break;
            }
            case PlantSetting::Type::Double: {
                auto* spin = new traceview::DoubleSpinBox(m_settingsBox);
                spin->setRange(setting.minimum, setting.maximum);
                spin->setDecimals(6);
                spin->setValue(setting.value.toDouble());
                if (!setting.unit.isEmpty()) {
                    spin->setSuffix(QLatin1Char(' ') + setting.unit);
                }
                connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, changed);
                field = spin;
                break;
            }
        }
        field->setMinimumWidth(kFieldWidth);
        field->setToolTip(setting.toolTip);
        auto* label = ui::fieldLabel(setting.label, m_settingsBox);
        label->setToolTip(setting.toolTip);
        m_settingsForm->addRow(label, field);
    }
}

void PlantPage::applySpeed() {
    if (m_updating || !m_plant) {
        return;
    }
    const int index = qBound(0, m_speedCombo->currentIndex(), int(std::size(kSpeeds)) - 1);
    m_plant->setRunMode(kSpeeds[index].mode, kSpeeds[index].scale);
}

void PlantPage::applyRate() {
    if (m_updating || !m_plant || m_rateSpin->value() == m_plant->sampleRate()) {
        return;
    }
    m_plant->setSampleRate(m_rateSpin->value());
}

void PlantPage::applySettings() {
    if (!m_plant) {
        return;
    }
    const QVariantMap values = m_pending;
    m_pending.clear();
    m_applyButton->setEnabled(false);
    m_plant->applySettings(values);
    rebuildSettings();
}

void PlantPage::refreshLive() {
    refreshHeader();
    m_page->refreshLive();  // the code's columns, and an action's line over the status
    const QLocale locale;
    if (!m_plant) {
        for (QLabel* value : std::as_const(m_statValues)) {
            value->setText(QStringLiteral("--"));
        }
        return;
    }
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
