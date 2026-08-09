#pragma once

#include "dashboard/dashboardwidget.h"

class QLabel;
class QPushButton;
class QSlider;

namespace traceview {

// A momentary action button — clicking it fires once and doesn't hold
// state, unlike ToggleSwitchWidget below. Purely visual for now:
// pressedRequested() has nowhere to send its value yet, same as
// SerialMonitorWidget::sendRequested — wiring it to a live output is a
// later, separate step. See PushButtonConfigEditor (controlconfigeditor.h)
// for the label/style/press-release/long-press/repeat settings captured in
// the meantime.
class PushButtonWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit PushButtonWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override { return false; }

signals:
    void pressedRequested();

private:
    QPushButton* m_button = nullptr;
};

// An on/off switch that holds its state between clicks, unlike
// PushButtonWidget above. Purely visual for now — toggled() has nowhere to
// send its value yet.
class ToggleSwitchWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit ToggleSwitchWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override { return false; }

signals:
    void toggled(bool checked);

private:
    QPushButton* m_switchButton = nullptr;
};

// A bounded value control (0-100 by default; real range is a
// SliderConfigEditor setting). Purely visual for now — valueChanged() has
// nowhere to send its value yet.
class SliderWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit SliderWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override { return false; }

signals:
    void valueChanged(int value);

private:
    QSlider* m_slider = nullptr;
    QLabel* m_valueLabel = nullptr;
};

} // namespace traceview
