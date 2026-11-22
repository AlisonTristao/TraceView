#include "controlwidgets.h"

#include <QEasingCurve>
#include <QEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include "dashboard/widgets/controlpainting.h"
#include "preferences/appsettings.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
// The content margins inside a plate (a style with screws, Panel): wide at
// the sides, clear of the screws in the corners, and narrow at the top and
// bottom so a plate holds the same rows of controls as a plain cell.
// Without a plate there are none -- see StyledControlWidget::contentMargins().
constexpr int kPlateSideInset = 14;
constexpr int kPlateEdgeInset = 5;

// Space between a control's title, body and state text.
constexpr int kControlSpacing = 6;

// The largest a push button grows (the layout hints' maximum minus the
// grid gutter): past this a bigger cell only adds room right and below.
constexpr QSize kMaxButtonSize(232, 72);

// Matches kSelectionAnimMs (dashboardcell.cpp) / the ~150ms discrete-state
// transition convention in docs/VISUAL_IDENTITY.md's Motion section.
constexpr int kSwitchAnimMs = 150;

// The LED lamp's largest radius: past this a bigger cell only adds room
// around it.
constexpr qreal kMaxLedRadius = 28.0;
constexpr qreal kMinLedRadius = 4.0;

// Toggles the QSS-driven "dashboardControlPanel" surface fill (see
// stylesheet.cpp) the headerless controls opt into. Shown only in
// edit/Layout mode, so the cell corner reads as a panel while arranging;
// removed in Run so only the control itself sits on the canvas.
// unpolish()+polish() forces Qt to re-evaluate the QSS `[property=...]`
// selector against the changed dynamic property.
void setControlPanelFill(QWidget* widget, bool editMode) {
    widget->setProperty("dashboardControlPanel", editMode);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}
}  // namespace

// Why the controls keep DashboardWidget's opaque styled background: any
// pixel the control itself doesn't cover would otherwise leak DashboardCell's
// border and DashboardGrid's edit-mode grid lines right through. Expanding
// size policies on the parts below make sure that fill has no gaps to leak
// through either.

StyledControlWidget::StyledControlWidget(QWidget* parent) : DashboardWidget(parent) {
    connect(&AppSettings::instance(), &AppSettings::controlStyleChanged, this,
            [this] { refreshControlStyle(); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this] { refreshControlStyle(); });
    connect(&ThemeManager::instance(), &ThemeManager::appearanceChanged, this,
            [this] { refreshControlStyle(); });
}

QMargins StyledControlWidget::contentMargins() const {
    return m_style.screws
               ? QMargins(kPlateSideInset, kPlateEdgeInset, kPlateSideInset, kPlateEdgeInset)
               : QMargins();
}

void StyledControlWidget::setEditModeHint(bool editMode) {
    m_editMode = editMode;
    updatePanelFill();
}

void StyledControlWidget::updatePanelFill() {
    const bool fill = m_editMode || m_style.screws;
    // Without a plate, nothing at all behind the control in Run: a control
    // placed over a chart (an overlay, docs/LAYOUT.md) must not punch a
    // canvas-colored box into it. In edit mode the opaque fill stays, so the
    // grid lines don't show through.
    setAttribute(Qt::WA_StyledBackground, fill || m_editMode);
    if (property("dashboardControlPanel").toBool() != fill) {
        setControlPanelFill(this, fill);
    }
    update();
}

void StyledControlWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    if (testAttribute(Qt::WA_StyledBackground)) {
        QStyleOption option;
        option.initFrom(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }
    if (m_style.screws) {
        paintPanelScrews(painter, QRectF(rect()), 6.5, 3.0);
    }
}

void StyledControlWidget::setStyleChoice(const QJsonObject& config) {
    const ControlStyleChoice choice = parseControlStyleChoice(config);
    if (choice.followApp == m_choice.followApp && choice.style == m_choice.style) {
        return;
    }
    m_choice = choice;
    refreshControlStyle();
}

void StyledControlWidget::refreshControlStyle() {
    m_style =
        controlStyleFor(effectiveControlStyle(m_choice, AppSettings::instance().controlStyleId()));
    m_colors = traceview::controlColors(ThemeManager::instance().currentTheme());
    updatePanelFill();
    applyControlStyle();
    update();
}

ControlText::ControlText(Role role, QWidget* parent) : QWidget(parent), m_role(role) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // Painted parts only: the app stylesheet's QWidget background rule would
    // otherwise lay a canvas-colored strip over the control's plate.
    setStyleSheet(QStringLiteral("background: transparent;"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ControlText::setText(const QString& text) {
    if (text == m_text) {
        return;
    }
    m_text = text;
    updateGeometry();
    update();
}

void ControlText::setLook(const ControlStyle& style, const ControlColors& colors) {
    m_style = style;
    m_colors = colors;
    updateGeometry();
    update();
}

QFont ControlText::roleFont() const {
    return m_role == Role::Title ? controlTitleFont(m_style, font())
                                 : controlTextFont(m_style, font());
}

QString ControlText::shownText() const {
    return shownText(m_text);
}

QString ControlText::shownText(const QString& text) const {
    return m_role == Role::Title ? controlStyledTitle(m_style, text)
                                 : controlStyledText(m_style, text);
}

void ControlText::setAlignment(Qt::Alignment alignment) {
    m_alignment = alignment;
    update();
}

int ControlText::textWidth(const QString& text) const {
    return qCeil(QFontMetricsF(roleFont()).horizontalAdvance(shownText(text))) + 4;
}

QSize ControlText::sizeHint() const {
    const QFontMetricsF metrics(roleFont());
    return QSize(qCeil(metrics.horizontalAdvance(shownText())) + 4, qCeil(metrics.height()) + 2);
}

QSize ControlText::minimumSizeHint() const {
    return QSize(0, sizeHint().height());
}

void ControlText::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QFont textFont = roleFont();
    painter.setFont(textFont);
    painter.setPen(m_role == Role::Title ? controlTitleColor(m_style, m_colors)
                                         : controlStateTextColor(m_style, m_colors));
    const QFontMetricsF metrics(textFont);
    painter.drawText(rect(), m_alignment | Qt::AlignVCenter,
                     metrics.elidedText(shownText(), Qt::ElideRight, width()));
}

ControlButton::ControlButton(QWidget* parent) : QAbstractButton(parent) {
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void ControlButton::setLook(const ControlStyle& style, const ControlColors& colors,
                            ControlTone tone) {
    m_style = style;
    m_colors = colors;
    m_tone = tone;
    update();
}

QSize ControlButton::sizeHint() const {
    const QFontMetricsF metrics(controlTextFont(m_style, font()));
    return QSize(qCeil(metrics.horizontalAdvance(text())) + 32, qCeil(metrics.height()) + 16);
}

bool ControlButton::event(QEvent* event) {
    if (event->type() == QEvent::HoverEnter || event->type() == QEvent::HoverLeave) {
        update();
    }
    return QAbstractButton::event(event);
}

void ControlButton::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    paintControlButton(painter, QRectF(rect()), m_style, m_colors, controlTextFont(m_style, font()),
                       text(), m_tone, isDown(), underMouse(), isEnabled());
}

PushButtonWidget::PushButtonWidget(QWidget* parent) : StyledControlWidget(parent) {
    m_button = new ControlButton(this);
    m_button->setText(tr("Push Button"));
    m_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Placed by resizeEvent(): the whole cell minus the inset, capped and
    // centered -- a key stretched over a big cell reads as a colored slab,
    // not a button (see docs/LAYOUT.md for the sizes).

    m_repeatTimer = new QTimer(this);
    connect(m_repeatTimer, &QTimer::timeout, this, [this]() { sendCommand(m_config.onPress); });

    m_longPressTimer = new QTimer(this);
    m_longPressTimer->setSingleShot(true);
    connect(m_longPressTimer, &QTimer::timeout, this,
            [this]() { sendCommand(m_config.longPressCommand); });

    connect(m_button, &QAbstractButton::clicked, this, &PushButtonWidget::pressedRequested);
    connect(m_button, &QAbstractButton::pressed, this, &PushButtonWidget::onButtonPressed);
    connect(m_button, &QAbstractButton::released, this, &PushButtonWidget::onButtonReleased);

    refreshControlStyle();
}

void PushButtonWidget::resizeEvent(QResizeEvent* event) {
    StyledControlWidget::resizeEvent(event);
    const QRect area = rect().marginsRemoved(contentMargins());
    // The button is the whole control: it fills the cell up to its
    // maximum and stays centered in any room past that.
    QRect button(QPoint(0, 0), area.size().boundedTo(kMaxButtonSize));
    button.moveCenter(area.center());
    m_button->setGeometry(button);
}

void PushButtonWidget::setConfig(const QJsonObject& config) {
    m_config = parsePushButtonCommandConfig(config);
    m_button->setText(m_config.label.isEmpty() ? tr("Push Button") : m_config.label);
    setStyleChoice(config);
    applyControlStyle();
}

void PushButtonWidget::applyControlStyle() {
    m_button->setLook(controlStyle(), controlColors(), controlToneFromId(m_config.variant));
}

void PushButtonWidget::onButtonPressed() {
    m_pressSuppressed = m_config.debounceMs > 0 && m_debounceValid &&
                        m_debounceElapsed.elapsed() < m_config.debounceMs;
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
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_slideAnim.setDuration(kSwitchAnimMs);
    m_slideAnim.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&m_slideAnim, &QVariantAnimation::valueChanged, this,
            QOverload<>::of(&QWidget::update));
    connect(this, &QAbstractButton::toggled, this, &ToggleSwitch::onToggled);
}

void ToggleSwitch::setLook(const ControlStyle& style, const ControlColors& colors,
                           const QString& offText, const QString& onText) {
    const QSizeF before = controlSwitchSize(m_style, font(), m_offText, m_onText);
    m_style = style;
    m_colors = colors;
    m_offText = offText;
    m_onText = onText;
    if (controlSwitchSize(m_style, font(), m_offText, m_onText) != before) {
        updateGeometry();
    }
    update();
}

QSize ToggleSwitch::sizeHint() const {
    return controlSwitchSize(m_style, font(), m_offText, m_onText).toSize();
}

void ToggleSwitch::onToggled(bool checked) {
    // `checked` is the state we're animating TO -- while idle, the switch
    // was showing the opposite value right up until this toggled(), so that
    // (not currentValue(), which is stale once idle) is the slide start.
    const qreal from = m_slideAnim.state() == QAbstractAnimation::Running
                           ? m_slideAnim.currentValue().toReal()
                           : (checked ? 0.0 : 1.0);
    m_slideAnim.stop();
    if (ThemeManager::instance().reduceMotion()) {
        update();
        return;
    }
    m_slideAnim.setStartValue(from);
    m_slideAnim.setEndValue(checked ? 1.0 : 0.0);
    m_slideAnim.start();
}

void ToggleSwitch::paintEvent(QPaintEvent*) {
    const qreal t = m_slideAnim.state() == QAbstractAnimation::Running
                        ? m_slideAnim.currentValue().toReal()
                        : (isChecked() ? 1.0 : 0.0);
    QPainter painter(this);
    const QSizeF natural = controlSwitchSize(m_style, font(), m_offText, m_onText);
    const QRectF body(
        QPointF((width() - natural.width()) / 2.0, (height() - natural.height()) / 2.0), natural);
    paintControlSwitch(painter, body, m_style, m_colors, font(), t, isEnabled(), m_offText,
                       m_onText);
}

// The dotted leader between a checkbox's name and its state text, as on a
// printed form (the Scientific checkbox, see ToggleSwitchWidget).
class ControlLeader final : public QWidget {
public:
    explicit ControlLeader(QWidget* parent) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setMaximumWidth(120);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    void setColor(const QColor& color) {
        m_color = color;
        update();
    }
    QSize sizeHint() const override {
        return {28, 10};
    }
    QSize minimumSizeHint() const override {
        return {16, 4};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(m_color, 1.0);
        pen.setDashPattern({1.0, 3.0});
        painter.setPen(pen);
        // Near the text baseline, not the middle of the row.
        const qreal y = qRound(height() * 0.62) + 0.5;
        painter.drawLine(QPointF(3.0, y), QPointF(width() - 3.0, y));
    }

private:
    QColor m_color;
};

// No cell header -- but an optional title of its own, since the switch
// doesn't say what it controls. Title over the switch at its natural size
// and its state text, as one block: left-aligned, so a wider cell never
// moves the switch sideways, and centered vertically (docs/LAYOUT.md
// "Spacing"). The state text has the width of the longer of its two texts,
// so flipping the switch doesn't shift anything either.
ToggleSwitchWidget::ToggleSwitchWidget(QWidget* parent) : StyledControlWidget(parent) {
    m_title = new ControlText(ControlText::Role::Title, this);
    // Hidden (not just empty) so an untitled switch doesn't reserve a blank
    // strip above it -- see setConfig().
    m_title->setVisible(false);

    m_switch = new ToggleSwitch(this);
    m_state = new ControlText(ControlText::Role::State, this);
    m_state->setVisible(false);
    m_state->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_leader = new ControlLeader(this);
    m_leader->setVisible(false);

    m_row = new QHBoxLayout();
    m_row->setSpacing(10);
    m_row->addWidget(m_switch);
    m_row->addWidget(m_leader, 1);
    m_row->addWidget(m_state);
    m_row->addStretch(1);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(kControlSpacing);
    m_layout->addStretch(1);
    m_layout->addWidget(m_title);
    m_layout->addLayout(m_row);
    m_layout->addStretch(1);

    connect(m_switch, &QAbstractButton::toggled, this, [this](bool checked) {
        updateStateText();
        emit toggled(checked);

        const QString& command = checked ? m_config.onCommand : m_config.offCommand;
        if (!command.isEmpty()) {
            emit sendRequested(command.toUtf8());
        }
    });

    refreshControlStyle();
}

void ToggleSwitchWidget::setConfig(const QJsonObject& config) {
    m_config = parseToggleCommandConfig(config);
    m_title->setText(m_config.label);
    m_title->setVisible(!m_config.label.isEmpty());

    if (!m_configInitialized) {
        // Blocked so restoring the configured starting state (fresh insert
        // or project load) never itself fires onCommand/offCommand.
        const QSignalBlocker blocker(m_switch);
        m_switch->setChecked(m_config.defaultState);
        m_configInitialized = true;
    }
    setStyleChoice(config);
    applyControlStyle();
}

void ToggleSwitchWidget::applyControlStyle() {
    const ControlStyle& style = controlStyle();
    m_layout->setContentsMargins(contentMargins());
    m_title->setLook(style, controlColors());
    m_state->setLook(style, controlColors());
    m_leader->setColor(controlColors().textSecondary);
    // The checkbox reads as a printed form row: box, name, leader, state.
    const bool form = style.switchLook == ControlSwitchLook::Checkbox;
    if (form != m_formRow) {
        m_formRow = form;
        m_layout->removeWidget(m_title);
        m_row->removeWidget(m_title);
        if (form) {
            m_row->insertWidget(1, m_title);
        } else {
            m_layout->insertWidget(1, m_title);
        }
    }
    const QString offText = m_config.offLabel.isEmpty() ? tr("off") : m_config.offLabel;
    const QString onText = m_config.onLabel.isEmpty() ? tr("on") : m_config.onLabel;
    m_switch->setLook(style, controlColors(), offText, onText);
    updateStateText();
}

void ToggleSwitchWidget::updateStateText() {
    // A look with text cells already prints both states inside the switch.
    const bool cells = controlStyle().switchLook == ControlSwitchLook::Cells;
    const bool hasText = !m_config.onLabel.isEmpty() || !m_config.offLabel.isEmpty();
    m_state->setText(m_switch->isChecked() ? m_config.onLabel : m_config.offLabel);
    m_state->setFixedWidth(
        qMax(m_state->textWidth(m_config.onLabel), m_state->textWidth(m_config.offLabel)));
    m_state->setVisible(hasText && !cells);
    m_leader->setVisible(m_formRow && hasText && !m_config.label.isEmpty());
}

ControlSlider::ControlSlider(QWidget* parent) : QAbstractSlider(parent) {
    setOrientation(Qt::Horizontal);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ControlSlider::setLook(const ControlStyle& style, const ControlColors& colors,
                            const QStringList& scaleLabels) {
    const bool resized = controlSliderHeight(style) != controlSliderHeight(m_style);
    m_style = style;
    m_colors = colors;
    m_scaleLabels = scaleLabels;
    if (resized) {
        updateGeometry();
    }
    update();
}

QSize ControlSlider::sizeHint() const {
    return QSize(160, qCeil(controlSliderHeight(m_style)));
}

QSize ControlSlider::minimumSizeHint() const {
    return QSize(60, qCeil(controlSliderHeight(m_style)));
}

int ControlSlider::valueAt(qreal x) const {
    const ControlSliderTrack track = controlSliderTrack(QRectF(rect()), m_style);
    const qreal fraction = controlSliderFractionAt(track, x);
    return minimum() + qRound(fraction * (maximum() - minimum()));
}

void ControlSlider::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QAbstractSlider::mousePressEvent(event);
        return;
    }
    event->accept();
    setSliderDown(true);
    setSliderPosition(valueAt(event->position().x()));
    update();
}

void ControlSlider::mouseMoveEvent(QMouseEvent* event) {
    if (!isSliderDown()) {
        QAbstractSlider::mouseMoveEvent(event);
        return;
    }
    event->accept();
    setSliderPosition(valueAt(event->position().x()));
}

void ControlSlider::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !isSliderDown()) {
        QAbstractSlider::mouseReleaseEvent(event);
        return;
    }
    event->accept();
    setSliderDown(false);
    update();
}

void ControlSlider::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const int span = maximum() - minimum();
    const qreal fraction = span > 0 ? qreal(sliderPosition() - minimum()) / span : 0.0;
    paintControlSlider(painter, QRectF(rect()), m_style, m_colors, font(), fraction, isSliderDown(),
                       isEnabled(), m_scaleLabels);
}

ControlReadout::ControlReadout(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // Painted parts only: the app stylesheet's QWidget background rule would
    // otherwise lay a canvas-colored strip over the control's plate.
    setStyleSheet(QStringLiteral("background: transparent;"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void ControlReadout::setText(const QString& text) {
    if (text == m_text) {
        return;
    }
    m_text = text;
    update();
}

void ControlReadout::setWidestText(const QString& text) {
    if (text == m_widest) {
        return;
    }
    m_widest = text;
    updateGeometry();
}

void ControlReadout::setLook(const ControlStyle& style, const ControlColors& colors) {
    m_style = style;
    m_colors = colors;
    updateGeometry();
    update();
}

QSize ControlReadout::sizeHint() const {
    const QString widest = m_widest.size() > m_text.size() ? m_widest : m_text;
    return controlReadoutSize(m_style, font(), widest).toSize() + QSize(1, 1);
}

void ControlReadout::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QSizeF box =
        controlReadoutSize(m_style, font(), m_widest.size() > m_text.size() ? m_widest : m_text);
    const QRectF r(QPointF(width() - box.width(), (height() - box.height()) / 2.0), box);
    paintControlReadout(painter, r, m_style, m_colors, font(), m_text, isEnabled());
}

// Same layout idea as the switch: an optional title, then the slider and
// its readout centered vertically in whatever height is left -- a slider
// stretched tall only grows the dead space around its track. Side margins
// keep the thumb off the cell border.
SliderWidget::SliderWidget(QWidget* parent) : StyledControlWidget(parent) {
    m_title = new ControlText(ControlText::Role::Title, this);
    m_title->setVisible(false);

    m_slider = new ControlSlider(this);
    m_slider->setRange(0, 100);
    m_slider->setValue(50);

    m_readout = new ControlReadout(this);
    m_readout->setText(QStringLiteral("50"));

    auto* sliderRow = new QHBoxLayout();
    sliderRow->setSpacing(10);
    sliderRow->addWidget(m_slider, 1);
    sliderRow->addWidget(m_readout);

    // Title over the slider and its readout, as one block centered
    // vertically like the other controls (docs/LAYOUT.md "Spacing").
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(kControlSpacing);
    m_layout->addStretch(1);
    m_layout->addWidget(m_title);
    m_layout->addLayout(sliderRow);
    m_layout->addStretch(1);

    connect(m_slider, &QAbstractSlider::valueChanged, this, &SliderWidget::onSliderValueChanged);
    connect(m_slider, &QAbstractSlider::sliderReleased, this, &SliderWidget::onSliderReleased);

    refreshControlStyle();
}

void SliderWidget::setConfig(const QJsonObject& config) {
    m_config = parseSliderCommandConfig(config);
    m_title->setText(m_config.label);
    m_title->setVisible(!m_config.label.isEmpty());

    // Blocked so applying the range/starting value never itself fires a
    // send -- only real user interaction (below) does.
    {
        const QSignalBlocker blocker(m_slider);
        m_slider->setRange(0, sliderTickCount(m_config));
        if (!m_configInitialized) {
            m_slider->setValue(sliderValueToIndex(m_config, m_config.defaultValue));
            m_configInitialized = true;
        }
    }

    m_readout->setVisible(m_config.showValue);
    QString widest = formatValue(m_config.min);
    for (double value :
         {m_config.max, m_config.min + m_config.step, m_config.max - m_config.step}) {
        const QString text = formatValue(value);
        if (text.size() > widest.size()) {
            widest = text;
        }
    }
    m_readout->setWidestText(widest);
    updateValueLabel(sliderIndexToValue(m_config, m_slider->value()));
    setStyleChoice(config);
    applyControlStyle();
}

void SliderWidget::applyControlStyle() {
    m_layout->setContentsMargins(contentMargins());
    m_title->setLook(controlStyle(), controlColors());
    m_readout->setLook(controlStyle(), controlColors());
    m_slider->setLook(
        controlStyle(), controlColors(),
        {QString::number(m_config.min), QString::number((m_config.min + m_config.max) / 2.0),
         QString::number(m_config.max)});
}

QString SliderWidget::formatValue(double value) const {
    return QString::number(value) + m_config.unit;
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
    m_readout->setText(formatValue(value));
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

LedLamp::LedLamp(QWidget* parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Painted parts only: the app stylesheet's QWidget background rule would
    // otherwise lay a canvas-colored strip over the control's plate.
    setStyleSheet(QStringLiteral("background: transparent;"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void LedLamp::setLook(const ControlStyle& style, const ControlColors& colors) {
    m_style = style;
    m_colors = colors;
    update();
}

void LedLamp::setLit(bool lit, const QColor& color) {
    if (lit == m_lit && color == m_color) {
        return;
    }
    m_lit = lit;
    m_color = color;
    update();
}

QSize LedLamp::sizeHint() const {
    const int side = qCeil(2.0 * 12.0 * kControlLedReach);
    return QSize(side, side);
}

QSize LedLamp::minimumSizeHint() const {
    const int side = qCeil(2.0 * kMinLedRadius * kControlLedReach);
    return QSize(side, side);
}

void LedLamp::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const qreal radius =
        qBound(kMinLedRadius, qMin(width(), height()) / (2.0 * kControlLedReach), kMaxLedRadius);
    paintControlLed(painter, QRectF(rect()).center(), radius, m_style, m_colors, m_color, m_lit,
                    isEnabled());
}

LedWidget::LedWidget(QWidget* parent) : StyledControlWidget(parent) {
    m_title = new ControlText(ControlText::Role::Title, this);
    m_title->setVisible(false);
    m_lamp = new LedLamp(this);
    m_state = new ControlText(ControlText::Role::State, this);
    m_state->setVisible(false);

    // A column: title, lamp, state text, centered in the cell; the lamp is
    // sized from the room left (fitLamp()).
    m_title->setAlignment(Qt::AlignHCenter);
    m_state->setAlignment(Qt::AlignHCenter);
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(kControlSpacing);
    m_layout->addStretch(1);
    m_layout->addWidget(m_title);
    m_layout->addWidget(m_lamp);
    m_layout->addWidget(m_state);
    m_layout->addStretch(1);

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, [this] {
        m_blinkPhase = !m_blinkPhase;
        updateLamp();
    });

    refreshControlStyle();
}

void LedWidget::setConfig(const QJsonObject& config) {
    const LedConfig previous = m_config;
    m_config = parseLedConfig(config);
    m_deviceId = config.value("deviceId").toString();
    if (previous.sourceId != m_config.sourceId || previous.topicId != m_config.topicId ||
        previous.fieldId != m_config.fieldId) {
        // A value read from another field says nothing about this one.
        m_hasValue = false;
    }
    m_title->setText(m_config.label);
    m_title->setVisible(!m_config.label.isEmpty());
    setStyleChoice(config);
    applyControlStyle();
}

void LedWidget::applyControlStyle() {
    m_layout->setContentsMargins(contentMargins());
    m_title->setLook(controlStyle(), controlColors());
    m_state->setLook(controlStyle(), controlColors());
    m_lamp->setLook(controlStyle(), controlColors());
    updateLamp();
    fitLamp();
}

void LedWidget::resizeEvent(QResizeEvent* event) {
    StyledControlWidget::resizeEvent(event);
    fitLamp();
}

void LedWidget::fitLamp() {
    // The lamp takes the height left under the title and over the state
    // text, as a square, up to its largest size.
    const QMargins margins = contentMargins();
    int room = height() - margins.top() - margins.bottom();
    for (QWidget* text : {static_cast<QWidget*>(m_title), static_cast<QWidget*>(m_state)}) {
        if (!text->isHidden()) {
            room -= text->sizeHint().height() + kControlSpacing;
        }
    }
    const int maxSide = qCeil(2.0 * kMaxLedRadius * kControlLedReach);
    const int minSide = qCeil(2.0 * kMinLedRadius * kControlLedReach);
    const int side =
        qBound(minSide, qMin(room, width() - margins.left() - margins.right()), maxSide);
    if (m_lamp->height() != side || m_lamp->minimumHeight() != side) {
        m_lamp->setFixedHeight(side);
    }
}

QColor LedWidget::currentColor() const {
    if (m_config.colorMode == LedColorMode::Rgb) {
        return m_hasValue && m_on ? ledRgbColor(m_value) : controlColors().textSecondary;
    }
    return m_config.color.isValid() ? m_config.color : controlColors().success;
}

QVector<TelemetryTopicRequest> LedWidget::topicRequests() const {
    if (m_deviceId.isEmpty() || m_config.sourceId == 0 || m_config.topicId == 0) {
        return {};
    }
    const quint32 rateMillihz =
        quint32(qBound(1.0, 1'000'000.0 / m_config.sampleTimeMs, 1'000'000.0));
    return {{m_deviceId, m_config.sourceId, m_config.topicId, rateMillihz}};
}

void LedWidget::onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                              double value) {
    Q_UNUSED(timestampUs);
    if (binding.sourceId != m_config.sourceId || binding.topicId != m_config.topicId ||
        binding.fieldId != m_config.fieldId || binding.elementIndex != 0) {
        return;
    }
    setValue(value);
}

void LedWidget::onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                              const QVector<float>& values) {
    Q_UNUSED(timestampUs);
    if (binding.sourceId != m_config.sourceId || binding.topicId != m_config.topicId ||
        binding.fieldId != m_config.fieldId || values.isEmpty()) {
        return;
    }
    setValue(values.first());
}

void LedWidget::onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                             const QString& text) {
    Q_UNUSED(sourceId);
    Q_UNUSED(topicId);
    Q_UNUSED(timestampUs);
    Q_UNUSED(text);
}

void LedWidget::setValue(double value) {
    m_value = value;
    m_hasValue = true;
    updateLamp();
}

void LedWidget::updateLamp() {
    const bool wasOn = m_on;
    m_on = m_hasValue && ledIsOn(m_config, m_value);

    const bool blinking = m_on && m_config.blink;
    if (blinking && (!m_blinkTimer->isActive() || m_blinkTimer->interval() != m_config.blinkMs)) {
        m_blinkTimer->start(m_config.blinkMs);
    } else if (!blinking) {
        m_blinkTimer->stop();
        m_blinkPhase = true;
    }
    if (m_on && !wasOn) {
        m_blinkPhase = true;
    }

    m_lamp->setLit(m_on && m_blinkPhase, currentColor());
    const bool hasText = !m_config.onLabel.isEmpty() || !m_config.offLabel.isEmpty();
    m_state->setText(m_on ? m_config.onLabel : m_config.offLabel);
    m_state->setVisible(hasText);
}

}  // namespace traceview
