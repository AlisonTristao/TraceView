#include "controlwidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace traceview {

// Turning WA_StyledBackground back off (so the container itself paints
// nothing and shows whatever's behind it) looked right in principle, but in
// practice it's exactly what dashboard/dashboardwidget.h's comment warns
// against: any pixel the actual control doesn't itself opaquely cover —
// and QPushButton's default vertical size policy is Fixed, so plenty were
// left uncovered below it — leaked DashboardCell's border and
// DashboardGrid's edit-mode grid lines (both painted in
// palette.borderStrong, i.e. a light, "white" line) right through. So these
// stay on DashboardWidget's default opaque fill instead — it already paints
// exactly palette.background, the same color as the canvas behind the cell
// (DashboardGrid never fills its own background — see its paintEvent), so
// covering the whole cell in it reads as "no panel" without any actual
// transparency to get wrong. Zero margins plus an Expanding size policy on
// the interactive control below make sure that fill has no gaps of its own
// to leak through, on top of it.

PushButtonWidget::PushButtonWidget(QWidget* parent) : DashboardWidget(parent) {
    m_button = new QPushButton("Push Button", this);
    m_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_button);

    connect(m_button, &QPushButton::clicked, this, &PushButtonWidget::pressedRequested);
}

ToggleSwitchWidget::ToggleSwitchWidget(QWidget* parent) : DashboardWidget(parent) {
    m_switchButton = new QPushButton("OFF", this);
    m_switchButton->setCheckable(true);
    m_switchButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // No header/title row — just the switch itself, filling the cell.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_switchButton);

    connect(m_switchButton, &QPushButton::toggled, this, [this](bool checked) {
        m_switchButton->setText(checked ? "ON" : "OFF");
        emit toggled(checked);
    });
}

SliderWidget::SliderWidget(QWidget* parent) : DashboardWidget(parent) {
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 100);
    m_slider->setValue(50);

    m_valueLabel = new QLabel("50", this);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setMinimumWidth(28);

    auto* sliderRow = new QHBoxLayout();
    sliderRow->addWidget(m_slider, 1);
    sliderRow->addWidget(m_valueLabel);

    // No header/title row — just the control itself, vertically centered in
    // whatever height the cell ends up with. Unlike the two widgets above,
    // the slider itself is left at its natural (thin) height rather than
    // stretched — a QSlider stretched tall just grows the dead space around
    // its groove, it doesn't make the groove itself any bigger.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addLayout(sliderRow);
    layout->addStretch(1);

    connect(m_slider, &QSlider::valueChanged, this, [this](int value) {
        m_valueLabel->setText(QString::number(value));
        emit valueChanged(value);
    });
}

} // namespace traceview
