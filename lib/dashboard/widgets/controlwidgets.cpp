#include "controlwidgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace traceview {

namespace {
// Push button/toggle switch inset from the cell edge (see below for why).
// Small enough to still read as "fills the cell", large enough that the
// QPushButton's own 4px QSS corner (stylesheet.cpp) never touches the
// DashboardWidget mask's 12px corner (kContainerCornerRadius) around it.
constexpr int kControlInset = 8;
} // namespace

// Turning WA_StyledBackground back off (so the container itself paints
// nothing and shows whatever's behind it) looked right in principle, but in
// practice it's exactly what dashboard/dashboardwidget.h's comment warns
// against: any pixel the actual control doesn't itself opaquely cover —
// and QPushButton's default vertical size policy is Fixed, so plenty were
// left uncovered below it — leaked DashboardCell's border and
// DashboardGrid's edit-mode grid lines (both painted in
// palette.borderStrong, i.e. a light, "white" line) right through. So these
// stay on DashboardWidget's default opaque fill instead. An Expanding size
// policy on the interactive control below makes sure that fill has no gaps
// of its own to leak through, on top of it. kControlInset margin around the
// layout doesn't reopen that gap -- it's still covered, just by
// DashboardWidget's own opaque fill (below) instead of the button.
//
// That inset itself: at zero margin, QPushButton used to sit flush against
// the cell edge, i.e. exactly on the same corner DashboardWidget's mask
// rounds at kContainerCornerRadius (12px, see roundedcorners.h). But the
// button draws its own corner via QSS at the separate, smaller 4px control
// radius (stylesheet.cpp) -- a tighter curve than the mask around it,
// leaving a wedge between the two where the button's border cuts inward
// before the panel's outer curve does. Reported live 2026-08-09 ("os cantos
// redondos estão estranhos"). kControlInset pulls the button off that edge
// so its own corner radius never has to coexist with the container's.
//
// The fill color itself is overridden away from DashboardWidget's default
// (palette.background, same as the canvas behind the cell) to palette.surface
// via the "dashboardControlPanel" property (see stylesheet.cpp) — with
// DashboardCell's cell border now rounded (see docs/VISUAL_IDENTITY.md), a
// fill that's indistinguishable from the canvas left the rounded corner
// reading as a stray curve with nothing behind it instead of a panel,
// especially with another widget's cell sitting nearby.

PushButtonWidget::PushButtonWidget(QWidget* parent) : DashboardWidget(parent) {
    setProperty("dashboardControlPanel", true);
    m_button = new QPushButton("Push Button", this);
    m_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlInset, kControlInset, kControlInset, kControlInset);
    layout->addWidget(m_button);

    m_repeatTimer = new QTimer(this);
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() { sendCommand(m_config.onPress); });

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    connect(m_longPressTimer, &QTimer::timeout, this, [this]() { sendCommand(m_config.longPressCommand); });

    connect(m_button, &QPushButton::clicked, this, &PushButtonWidget::pressedRequested);
    connect(m_button, &QAbstractButton::pressed, this, &PushButtonWidget::onButtonPressed);
    connect(m_button, &QAbstractButton::released, this, &PushButtonWidget::onButtonReleased);
}

void PushButtonWidget::setConfig(const QJsonObject& config) {
    m_config = parsePushButtonCommandConfig(config);
}

void PushButtonWidget::onButtonPressed() {
    m_pressSuppressed =
        m_config.debounceMs > 0 && m_debounceValid && m_debounceElapsed.elapsed() < m_config.debounceMs;
    if (m_pressSuppressed) {
        return;
    }
    m_debounceElapsed.start();
    m_debounceValid = true;

    sendCommand(m_config.onPress);
    if (m_config.repeatWhileHeld && m_config.repeatIntervalMs > 0) {
        m_repeatTimer->start(m_config.repeatIntervalMs);
    }
    if (m_config.longPressEnabled) {
        m_longPressTimer->start(m_config.longPressThresholdMs);
    }
}

void PushButtonWidget::onButtonReleased() {
    m_repeatTimer->stop();
    m_longPressTimer->stop();

    if (m_pressSuppressed) {
        m_pressSuppressed = false;
        return;
    }
    if (m_config.mode == PushButtonMode::Momentary) {
        sendCommand(m_config.onRelease);
    }
}

void PushButtonWidget::sendCommand(const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    emit sendRequested(text.toUtf8());
}

ToggleSwitchWidget::ToggleSwitchWidget(QWidget* parent) : DashboardWidget(parent) {
    setProperty("dashboardControlPanel", true);
    m_switchButton = new QPushButton("OFF", this);
    m_switchButton->setCheckable(true);
    m_switchButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // No header/title row — just the switch itself, filling the cell (inset
    // by kControlInset off the edge -- see the big comment above
    // PushButtonWidget's constructor for why that's needed).
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kControlInset, kControlInset, kControlInset, kControlInset);
    layout->addWidget(m_switchButton);

    connect(m_switchButton, &QPushButton::toggled, this, [this](bool checked) {
        m_switchButton->setText(checked ? "ON" : "OFF");
        emit toggled(checked);

        const QString& command = checked ? m_config.onCommand : m_config.offCommand;
        if (!command.isEmpty()) {
            emit sendRequested(command.toUtf8());
        }
    });
}

void ToggleSwitchWidget::setConfig(const QJsonObject& config) {
    m_config = parseToggleCommandConfig(config);
    if (!m_configInitialized) {
        // Blocked so restoring the configured starting state (fresh insert
        // or project load) never itself fires onCommand/offCommand.
        const QSignalBlocker blocker(m_switchButton);
        m_switchButton->setChecked(m_config.defaultState);
        m_switchButton->setText(m_config.defaultState ? "ON" : "OFF");
        m_configInitialized = true;
    }
}

SliderWidget::SliderWidget(QWidget* parent) : DashboardWidget(parent) {
    setProperty("dashboardControlPanel", true);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 100);
    m_slider->setValue(50);
    // Belt-and-suspenders alongside the groove/handle margin fix in
    // stylesheet.cpp: guarantees the widget itself is always tall enough to
    // fit the 16px handle circle without clipping it top/bottom, regardless
    // of whatever sizeHint the style computes for the customized groove.
    m_slider->setMinimumHeight(20);

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
    // Side margins keep the handle/label off the cell border -- unlike
    // PushButton/ToggleSwitch, this control isn't meant to fill the cell
    // edge-to-edge, and DashboardWidget's opaque background fill (see
    // dashboardwidget.h) already covers the newly-exposed margin strip, so
    // nothing leaks through it.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);
    layout->addStretch(1);
    layout->addLayout(sliderRow);
    layout->addStretch(1);

    connect(m_slider, &QSlider::valueChanged, this, &SliderWidget::onSliderValueChanged);
    connect(m_slider, &QSlider::sliderReleased, this, &SliderWidget::onSliderReleased);
}

void SliderWidget::setConfig(const QJsonObject& config) {
    m_config = parseSliderCommandConfig(config);

    // Blocked so applying the range/starting value never itself fires a
    // send -- only real user interaction (below) does.
    const QSignalBlocker blocker(m_slider);
    m_slider->setRange(0, sliderTickCount(m_config));
    if (!m_configInitialized) {
        m_slider->setValue(sliderValueToIndex(m_config, m_config.defaultValue));
        m_configInitialized = true;
    }

    m_valueLabel->setVisible(m_config.showValue);
    updateValueLabel(sliderIndexToValue(m_config, m_slider->value()));
}

void SliderWidget::onSliderValueChanged(int index) {
    const double value = sliderIndexToValue(m_config, index);
    updateValueLabel(value);
    // Kept as the raw tick index (this control's existing external signal,
    // not part of the serial output path) -- see sendRequested() for the
    // properly-scaled real-world value used in outbound commands.
    emit valueChanged(index);

    if (m_config.sendMode == SliderSendMode::Continuous) {
        scheduleContinuousSend(value);
    }
}

void SliderWidget::onSliderReleased() {
    if (m_config.sendMode == SliderSendMode::OnRelease) {
        sendCommandFor(sliderIndexToValue(m_config, m_slider->value()));
    }
}

void SliderWidget::updateValueLabel(double value) {
    m_valueLabel->setText(QString::number(value) + m_config.unit);
}

void SliderWidget::scheduleContinuousSend(double value) {
    m_pendingValue = value;
    m_hasPendingSend = true;
    if (m_throttleActive) {
        return;
    }

    sendCommandFor(value);
    m_hasPendingSend = false;
    m_throttleActive = true;
    QTimer::singleShot(qMax(1, m_config.throttleMs), this, [this]() {
        m_throttleActive = false;
        if (m_hasPendingSend) {
            sendCommandFor(m_pendingValue);
            m_hasPendingSend = false;
        }
    });
}

void SliderWidget::sendCommandFor(double value) {
    const QByteArray command = buildSliderCommand(m_config, value);
    if (!command.isEmpty()) {
        emit sendRequested(command);
    }
}

} // namespace traceview
