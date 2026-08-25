#include "controlconfigeditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>

namespace traceview {

namespace {
// String ids are what's persisted in the config JSON — stable across
// re-orderings of the combo items; kVariantLabels is just the display text
// at the matching index (see ChartConfigEditor's kStyleIds for the same
// convention).
//
// These lists are file-scope statics, not members of a QObject, so tr()
// isn't available here; use QCoreApplication::translate() with a shared
// context instead (same idiom as ThemePalette in palettes.cpp).
const QStringList kVariantIds = {"default", "success", "warning", "danger"};
const QStringList kVariantLabels = {
    QCoreApplication::translate("ControlConfigEditor", "Default"),
    QCoreApplication::translate("ControlConfigEditor", "Success"),
    QCoreApplication::translate("ControlConfigEditor", "Warning"),
    QCoreApplication::translate("ControlConfigEditor", "Danger"),
};

const QStringList kButtonModeIds = {"momentary", "pulse"};
const QStringList kButtonModeLabels = {
    QCoreApplication::translate("ControlConfigEditor", "Momentary (press + release)"),
    QCoreApplication::translate("ControlConfigEditor", "Pulse (single command)"),
};

const QStringList kSendModeIds = {"continuous", "onRelease"};
const QStringList kSendModeLabels = {
    QCoreApplication::translate("ControlConfigEditor", "Continuous (while dragging)"),
    QCoreApplication::translate("ControlConfigEditor", "On release"),
};
}  // namespace

PushButtonConfigEditor::PushButtonConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(tr("Which device this button's commands are sent to."));

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText(tr("Button"));

    m_variantCombo = new QComboBox(this);
    m_variantCombo->addItems(kVariantLabels);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItems(kButtonModeLabels);
    m_modeCombo->setToolTip(
        tr("Momentary sends the press command on press and the release command on release. "
           "Pulse sends only the press command, once per click."));

    m_onPressEdit = new QLineEdit(this);
    m_onPressEdit->setPlaceholderText(tr("Command sent on press"));

    m_onReleaseEdit = new QLineEdit(this);
    m_onReleaseEdit->setPlaceholderText(tr("Command sent on release"));

    m_repeatCheck = new QCheckBox(tr("Repeat while held"), this);

    m_repeatIntervalSpin = new QSpinBox(this);
    m_repeatIntervalSpin->setRange(10, 60'000);
    m_repeatIntervalSpin->setSuffix(tr(" ms"));
    m_repeatIntervalSpin->setValue(200);

    m_longPressCheck = new QCheckBox(tr("Long-press action"), this);

    m_longPressThresholdSpin = new QSpinBox(this);
    m_longPressThresholdSpin->setRange(100, 60'000);
    m_longPressThresholdSpin->setSuffix(tr(" ms"));
    m_longPressThresholdSpin->setValue(600);

    m_longPressCommandEdit = new QLineEdit(this);
    m_longPressCommandEdit->setPlaceholderText(tr("Command sent once held past the threshold"));

    m_debounceSpin = new QSpinBox(this);
    m_debounceSpin->setRange(0, 10'000);
    m_debounceSpin->setSuffix(tr(" ms"));
    m_debounceSpin->setValue(150);
    m_debounceSpin->setToolTip(
        tr("Minimum time between triggers — extra presses inside this window are ignored."));

    m_confirmCheck = new QCheckBox(tr("Confirm before sending"), this);

    m_formLayout = new QFormLayout(this);
    m_formLayout->setContentsMargins(0, 8, 0, 0);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(tr("Device"), m_deviceCombo);
    m_formLayout->addRow(tr("Label"), m_labelEdit);
    m_formLayout->addRow(tr("Style"), m_variantCombo);
    m_formLayout->addRow(tr("Mode"), m_modeCombo);
    m_formLayout->addRow(tr("On press"), m_onPressEdit);
    m_formLayout->addRow(tr("On release"), m_onReleaseEdit);
    m_formLayout->addRow(QString(), m_repeatCheck);
    m_formLayout->addRow(tr("Repeat interval"), m_repeatIntervalSpin);
    m_formLayout->addRow(QString(), m_longPressCheck);
    m_formLayout->addRow(tr("Long-press time"), m_longPressThresholdSpin);
    m_formLayout->addRow(tr("Long-press command"), m_longPressCommandEdit);
    m_formLayout->addRow(tr("Debounce"), m_debounceSpin);
    m_formLayout->addRow(QString(), m_confirmCheck);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_variantCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_onPressEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_onReleaseEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_repeatCheck, &QCheckBox::toggled, this, [this](bool) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_repeatIntervalSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_longPressCheck, &QCheckBox::toggled, this, [this](bool) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_longPressThresholdSpin, &QSpinBox::valueChanged, this,
            [this](int) { emitChanged(); });
    connect(m_longPressCommandEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_debounceSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_confirmCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });

    updateRowsVisibility();
}

void PushButtonConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    m_labelEdit->setText(config.value("label").toString());
    m_variantCombo->setCurrentIndex(
        qMax(0, kVariantIds.indexOf(config.value("variant").toString("default"))));
    m_modeCombo->setCurrentIndex(
        qMax(0, kButtonModeIds.indexOf(config.value("mode").toString("momentary"))));
    m_onPressEdit->setText(config.value("onPress").toString());
    m_onReleaseEdit->setText(config.value("onRelease").toString());
    m_repeatCheck->setChecked(config.value("repeatWhileHeld").toBool(false));
    m_repeatIntervalSpin->setValue(config.value("repeatIntervalMs").toInt(200));

    const QJsonObject longPress = config.value("longPress").toObject();
    m_longPressCheck->setChecked(longPress.value("enabled").toBool(false));
    m_longPressThresholdSpin->setValue(longPress.value("thresholdMs").toInt(600));
    m_longPressCommandEdit->setText(longPress.value("command").toString());

    m_debounceSpin->setValue(config.value("debounceMs").toInt(150));
    m_confirmCheck->setChecked(config.value("confirmBeforeSend").toBool(false));

    updateRowsVisibility();
    m_updating = false;
}

QJsonObject PushButtonConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    cfg["label"] = m_labelEdit->text();
    cfg["variant"] = kVariantIds.value(m_variantCombo->currentIndex(), kVariantIds.first());
    cfg["mode"] = kButtonModeIds.value(m_modeCombo->currentIndex(), kButtonModeIds.first());
    cfg["onPress"] = m_onPressEdit->text();
    cfg["onRelease"] = m_onReleaseEdit->text();
    cfg["repeatWhileHeld"] = m_repeatCheck->isChecked();
    cfg["repeatIntervalMs"] = m_repeatIntervalSpin->value();

    QJsonObject longPress;
    longPress["enabled"] = m_longPressCheck->isChecked();
    longPress["thresholdMs"] = m_longPressThresholdSpin->value();
    longPress["command"] = m_longPressCommandEdit->text();
    cfg["longPress"] = longPress;

    cfg["debounceMs"] = m_debounceSpin->value();
    cfg["confirmBeforeSend"] = m_confirmCheck->isChecked();
    return cfg;
}

void PushButtonConfigEditor::updateRowsVisibility() {
    const bool momentary = kButtonModeIds.value(m_modeCombo->currentIndex()) == "momentary";
    m_formLayout->setRowVisible(m_onReleaseEdit, momentary);
    m_formLayout->setRowVisible(m_repeatIntervalSpin, m_repeatCheck->isChecked());
    m_formLayout->setRowVisible(m_longPressThresholdSpin, m_longPressCheck->isChecked());
    m_formLayout->setRowVisible(m_longPressCommandEdit, m_longPressCheck->isChecked());
}

void PushButtonConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    populateDeviceCombo(m_deviceCombo, devices);
    m_updating = wasUpdating;
}

void PushButtonConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

ToggleSwitchConfigEditor::ToggleSwitchConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(tr("Which device this toggle's commands are sent to."));

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText(tr("Toggle"));

    m_onLabelEdit = new QLineEdit(this);
    m_onLabelEdit->setPlaceholderText(tr("ON"));

    m_offLabelEdit = new QLineEdit(this);
    m_offLabelEdit->setPlaceholderText(tr("OFF"));

    m_defaultOnCheck = new QCheckBox(tr("Starts ON"), this);

    m_onCommandEdit = new QLineEdit(this);
    m_onCommandEdit->setPlaceholderText(tr("Command sent when turned ON"));

    m_offCommandEdit = new QLineEdit(this);
    m_offCommandEdit->setPlaceholderText(tr("Command sent when turned OFF"));

    m_confirmCheck = new QCheckBox(tr("Confirm before toggling"), this);

    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->addRow(tr("Device"), m_deviceCombo);
    layout->addRow(tr("Label"), m_labelEdit);
    layout->addRow(tr("On text"), m_onLabelEdit);
    layout->addRow(tr("Off text"), m_offLabelEdit);
    layout->addRow(QString(), m_defaultOnCheck);
    layout->addRow(tr("On command"), m_onCommandEdit);
    layout->addRow(tr("Off command"), m_offCommandEdit);
    layout->addRow(QString(), m_confirmCheck);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_onLabelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_offLabelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_defaultOnCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
    connect(m_onCommandEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_offCommandEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_confirmCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
}

void ToggleSwitchConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    m_labelEdit->setText(config.value("label").toString());
    m_onLabelEdit->setText(config.value("onLabel").toString());
    m_offLabelEdit->setText(config.value("offLabel").toString());
    m_defaultOnCheck->setChecked(config.value("defaultState").toBool(false));
    m_onCommandEdit->setText(config.value("onCommand").toString());
    m_offCommandEdit->setText(config.value("offCommand").toString());
    m_confirmCheck->setChecked(config.value("confirmBeforeToggle").toBool(false));
    m_updating = false;
}

QJsonObject ToggleSwitchConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    cfg["label"] = m_labelEdit->text();
    cfg["onLabel"] = m_onLabelEdit->text();
    cfg["offLabel"] = m_offLabelEdit->text();
    cfg["defaultState"] = m_defaultOnCheck->isChecked();
    cfg["onCommand"] = m_onCommandEdit->text();
    cfg["offCommand"] = m_offCommandEdit->text();
    cfg["confirmBeforeToggle"] = m_confirmCheck->isChecked();
    return cfg;
}

void ToggleSwitchConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    populateDeviceCombo(m_deviceCombo, devices);
    m_updating = wasUpdating;
}

void ToggleSwitchConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

SliderConfigEditor::SliderConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});
    m_deviceCombo->setToolTip(tr("Which device this slider's commands are sent to."));

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText(tr("Slider"));

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
    m_unitEdit->setPlaceholderText(tr("V, %, ..."));

    m_showValueCheck = new QCheckBox(tr("Show current value"), this);
    m_showValueCheck->setChecked(true);

    m_sendModeCombo = new QComboBox(this);
    m_sendModeCombo->addItems(kSendModeLabels);
    m_sendModeCombo->setToolTip(
        tr("Continuous sends the command on every step while dragging, throttled below. "
           "On release sends it once, when the handle is let go."));

    m_throttleSpin = new QSpinBox(this);
    m_throttleSpin->setRange(10, 10'000);
    m_throttleSpin->setSuffix(tr(" ms"));
    m_throttleSpin->setValue(100);
    m_throttleSpin->setToolTip(
        tr("Minimum time between sends while dragging, so every pixel of motion isn't its own "
           "message."));

    m_commandTemplateEdit = new QLineEdit(this);
    m_commandTemplateEdit->setPlaceholderText(tr("SET {value}"));
    m_commandTemplateEdit->setToolTip(
        tr("Command sent on change — {value} is replaced with the current slider value."));

    m_formLayout = new QFormLayout(this);
    m_formLayout->setContentsMargins(0, 8, 0, 0);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(tr("Device"), m_deviceCombo);
    m_formLayout->addRow(tr("Label"), m_labelEdit);
    m_formLayout->addRow(tr("Min"), m_minSpin);
    m_formLayout->addRow(tr("Max"), m_maxSpin);
    m_formLayout->addRow(tr("Step"), m_stepSpin);
    m_formLayout->addRow(tr("Default"), m_defaultSpin);
    m_formLayout->addRow(tr("Unit"), m_unitEdit);
    m_formLayout->addRow(QString(), m_showValueCheck);
    m_formLayout->addRow(tr("Send"), m_sendModeCombo);
    m_formLayout->addRow(tr("Throttle"), m_throttleSpin);
    m_formLayout->addRow(tr("Command"), m_commandTemplateEdit);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_minSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_maxSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_stepSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_defaultSpin, &QDoubleSpinBox::valueChanged, this, [this](double) { emitChanged(); });
    connect(m_unitEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_showValueCheck, &QCheckBox::toggled, this, [this](bool) { emitChanged(); });
    connect(m_sendModeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_throttleSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_commandTemplateEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });

    updateRowsVisibility();
}

void SliderConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const int deviceIdx = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIdx >= 0 ? deviceIdx : 0);
    m_labelEdit->setText(config.value("label").toString());
    m_minSpin->setValue(config.value("min").toDouble(0.0));
    m_maxSpin->setValue(config.value("max").toDouble(100.0));
    m_stepSpin->setValue(config.value("step").toDouble(1.0));
    m_defaultSpin->setValue(config.value("defaultValue").toDouble(50.0));
    m_unitEdit->setText(config.value("unit").toString());
    m_showValueCheck->setChecked(config.value("showValue").toBool(true));
    m_sendModeCombo->setCurrentIndex(
        qMax(0, kSendModeIds.indexOf(config.value("sendMode").toString("continuous"))));
    m_throttleSpin->setValue(config.value("throttleMs").toInt(100));
    m_commandTemplateEdit->setText(config.value("commandTemplate").toString());
    updateRowsVisibility();
    m_updating = false;
}

QJsonObject SliderConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    cfg["label"] = m_labelEdit->text();
    cfg["min"] = m_minSpin->value();
    cfg["max"] = m_maxSpin->value();
    cfg["step"] = m_stepSpin->value();
    cfg["defaultValue"] = m_defaultSpin->value();
    cfg["unit"] = m_unitEdit->text();
    cfg["showValue"] = m_showValueCheck->isChecked();
    cfg["sendMode"] = kSendModeIds.value(m_sendModeCombo->currentIndex(), kSendModeIds.first());
    cfg["throttleMs"] = m_throttleSpin->value();
    cfg["commandTemplate"] = m_commandTemplateEdit->text();
    return cfg;
}

void SliderConfigEditor::updateRowsVisibility() {
    m_formLayout->setRowVisible(
        m_throttleSpin, kSendModeIds.value(m_sendModeCombo->currentIndex()) == "continuous");
}

void SliderConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    populateDeviceCombo(m_deviceCombo, devices);
    m_updating = wasUpdating;
}

void SliderConfigEditor::emitChanged() {
    if (m_updating) {
        return;
    }
    emit configChanged();
}

}  // namespace traceview
