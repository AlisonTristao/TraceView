#pragma once

#include "dashboard/widgetconfigeditor.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;

namespace traceview {

// Settings for PushButtonWidget (see widgets/controlwidgets.h): the label
// shown on the button, a color style for at-a-glance meaning (e.g. a green
// "Start" vs. a red "Stop"), and the command/value it should send when
// pressed. Like ChartConfigEditor, this only captures the shape — nothing
// consumes "command" yet, wiring it to a live output is later work.
class PushButtonConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit PushButtonConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    void emitChanged();

    bool m_updating = false;

    QLineEdit* m_labelEdit = nullptr;
    QComboBox* m_variantCombo = nullptr;
    QLineEdit* m_commandEdit = nullptr;
};

// Settings for ToggleSwitchWidget: the label, the text shown in each state,
// and which state it starts in.
class ToggleSwitchConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit ToggleSwitchConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    void emitChanged();

    bool m_updating = false;

    QLineEdit* m_labelEdit = nullptr;
    QLineEdit* m_onLabelEdit = nullptr;
    QLineEdit* m_offLabelEdit = nullptr;
    QCheckBox* m_defaultOnCheck = nullptr;
};

// Settings for SliderWidget: label, bounds/step, the starting value, an
// optional unit label, and whether the current value is shown alongside
// the slider.
class SliderConfigEditor : public WidgetConfigEditor {
    Q_OBJECT

public:
    explicit SliderConfigEditor(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    QJsonObject config() const override;

private:
    void emitChanged();

    bool m_updating = false;

    QLineEdit* m_labelEdit = nullptr;
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QDoubleSpinBox* m_stepSpin = nullptr;
    QDoubleSpinBox* m_defaultSpin = nullptr;
    QLineEdit* m_unitEdit = nullptr;
    QCheckBox* m_showValueCheck = nullptr;
};

} // namespace traceview
