#include "controlconfigeditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>

namespace traceview {

namespace {
// String ids are what's persisted in the config JSON — stable across
// re-orderings of the combo items; kVariantLabels is just the display text
// at the matching index (see ChartConfigEditor's kStyleIds for the same
// convention).
const QStringList kVariantIds = {"default", "success", "warning", "danger"};
const QStringList kVariantLabels = {"Default", "Success", "Warning", "Danger"};
} // namespace

PushButtonConfigEditor::PushButtonConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText("Button");

    m_variantCombo = new QComboBox(this);
    m_variantCombo->addItems(kVariantLabels);

    m_commandEdit = new QLineEdit(this);
    m_commandEdit->setPlaceholderText("Value sent when pressed");
    m_commandEdit->setToolTip("Value sent when pressed — wiring to a live output is pending.");

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow("Label", m_labelEdit);
    layout->addRow("Style", m_variantCombo);
    layout->addRow("Command", m_commandEdit);

    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_variantCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_commandEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
}

void PushButtonConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_labelEdit->setText(config.value("label").toString());
    m_variantCombo->setCurrentIndex(qMax(0, kVariantIds.indexOf(config.value("variant").toString("default"))));
    m_commandEdit->setText(config.value("command").toString());
    m_updating = false;
}

QJsonObject PushButtonConfigEditor::config() const {
    QJsonObject cfg;
    cfg["label"] = m_labelEdit->text();
    cfg["variant"] = kVariantIds.value(m_variantCombo->currentIndex(), kVariantIds.first());
    cfg["command"] = m_commandEdit->text();
    return cfg;
}

void PushButtonConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

ToggleSwitchConfigEditor::ToggleSwitchConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText("Toggle");

    m_onLabelEdit = new QLineEdit(this);
    m_onLabelEdit->setPlaceholderText("ON");

    m_offLabelEdit = new QLineEdit(this);
    m_offLabelEdit->setPlaceholderText("OFF");

    m_defaultOnCheck = new QCheckBox("Starts ON", this);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow("Label", m_labelEdit);
    layout->addRow("On text", m_onLabelEdit);
    layout->addRow("Off text", m_offLabelEdit);
    layout->addRow(QString(), m_defaultOnCheck);

    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_onLabelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_offLabelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_defaultOnCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
}

void ToggleSwitchConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_labelEdit->setText(config.value("label").toString());
    m_onLabelEdit->setText(config.value("onLabel").toString());
    m_offLabelEdit->setText(config.value("offLabel").toString());
    m_defaultOnCheck->setChecked(config.value("defaultState").toBool(false));
    m_updating = false;
}

QJsonObject ToggleSwitchConfigEditor::config() const {
    QJsonObject cfg;
    cfg["label"] = m_labelEdit->text();
    cfg["onLabel"] = m_onLabelEdit->text();
    cfg["offLabel"] = m_offLabelEdit->text();
    cfg["defaultState"] = m_defaultOnCheck->isChecked();
    return cfg;
}

void ToggleSwitchConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

SliderConfigEditor::SliderConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText("Slider");

    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-100'000.0, 100'000.0);
    m_minSpin->setDecimals(2);
    m_minSpin->setValue(0.0);

    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-100'000.0, 100'000.0);
    m_maxSpin->setDecimals(2);
    m_maxSpin->setValue(100.0);

    m_stepSpin = new QDoubleSpinBox(this);
    m_stepSpin->setRange(0.01, 100'000.0);
    m_stepSpin->setDecimals(2);
    m_stepSpin->setValue(1.0);

    m_defaultSpin = new QDoubleSpinBox(this);
    m_defaultSpin->setRange(-100'000.0, 100'000.0);
    m_defaultSpin->setDecimals(2);
    m_defaultSpin->setValue(50.0);

    m_unitEdit = new QLineEdit(this);
    m_unitEdit->setPlaceholderText("V, %, ...");

    m_showValueCheck = new QCheckBox("Show current value", this);
    m_showValueCheck->setChecked(true);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow("Label", m_labelEdit);
    layout->addRow("Min", m_minSpin);
    layout->addRow("Max", m_maxSpin);
    layout->addRow("Step", m_stepSpin);
    layout->addRow("Default", m_defaultSpin);
    layout->addRow("Unit", m_unitEdit);
    layout->addRow(QString(), m_showValueCheck);

    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_stepSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_defaultSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_showValueCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
}

void SliderConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    m_labelEdit->setText(config.value("label").toString());
    m_minSpin->setValue(config.value("min").toDouble(0.0));
    m_maxSpin->setValue(config.value("max").toDouble(100.0));
    m_stepSpin->setValue(config.value("step").toDouble(1.0));
    m_defaultSpin->setValue(config.value("defaultValue").toDouble(50.0));
    m_unitEdit->setText(config.value("unit").toString());
    m_showValueCheck->setChecked(config.value("showValue").toBool(true));
    m_updating = false;
}

QJsonObject SliderConfigEditor::config() const {
    QJsonObject cfg;
    cfg["label"] = m_labelEdit->text();
    cfg["min"] = m_minSpin->value();
    cfg["max"] = m_maxSpin->value();
    cfg["step"] = m_stepSpin->value();
    cfg["defaultValue"] = m_defaultSpin->value();
    cfg["unit"] = m_unitEdit->text();
    cfg["showValue"] = m_showValueCheck->isChecked();
    return cfg;
}

void SliderConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

} // namespace traceview
