#pragma once

#include "dashboard/widgetconfigeditor.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLineEdit;
class QSpinBox;

namespace traceview {

// Settings for PushButtonWidget (see widgets/controlwidgets.h): the label
// and color style shown on the button, plus the event/command shape a real
// data-source binding will eventually consume — Mode picks whether a click
// fires a press command only ("Pulse") or press-then-release commands
// ("Momentary"); Repeat re-fires the press command on an interval while
// held; Long Press adds a separate command that only fires once the button
// has been held past a threshold. Debounce and Confirm apply to every
// trigger regardless of mode. Like ChartConfigEditor, this only captures
// the shape — nothing consumes these commands yet, wiring them to a live
// output is later work.
class PushButtonConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit PushButtonConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    // Momentary's "On release" row and Repeat/Long Press's dependent rows
    // only make sense once their governing combo/checkbox says so.
    void updateRowsVisibility();
    void emitChanged();

    bool m_updating = false;

    QFormLayout* m_formLayout = nullptr;

    QComboBox* m_deviceCombo = nullptr;
    QLineEdit* m_labelEdit = nullptr;
    QComboBox* m_variantCombo = nullptr;
    QComboBox* m_modeCombo = nullptr;
    QLineEdit* m_onPressEdit = nullptr;
    QLineEdit* m_onReleaseEdit = nullptr;
    QCheckBox* m_repeatCheck = nullptr;
    QSpinBox* m_repeatIntervalSpin = nullptr;
    QCheckBox* m_longPressCheck = nullptr;
    QSpinBox* m_longPressThresholdSpin = nullptr;
    QLineEdit* m_longPressCommandEdit = nullptr;
    QSpinBox* m_debounceSpin = nullptr;
    QCheckBox* m_confirmCheck = nullptr;
};

// Settings for ToggleSwitchWidget: the label, the text shown in each state,
// which state it starts in, the commands sent on each transition, and
// whether flipping it needs confirmation first.
class ToggleSwitchConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit ToggleSwitchConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    void emitChanged();

    bool m_updating = false;

    QComboBox* m_deviceCombo = nullptr;
    QLineEdit* m_labelEdit = nullptr;
    QLineEdit* m_onLabelEdit = nullptr;
    QLineEdit* m_offLabelEdit = nullptr;
    QCheckBox* m_defaultOnCheck = nullptr;
    QLineEdit* m_onCommandEdit = nullptr;
    QLineEdit* m_offCommandEdit = nullptr;
    QCheckBox* m_confirmCheck = nullptr;
};

// Settings for SliderWidget: label, bounds/step, the starting value, an
// optional unit label, whether the current value is shown alongside the
// slider, and how it sends: continuously while dragging (throttled to a max
// rate) or only once the handle is released.
class SliderConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit SliderConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;
    void setAvailableDevices(const QVector<DeviceOption>& devices) override;

private:
    // Throttle only makes sense in "Continuous" send mode.
    void updateRowsVisibility();
    void emitChanged();

    bool m_updating = false;

    QFormLayout* m_formLayout = nullptr;

    QComboBox* m_deviceCombo = nullptr;
    QLineEdit* m_labelEdit = nullptr;
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QDoubleSpinBox* m_stepSpin = nullptr;
    QDoubleSpinBox* m_defaultSpin = nullptr;
    QLineEdit* m_unitEdit = nullptr;
    QCheckBox* m_showValueCheck = nullptr;
    QComboBox* m_sendModeCombo = nullptr;
    QSpinBox* m_throttleSpin = nullptr;
    QLineEdit* m_commandTemplateEdit = nullptr;
};

} // namespace traceview
