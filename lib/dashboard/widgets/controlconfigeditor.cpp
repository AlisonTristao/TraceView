#include "controlconfigeditor.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QIcon>
#include <QLineEdit>
#include <QPixmap>
#include <QSpinBox>

#include "dashboard/widgets/controldata.h"
#include "dashboard/widgets/controlstyle.h"
#include "traceview/thememanager.h"

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

// Stand-in for QFormLayout::setRowVisible(QWidget*, bool), which only exists
// from Qt 6.4 -- Ubuntu 22.04's packaged Qt is 6.2.
void setFormRowVisible(QFormLayout* layout, QWidget* fieldWidget, bool visible) {
    int row = -1;
    QFormLayout::ItemRole role;
    layout->getWidgetPosition(fieldWidget, &row, &role);
    if (row < 0) return;
    for (QFormLayout::ItemRole r : {QFormLayout::LabelRole, QFormLayout::FieldRole}) {
        if (QLayoutItem* item = layout->itemAt(row, r)) {
            if (QWidget* widget = item->widget()) widget->setVisible(visible);
        }
    }
}
const QStringList kButtonModeLabels = {
    QCoreApplication::translate("ControlConfigEditor", "Momentary (press + release)"),
    QCoreApplication::translate("ControlConfigEditor", "Pulse (single command)"),
};

// The Style combo every control editor shows: "App default" (follow View >
// Control Style) and then every control style, ids as itemData.
QComboBox* makeControlStyleCombo(QWidget* parent) {
    auto* combo = new QComboBox(parent);
    combo->addItem(QCoreApplication::translate("ControlConfigEditor", "App default"),
                   QString::fromLatin1(kControlStyleApp));
    for (ControlStyleId id : allControlStyles()) {
        combo->addItem(controlStyleDisplayName(id), controlStyleIdString(id));
    }
    combo->setToolTip(QCoreApplication::translate(
        "ControlConfigEditor",
        "How this control is drawn. App default follows View > Control Style."));
    return combo;
}

void selectControlStyle(QComboBox* combo, const QJsonObject& config) {
    const ControlStyleChoice choice = parseControlStyleChoice(config);
    const QString id = choice.followApp ? QString::fromLatin1(kControlStyleApp)
                                        : controlStyleIdString(choice.style);
    combo->setCurrentIndex(qMax(0, combo->findData(id)));
}

void writeControlStyle(QJsonObject& config, const QComboBox* combo) {
    config[QLatin1String(kControlStyleKey)] = combo->currentData().toString();
}

QIcon swatchIcon(const QColor& color) {
    QPixmap pixmap(14, 14);
    pixmap.fill(color);
    return QIcon(pixmap);
}

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

    m_styleCombo = makeControlStyleCombo(this);

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
    m_formLayout->addRow(tr("Style"), m_styleCombo);
    m_formLayout->addRow(tr("Color"), m_variantCombo);
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
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
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
    selectControlStyle(m_styleCombo, config);
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
    writeControlStyle(cfg, m_styleCombo);
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
    setFormRowVisible(m_formLayout, m_onReleaseEdit, momentary);
    setFormRowVisible(m_formLayout, m_repeatIntervalSpin, m_repeatCheck->isChecked());
    setFormRowVisible(m_formLayout, m_longPressThresholdSpin, m_longPressCheck->isChecked());
    setFormRowVisible(m_formLayout, m_longPressCommandEdit, m_longPressCheck->isChecked());
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

    m_styleCombo = makeControlStyleCombo(this);

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
    layout->addRow(tr("Style"), m_styleCombo);
    layout->addRow(tr("On text"), m_onLabelEdit);
    layout->addRow(tr("Off text"), m_offLabelEdit);
    layout->addRow(QString(), m_defaultOnCheck);
    layout->addRow(tr("On command"), m_onCommandEdit);
    layout->addRow(tr("Off command"), m_offCommandEdit);
    layout->addRow(QString(), m_confirmCheck);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
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
    selectControlStyle(m_styleCombo, config);
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
    writeControlStyle(cfg, m_styleCombo);
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

    m_styleCombo = makeControlStyleCombo(this);

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
    m_formLayout->addRow(tr("Style"), m_styleCombo);
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
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
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
    selectControlStyle(m_styleCombo, config);
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
    writeControlStyle(cfg, m_styleCombo);
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
    setFormRowVisible(m_formLayout, m_throttleSpin,
                      kSendModeIds.value(m_sendModeCombo->currentIndex()) == "continuous");
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

namespace {

// The LED color presets, after the drawing the LED was designed from.
struct LedColorPreset {
    const char* name;
    const char* hex;
};

const LedColorPreset kLedColorPresets[] = {
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Red"), "#EF4444"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Orange"), "#F97316"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Yellow"), "#FACC15"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Green"), "#22C55E"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Cyan"), "#06B6D4"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Blue"), "#3B82F6"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "Purple"), "#A855F7"},
    {QT_TRANSLATE_NOOP("ControlConfigEditor", "White"), "#F5F5F4"},
};

constexpr char kThemeColorData[] = "theme";
constexpr char kCustomColorData[] = "custom";

}  // namespace

LedConfigEditor::LedConfigEditor(QWidget* parent) : WidgetConfigEditor(parent) {
    m_deviceCombo = new QComboBox(this);
    populateDeviceCombo(m_deviceCombo, {});

    m_sourceIdEdit = new QLineEdit(this);
    m_sourceIdEdit->setReadOnly(true);
    m_sourceIdEdit->setPlaceholderText(tr("(auto)"));

    m_topicCombo = new QComboBox(this);
    m_topicCombo->setEditable(true);
    m_topicCombo->lineEdit()->setPlaceholderText(QStringLiteral("0x0101"));

    m_fieldCombo = new QComboBox(this);
    m_fieldCombo->setEditable(true);
    m_fieldCombo->setToolTip(
        tr("Which field of the topic drives the LED -- pick one the device has reported, or "
           "type its numeric id."));

    m_labelEdit = new QLineEdit(this);
    m_labelEdit->setPlaceholderText(tr("Status"));

    m_styleCombo = makeControlStyleCombo(this);

    m_colorModeCombo = new QComboBox(this);
    m_colorModeCombo->addItem(tr("One color"), ledColorModeId(LedColorMode::Fixed));
    m_colorModeCombo->addItem(tr("RGB from the field (0xRRGGBB)"),
                              ledColorModeId(LedColorMode::Rgb));

    m_conditionCombo = new QComboBox(this);
    m_conditionCombo->addItem(tr("Value is not zero"), ledConditionId(LedCondition::NonZero));
    m_conditionCombo->addItem(tr("Value is zero"), ledConditionId(LedCondition::Zero));
    m_conditionCombo->addItem(tr("Value is above"), ledConditionId(LedCondition::Above));
    m_conditionCombo->addItem(tr("Value is below"), ledConditionId(LedCondition::Below));

    m_thresholdSpin = new QDoubleSpinBox(this);
    m_thresholdSpin->setRange(-1'000'000.0, 1'000'000.0);
    m_thresholdSpin->setDecimals(3);
    m_thresholdSpin->setValue(0.5);

    m_colorCombo = new QComboBox(this);

    m_blinkCheck = new QCheckBox(tr("Blink while lit"), this);
    m_blinkSpin = new QSpinBox(this);
    m_blinkSpin->setRange(50, 10'000);
    m_blinkSpin->setSuffix(tr(" ms"));
    m_blinkSpin->setValue(500);

    m_onLabelEdit = new QLineEdit(this);
    m_onLabelEdit->setPlaceholderText(tr("Shown while lit"));
    m_offLabelEdit = new QLineEdit(this);
    m_offLabelEdit->setPlaceholderText(tr("Shown while off"));

    m_sampleTimeSpin = new QDoubleSpinBox(this);
    m_sampleTimeSpin->setRange(1.0, 3'600'000.0);
    m_sampleTimeSpin->setDecimals(0);
    m_sampleTimeSpin->setSuffix(tr(" ms"));
    m_sampleTimeSpin->setValue(100.0);
    m_sampleTimeSpin->setToolTip(tr("Requested update period."));

    m_formLayout = new QFormLayout(this);
    m_formLayout->setContentsMargins(0, 8, 0, 0);
    m_formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_formLayout->addRow(tr("Device"), m_deviceCombo);
    m_formLayout->addRow(tr("Source"), m_sourceIdEdit);
    m_formLayout->addRow(tr("Topic"), m_topicCombo);
    m_formLayout->addRow(tr("Field"), m_fieldCombo);
    m_formLayout->addRow(tr("Label"), m_labelEdit);
    m_formLayout->addRow(tr("Style"), m_styleCombo);
    m_formLayout->addRow(tr("Color mode"), m_colorModeCombo);
    m_formLayout->addRow(tr("Lit when"), m_conditionCombo);
    m_formLayout->addRow(tr("Threshold"), m_thresholdSpin);
    m_formLayout->addRow(tr("Color"), m_colorCombo);
    m_formLayout->addRow(QString(), m_blinkCheck);
    m_formLayout->addRow(tr("Blink period"), m_blinkSpin);
    m_formLayout->addRow(tr("On text"), m_onLabelEdit);
    m_formLayout->addRow(tr("Off text"), m_offLabelEdit);
    m_formLayout->addRow(tr("Sample period"), m_sampleTimeSpin);

    connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        populateTopicCombo(m_topicCombo, m_devices, m_deviceCombo->currentData().toString());
        updateIdentityDisplay();
        refreshFieldOptions();
        emitChanged();
    });
    connect(m_topicCombo, &QComboBox::activated, this, [this](int index) {
        QString sourceHex;
        QString topicHex;
        if (decodeTopicComboData(m_topicCombo->itemData(index), &sourceHex, &topicHex)) {
            const quint32 sourceId = quint32(sourceHex.toULongLong(nullptr, 0));
            const quint16 topicId = quint16(topicHex.toUInt(nullptr, 0));
            if (sourceId != m_sourceId || topicId != m_topicId) {
                m_topicName.clear();
                m_fieldCombo->setProperty("fieldName", QString());
            }
            m_sourceId = sourceId;
            m_topicId = topicId;
        }
        updateIdentityDisplay();
        refreshFieldOptions();
        emitChanged();
    });
    connect(m_topicCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const qulonglong typed = m_topicCombo->currentText().trimmed().toULongLong(&ok, 0);
        if (ok) {
            const quint16 topicId = quint16(qMin<qulonglong>(typed, 65535));
            quint32 sourceId = 0;
            const QString deviceId = m_deviceCombo->currentData().toString();
            for (const DeviceOption& device : m_devices) {
                if (device.id == deviceId) {
                    sourceId = device.selfSourceId;
                    break;
                }
            }
            if (sourceId != m_sourceId || topicId != m_topicId) {
                m_topicName.clear();
                m_fieldCombo->setProperty("fieldName", QString());
            }
            m_sourceId = sourceId;
            m_topicId = topicId;
        }
        updateIdentityDisplay();
        refreshFieldOptions();
        emitChanged();
    });
    connect(m_fieldCombo, &QComboBox::activated, this, [this](int index) {
        setBoundFieldId(m_fieldCombo, m_fieldCombo->itemData(index).toInt());
        emitChanged();
    });
    connect(m_fieldCombo->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        const int typed = m_fieldCombo->currentText().trimmed().toInt(&ok);
        if (ok) {
            setBoundFieldId(m_fieldCombo, typed);
        }
        emitChanged();
    });
    connect(m_labelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this](int) { emitChanged(); });
    connect(m_colorModeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_conditionCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_thresholdSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { emitChanged(); });
    connect(m_colorCombo, &QComboBox::activated, this, [this](int index) {
        const QString data = m_colorCombo->itemData(index).toString();
        if (data == QLatin1String(kThemeColorData)) {
            m_color = QColor();
        } else if (data == QLatin1String(kCustomColorData)) {
            const QColor picked = QColorDialog::getColor(
                m_color.isValid() ? m_color : QColor(Qt::green), this, tr("LED color"));
            if (picked.isValid()) {
                m_color = picked;
            }
        } else {
            m_color = QColor(data);
        }
        updateColorCombo();
        emitChanged();
    });
    connect(m_blinkCheck, &QCheckBox::toggled, this, [this](bool) {
        updateRowsVisibility();
        emitChanged();
    });
    connect(m_blinkSpin, &QSpinBox::valueChanged, this, [this](int) { emitChanged(); });
    connect(m_onLabelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_offLabelEdit, &QLineEdit::editingFinished, this, [this]() { emitChanged(); });
    connect(m_sampleTimeSpin, &QDoubleSpinBox::valueChanged, this,
            [this](double) { emitChanged(); });

    updateColorCombo();
    updateRowsVisibility();
}

void LedConfigEditor::setConfig(const QJsonObject& config) {
    m_updating = true;
    const LedConfig led = parseLedConfig(config);
    const int deviceIndex = m_deviceCombo->findData(config.value("deviceId").toString());
    m_deviceCombo->setCurrentIndex(deviceIndex >= 0 ? deviceIndex : 0);
    m_sourceId = led.sourceId;
    m_topicId = led.topicId;
    m_topicName = config.value("topicName").toString();
    m_fieldCombo->setProperty("fieldId", int(led.fieldId));
    m_fieldCombo->setProperty("fieldName", config.value("fieldName").toString());
    m_labelEdit->setText(led.label);
    selectControlStyle(m_styleCombo, config);
    m_colorModeCombo->setCurrentIndex(
        qMax(0, m_colorModeCombo->findData(ledColorModeId(led.colorMode))));
    m_conditionCombo->setCurrentIndex(
        qMax(0, m_conditionCombo->findData(ledConditionId(led.condition))));
    m_thresholdSpin->setValue(led.threshold);
    m_color = led.color;
    m_blinkCheck->setChecked(led.blink);
    m_blinkSpin->setValue(led.blinkMs);
    m_onLabelEdit->setText(led.onLabel);
    m_offLabelEdit->setText(led.offLabel);
    m_sampleTimeSpin->setValue(led.sampleTimeMs);
    populateTopicCombo(m_topicCombo, m_devices, m_deviceCombo->currentData().toString());
    updateIdentityDisplay();
    refreshFieldOptions();
    updateColorCombo();
    updateRowsVisibility();
    m_updating = false;
}

QJsonObject LedConfigEditor::config() const {
    QJsonObject cfg;
    cfg["deviceId"] = m_deviceCombo->currentData().toString();
    cfg["sourceId"] = formatHexId(m_sourceId, 8);
    cfg["topicId"] = formatHexId(m_topicId, 4);
    if (!m_topicName.isEmpty()) {
        cfg["topicName"] = m_topicName;
    }
    cfg["fieldId"] = m_fieldCombo->property("fieldId").toInt();
    const QString fieldName = m_fieldCombo->property("fieldName").toString();
    if (!fieldName.isEmpty()) {
        cfg["fieldName"] = fieldName;
    }
    cfg["label"] = m_labelEdit->text();
    writeControlStyle(cfg, m_styleCombo);
    cfg["colorMode"] = m_colorModeCombo->currentData().toString();
    cfg["condition"] = m_conditionCombo->currentData().toString();
    cfg["threshold"] = m_thresholdSpin->value();
    if (m_color.isValid()) {
        cfg["color"] = m_color.name();
    }
    cfg["blink"] = m_blinkCheck->isChecked();
    cfg["blinkMs"] = m_blinkSpin->value();
    cfg["onLabel"] = m_onLabelEdit->text();
    cfg["offLabel"] = m_offLabelEdit->text();
    cfg["sampleTimeMs"] = m_sampleTimeSpin->value();
    return cfg;
}

void LedConfigEditor::setAvailableDevices(const QVector<DeviceOption>& devices) {
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_devices = devices;
    populateDeviceCombo(m_deviceCombo, devices);
    populateTopicCombo(m_topicCombo, m_devices, m_deviceCombo->currentData().toString());
    updateIdentityDisplay();
    refreshFieldOptions();
    m_updating = wasUpdating;
}

void LedConfigEditor::updateRowsVisibility() {
    const bool fixed =
        m_colorModeCombo->currentData().toString() == ledColorModeId(LedColorMode::Fixed);
    const LedCondition condition = ledConditionFromId(m_conditionCombo->currentData().toString());
    const bool compares = condition == LedCondition::Above || condition == LedCondition::Below;
    setFormRowVisible(m_formLayout, m_conditionCombo, fixed);
    setFormRowVisible(m_formLayout, m_thresholdSpin, fixed && compares);
    setFormRowVisible(m_formLayout, m_colorCombo, fixed);
    setFormRowVisible(m_formLayout, m_blinkSpin, m_blinkCheck->isChecked());
}

void LedConfigEditor::updateIdentityDisplay() {
    const QString deviceId = m_deviceCombo->currentData().toString();
    const QString topicName = resolveCatalogTopicName(
        m_devices, deviceId, formatHexId(m_sourceId, 8), formatHexId(m_topicId, 4));
    m_topicCombo->setCurrentText(topicName.isEmpty() ? formatHexId(m_topicId, 4) : topicName);
    if (m_sourceId == 0) {
        m_sourceIdEdit->clear();
    } else {
        const QString sourceName = resolveSourceLabel(m_devices, m_sourceId);
        m_sourceIdEdit->setText(sourceName.isEmpty() ? formatHexId(m_sourceId, 8) : sourceName);
    }
}

void LedConfigEditor::refreshFieldOptions() {
    const QVector<CatalogTopicField> fields =
        resolveCatalogTopicFields(m_devices, m_deviceCombo->currentData().toString(),
                                  formatHexId(m_sourceId, 8), formatHexId(m_topicId, 4));
    QVector<CatalogTopicField> scalars;
    for (const CatalogTopicField& field : fields) {
        if (isPlottableScalarField(field)) {
            scalars.append(field);
        }
    }
    populateFieldCombo(m_fieldCombo, scalars);
    const int fieldId = m_fieldCombo->property("fieldId").toInt();
    QString shown = QString::number(fieldId);
    for (const CatalogTopicField& field : scalars) {
        if (field.fieldId == fieldId && !field.name.isEmpty()) {
            shown = field.name;
            break;
        }
    }
    m_fieldCombo->setCurrentText(shown);
}

void LedConfigEditor::updateColorCombo() {
    m_colorCombo->clear();
    m_colorCombo->addItem(swatchIcon(ThemeManager::instance().currentTheme().success),
                          tr("Palette (success)"), QString::fromLatin1(kThemeColorData));
    int selected = 0;
    for (const LedColorPreset& preset : kLedColorPresets) {
        const QColor color(QLatin1String(preset.hex));
        m_colorCombo->addItem(swatchIcon(color),
                              QCoreApplication::translate("ControlConfigEditor", preset.name),
                              color.name());
        if (m_color.isValid() && m_color == color) {
            selected = m_colorCombo->count() - 1;
        }
    }
    if (m_color.isValid() && selected == 0) {
        m_colorCombo->addItem(swatchIcon(m_color), m_color.name().toUpper(), m_color.name());
        selected = m_colorCombo->count() - 1;
    }
    m_colorCombo->addItem(tr("Custom..."), QString::fromLatin1(kCustomColorData));
    m_colorCombo->setCurrentIndex(selected);
}

void LedConfigEditor::emitChanged() {
    if (!m_updating) {
        emit configChanged();
    }
}

}  // namespace traceview
