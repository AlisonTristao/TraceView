#include "controlwidgets.h"

#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include "traceview/thememanager.h"

namespace traceview {

namespace {
// Push button inset from the cell edge (see below for why). Small enough to
// still read as "fills the cell", large enough that the QPushButton's own
// 4px QSS corner (stylesheet.cpp) never touches the DashboardWidget mask's
// 12px corner (kContainerCornerRadius) around it.
constexpr int kControlInset = 8;

// Toggles the QSS-driven "dashboardControlPanel" surface fill (see
// stylesheet.cpp) that the 3 headerless controls opt into. Shown only in
// edit/Layout mode -- see the big comment below -- and removed in Run so
// only the control itself sits on the canvas, with no background box behind
// it. unpolish()+polish() forces Qt to re-evaluate the QSS `[property=...]`
// selector against the changed dynamic property; a plain setProperty() alone
// doesn't reliably repaint it.
void setControlPanelFill(QWidget* widget, bool editMode) {
    widget->setProperty("dashboardControlPanel", editMode);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

constexpr int kSwitchWidth = 44;
constexpr int kSwitchHeight = 24;
constexpr int kSwitchThumbMargin = 3;
// Matches kSelectionAnimMs (dashboardcell.cpp) / the ~150ms discrete-state
// transition convention in docs/VISUAL_IDENTITY.md's Motion section.
constexpr int kSwitchAnimMs = 150;

QColor blendColor(const QColor& a, const QColor& b, qreal t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                             a.blueF() + (b.blueF() - a.blueF()) * t);
}
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
// especially with another widget's cell sitting nearby. That reasoning only
// holds while arranging, though -- in Run there's no neighboring chrome to
// disambiguate against, and a surface-colored box behind a control that's
// otherwise flat everywhere else just reads as visual clutter. So this fill
// is edit-mode-only now: setEditModeHint() (see setControlPanelFill() above)
// turns "dashboardControlPanel" on while m_editMode is true and off in Run,
// instead of it being permanently on like it used to be.

PushButtonWidget::PushButtonWidget(QWidget* parent) : DashboardWidget(parent) {
    m_button = new QPushButton(tr("Push Button"), this);
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
    m_button->setText(m_config.label.isEmpty() ? tr("Push Button") : m_config.label);
}

void PushButtonWidget::setEditModeHint(bool editMode) {
    setControlPanelFill(this, editMode);
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

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    m_slideAnim.setDuration(kSwitchAnimMs);
    m_slideAnim.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&m_slideAnim, &QVariantAnimation::valueChanged, this, QOverload<>::of(&QWidget::update));
    connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::onToggled);
}

QSize ToggleSwitch::sizeHint() const {
    return QSize(kSwitchWidth, kSwitchHeight);
}

void ToggleSwitch::onToggled(bool checked) {
    // `checked` is the state we're animating TO -- while idle, the switch
    // was showing the opposite value right up until this toggled(), so that
    // (not currentValue(), which is stale/invalid once idle) is the correct
    // slide start point.
    const qreal from = m_slideAnim.state() == QAbstractAnimation::Running ? m_slideAnim.currentValue().toReal()
                                                                           : (checked ? 0.0 : 1.0);
    m_slideAnim.stop();
    m_slideAnim.setStartValue(from);
    m_slideAnim.setEndValue(checked ? 1.0 : 0.0);
    m_slideAnim.start();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    const ThemePalette& palette = ThemeManager::instance().currentTheme();
    const qreal t =
        m_slideAnim.state() == QAbstractAnimation::Running ? m_slideAnim.currentValue().toReal() : (isChecked() ? 1.0 : 0.0);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF track(0.5, 0.5, width() - 1.0, height() - 1.0);
    const qreal trackRadius = track.height() / 2.0;
    painter.setPen(QPen(isEnabled() ? palette.border : palette.textDisabled, 1));
    painter.setBrush(blendColor(palette.surfaceAlt, palette.accent, t));
    painter.drawRoundedRect(track, trackRadius, trackRadius);

    const qreal thumbDiameter = track.height() - 2 * kSwitchThumbMargin;
    const qreal thumbTravel = track.width() - thumbDiameter - 2 * kSwitchThumbMargin;
    const qreal thumbX = track.left() + kSwitchThumbMargin + thumbTravel * t;
    const qreal thumbY = track.top() + (track.height() - thumbDiameter) / 2.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(isEnabled() ? palette.background : palette.textDisabled);
    painter.drawEllipse(QRectF(thumbX, thumbY, thumbDiameter, thumbDiameter));
}

// No fixed 24px header row (see DashboardCell) -- but does get an optional
// title label of its own, since the switch itself (unlike the old ON/OFF
// QPushButton text) no longer says anything about what it controls. Kept at
// its natural (fixed, sizeHint()) pill size and centered in whatever space
// the cell ends up with, same "don't stretch the control itself, center it
// instead" approach as SliderWidget below. Unlike the old checkable-
// QPushButton version, this one deliberately isn't Expanding: a slide switch
// stretched to fill the cell edge-to-edge would just be a big colored
// rectangle again, the opposite of "just the item" the redesign is for.
ToggleSwitchWidget::ToggleSwitchWidget(QWidget* parent) : DashboardWidget(parent) {
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignHCenter);
    // Hidden (not just empty text) so an unconfigured/untitled switch
    // doesn't reserve a blank strip above it -- see setConfig().
    m_titleLabel->setVisible(false);

    m_switch = new ToggleSwitch(this);

    auto* row = new QHBoxLayout();
    row->addStretch(1);
    row->addWidget(m_switch);
    row->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 0);
    layout->addWidget(m_titleLabel);
    layout->addStretch(1);
    layout->addLayout(row);
    layout->addStretch(1);

    connect(m_switch, &QAbstractButton::toggled, this, [this](bool checked) {
        emit toggled(checked);

        const QString& command = checked ? m_config.onCommand : m_config.offCommand;
        if (!command.isEmpty()) {
            emit sendRequested(command.toUtf8());
        }
    });
}

void ToggleSwitchWidget::setConfig(const QJsonObject& config) {
    m_config = parseToggleCommandConfig(config);
    m_titleLabel->setText(m_config.label);
    m_titleLabel->setVisible(!m_config.label.isEmpty());

    if (!m_configInitialized) {
        // Blocked so restoring the configured starting state (fresh insert
        // or project load) never itself fires onCommand/offCommand.
        const QSignalBlocker blocker(m_switch);
        m_switch->setChecked(m_config.defaultState);
        m_configInitialized = true;
    }
}

void ToggleSwitchWidget::setEditModeHint(bool editMode) {
    setControlPanelFill(this, editMode);
}

SliderWidget::SliderWidget(QWidget* parent) : DashboardWidget(parent) {
    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignHCenter);
    // Hidden (not just empty text) so an unconfigured/untitled slider
    // doesn't reserve a blank strip above it -- see setConfig().
    m_titleLabel->setVisible(false);

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

    // No fixed 24px header row (see DashboardCell) -- but does get an
    // optional title label of its own (see setConfig()), same reasoning as
    // ToggleSwitchWidget above: the bar itself doesn't say what it
    // controls. Below that, the control itself stays vertically centered in
    // whatever height remains -- the slider is left at its natural (thin)
    // height rather than stretched, since a QSlider stretched tall just
    // grows the dead space around its groove, it doesn't make the groove
    // itself any bigger.
    // Side margins keep the handle/label off the cell border -- unlike
    // PushButton, this control isn't meant to fill the cell edge-to-edge,
    // and DashboardWidget's opaque background fill (see dashboardwidget.h)
    // already covers the newly-exposed margin strip, so nothing leaks
    // through it.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 6, 12, 0);
    layout->addWidget(m_titleLabel);
    layout->addStretch(1);
    layout->addLayout(sliderRow);
    layout->addStretch(1);

    connect(m_slider, &QSlider::valueChanged, this, &SliderWidget::onSliderValueChanged);
    connect(m_slider, &QSlider::sliderReleased, this, &SliderWidget::onSliderReleased);
}

void SliderWidget::setConfig(const QJsonObject& config) {
    m_config = parseSliderCommandConfig(config);
    m_titleLabel->setText(m_config.label);
    m_titleLabel->setVisible(!m_config.label.isEmpty());

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

void SliderWidget::setEditModeHint(bool editMode) {
    setControlPanelFill(this, editMode);
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
