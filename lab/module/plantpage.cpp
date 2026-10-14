#include "plantpage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "lab/code/codeblockowner.h"
#include "lab/control/labdevices.h"
#include "lab/module/labui.h"
#include "lab/plant/labplantdevice.h"
#include "theme/doublespinbox.h"

namespace tracelab {

namespace {

constexpr int kFieldWidth = 180;  // a Settings field's

QFormLayout* makeForm() {
    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    return form;
}

}  // namespace

PlantPage::PlantPage(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = ui::categoryPage(this, tr("Plant"), QString(), &m_description);

    // What it is.
    auto* about = new LabSection(tr("The plant"), this);
    QFormLayout* aboutForm = makeForm();
    about->body()->addLayout(aboutForm);
    m_kind = new QLabel(about);
    aboutForm->addRow(ui::fieldLabel(tr("Kind"), about), m_kind);
    m_device = new QLabel(about);
    m_device->setTextInteractionFlags(Qt::TextSelectableByMouse);
    aboutForm->addRow(ui::fieldLabel(tr("Device"), about), m_device);
    m_status = new LabStatusLine(about);
    aboutForm->addRow(ui::fieldLabel(tr("Status"), about), m_status);
    m_codeButton = new QPushButton(tr("Go to its code"), about);
    m_codeButton->setToolTip(tr("The plant's code on the Code page: where it comes from, the "
                                "compiler's messages, the buttons that open it."));
    aboutForm->addRow(QString(), m_codeButton);
    layout->addWidget(about);

    // Its world's settings.
    m_settingsSection = new LabSection(tr("Settings"), this);
    m_settingsBox = new QWidget(m_settingsSection);
    auto* settingsLayout = new QVBoxLayout(m_settingsBox);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(10);
    m_settingsForm = makeForm();
    settingsLayout->addLayout(m_settingsForm);
    // Right under the fields it applies.
    auto* applyRow = new QHBoxLayout;
    m_applyButton = new QPushButton(tr("Apply and reset"), m_settingsBox);
    m_applyButton->setEnabled(false);
    m_applyButton->setToolTip(tr("The settings take effect on a reset of the run."));
    applyRow->addWidget(m_applyButton);
    applyRow->addWidget(ui::note(tr("They take effect together, on a reset of the run."),
                                 m_settingsBox),
                        1);
    settingsLayout->addLayout(applyRow);
    m_settingsSection->body()->addWidget(m_settingsBox);
    layout->addWidget(m_settingsSection);
    layout->addStretch(1);

    connect(m_applyButton, &QPushButton::clicked, this, &PlantPage::applySettings);
    connect(m_codeButton, &QPushButton::clicked, this, &PlantPage::codeRequested);
    setPlant(nullptr);
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
            if (m_pending.isEmpty()) {
                rebuildSettings();
            }
        });
        connect(plant, &LabPlantDevice::runningChanged, this, &PlantPage::refreshLive);
    }
    CodeBlockOwner* blocks = plant ? plant->codeBlocks() : nullptr;
    if (!plant) {
        m_description->setText(QString());
        m_kind->setText(QString());
        m_device->setText(QString());
    } else {
        m_description->setText(
            blocks ? tr("%1, written in C: its code is on the Code page, its parameters on "
                        "Parameters. Here, what it is and the settings of its world.")
                         .arg(plant->typeName())
                   : tr("%1, built into TraceView (C++): its code cannot be seen or changed, "
                        "only the settings below.")
                         .arg(plant->typeName()));
        m_kind->setText(blocks ? tr("%1  ·  C").arg(plant->typeName())
                               : tr("%1  ·  built into TraceView (C++)").arg(plant->typeName()));
        m_device->setText(LabDeviceRegistry::instance().idOf(plant));
    }
    m_codeButton->setVisible(blocks != nullptr);
    const QString world = plant ? plant->worldName() : QString();
    m_settingsSection->setTitle(world.isEmpty() ? tr("Settings") : world);
    rebuildSettings();
    refreshLive();
}

void PlantPage::refreshLive() {
    if (!m_plant) {
        m_status->setStatus(QString(), 0);
        return;
    }
    // A fault in the plant's code says more than "paused".
    CodeBlockOwner* blocks = m_plant->codeBlocks();
    const CodeBlockOwner::Status status = blocks ? blocks->status(0) : CodeBlockOwner::Status{};
    if (blocks && status.tone >= 2) {
        m_status->setStatus(status.text, status.tone);
    } else if (m_plant->running()) {
        m_status->setStatus(tr("running"), 1);
    } else {
        m_status->setStatus(tr("paused"), 0);
    }
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
    m_updating = true;
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
    m_updating = false;
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

}  // namespace tracelab
