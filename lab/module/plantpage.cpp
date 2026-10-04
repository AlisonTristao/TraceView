#include "plantpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "lab/code/codeblockowner.h"
#include "lab/module/blockeditor.h"
#include "lab/plant/labplantdevice.h"
#include "theme/doublespinbox.h"

namespace tracelab {

namespace {

constexpr int kFieldWidth = 180;

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

QLabel* fieldLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("labFieldLabel"));
    return label;
}

QLabel* heading(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

}  // namespace

PlantPage::PlantPage(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    m_title = heading(QString(), this);
    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_stats = new QHBoxLayout;
    m_stats->setSpacing(8);
    m_stats->addStretch();
    layout->addLayout(m_stats);
    addStat(tr("tick"));
    addStat(tr("time"));

    auto* columns = new QHBoxLayout;
    columns->setSpacing(32);
    auto* clockBox = new QWidget(this);
    auto* clockLayout = new QVBoxLayout(clockBox);
    clockLayout->setContentsMargins(0, 0, 0, 0);
    clockLayout->addWidget(heading(tr("Clock"), clockBox));
    QFormLayout* clockForm = makeForm(nullptr);
    clockLayout->addLayout(clockForm);
    m_speedCombo = new QComboBox(clockBox);
    m_speedCombo->addItem(tr("1x (real time)"));
    m_speedCombo->addItem(tr("2x"));
    m_speedCombo->addItem(tr("As fast as possible"));
    m_speedCombo->addItem(tr("Lockstep"));
    m_speedCombo->setItemData(3, tr("Lockstep: every tick waits for the controller's answer, "
                                    "so the run does not depend on how fast it computes."),
                              Qt::ToolTipRole);
    m_speedCombo->setMinimumWidth(kFieldWidth);
    clockForm->addRow(fieldLabel(tr("Speed"), clockBox), m_speedCombo);
    m_rateSpin = new QSpinBox(clockBox);
    m_rateSpin->setRange(1, 1000);
    m_rateSpin->setSuffix(tr(" Hz"));
    m_rateSpin->setMinimumWidth(kFieldWidth);
    m_rateSpin->setToolTip(tr("Step rate of the simulation: the plant steps and the controllers "
                              "on it sample at Ts = 1 / rate (tl_ts()). Changing it restarts "
                              "the run."));
    clockForm->addRow(fieldLabel(tr("Sample rate"), clockBox), m_rateSpin);
    clockLayout->addStretch();
    columns->addWidget(clockBox);

    auto* settingsColumn = new QWidget(this);
    auto* settingsLayout = new QVBoxLayout(settingsColumn);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->addWidget(heading(tr("Plant"), settingsColumn));
    m_settingsBox = new QWidget(settingsColumn);
    m_settingsForm = makeForm(m_settingsBox);
    settingsLayout->addWidget(m_settingsBox);
    m_applyButton = new QPushButton(tr("Apply and reset"), settingsColumn);
    m_applyButton->setEnabled(false);
    settingsLayout->addWidget(m_applyButton, 0, Qt::AlignLeft);
    settingsLayout->addStretch();
    columns->addWidget(settingsColumn);
    columns->addStretch();
    layout->addLayout(columns);
    // A plant written in C: its code, right under the clock and settings.
    m_code = new BlockEditor(this);
    layout->addWidget(m_code, 1);
    m_stretch = new QWidget(this);
    layout->addWidget(m_stretch, 1);

    connect(m_speedCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &PlantPage::applySpeed);
    connect(m_rateSpin, &QSpinBox::editingFinished, this, &PlantPage::applyRate);
    connect(m_applyButton, &QPushButton::clicked, this, &PlantPage::applySettings);
    setPlant(nullptr);
}

void PlantPage::addStat(const QString& caption) {
    auto* tile = new QFrame(this);
    tile->setObjectName(QStringLiteral("labStat"));
    auto* layout = new QVBoxLayout(tile);
    layout->setContentsMargins(10, 6, 10, 6);
    layout->setSpacing(0);
    auto* value = new QLabel(QStringLiteral("--"), tile);
    QFont font = value->font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * 1.3);
    }
    value->setFont(font);
    auto* label = new QLabel(caption, tile);
    label->setObjectName(QStringLiteral("labNote"));
    layout->addWidget(value);
    layout->addWidget(label);
    tile->setMinimumWidth(120);
    m_stats->insertWidget(m_stats->count() - 1, tile);  // before the stretch
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
    }
    m_title->setText(plant ? plant->typeName() : tr("No plant"));
    CodeBlockOwner* blocks = plant ? plant->codeBlocks() : nullptr;
    if (m_code->device() != blocks) {
        m_code->setBlock(blocks, 0);
    }
    m_code->setVisible(blocks != nullptr);
    m_stretch->setVisible(blocks == nullptr);
    refreshClock();
    rebuildSettings();
    refreshLive();
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
        auto* none = new QLabel(tr("This plant has no settings."), m_settingsBox);
        none->setObjectName(QStringLiteral("labNote"));
        m_settingsForm->addRow(none);
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
        auto* label = fieldLabel(setting.label, m_settingsBox);
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
    if (m_code->isVisible()) {
        m_code->refreshLive();
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
