#pragma once

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QElapsedTimer>
#include <QVariantAnimation>

#include "dashboard/dashboardwidget.h"
#include "dashboard/telemetryconsumer.h"
#include "dashboard/widgets/controldata.h"
#include "dashboard/widgets/controlstyle.h"

class QTimer;

namespace traceview {

// The control widgets: push button, toggle switch, slider and LED. Each one
// paints itself through the control paint building blocks
// (controlpainting.h) in the look its ControlStyle (controlstyle.h) picks --
// the app-wide one from View > Control Style, or the control's own "style"
// config. docs/CONTROL_STYLE.md is the manual.

// What every control kind shares: no cell header (the header would eat a
// disproportionate share of an already-small cell), the edit-mode panel
// fill, and the style -- resolved from the control's own choice and the
// app-wide setting, and re-resolved whenever either the setting or the
// palette changes. A subclass pushes the resolved style to its parts in
// applyControlStyle(), and calls refreshControlStyle() once at the end of
// its constructor.
class StyledControlWidget : public DashboardWidget {
public:
    explicit StyledControlWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override {
        return false;
    }
    void setEditModeHint(bool editMode) override;

    const ControlStyle& controlStyle() const {
        return m_style;
    }
    const ControlColors& controlColors() const {
        return m_colors;
    }

protected:
    // Reads the "style" key of a control config; refreshes when it changed.
    void setStyleChoice(const QJsonObject& config);
    void refreshControlStyle();
    virtual void applyControlStyle() {}

private:
    ControlStyleChoice m_choice;
    ControlStyle m_style;
    ControlColors m_colors;
};

// A line of text in a control's look: a title above the control (Title) or
// the state text beside it (State). Painted rather than a QLabel so the
// app stylesheet's label rules can't override the style's font and color.
class ControlText : public QWidget {
public:
    enum class Role { Title, State };

    explicit ControlText(Role role, QWidget* parent = nullptr);

    void setText(const QString& text);
    QString text() const {
        return m_text;
    }
    void setLook(const ControlStyle& style, const ControlColors& colors);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QFont roleFont() const;
    QString shownText() const;

    Role m_role;
    QString m_text;
    ControlStyle m_style;
    ControlColors m_colors;
};

// The push button itself, painted by paintControlButton(). A
// QAbstractButton, so pressed()/released()/clicked() and keyboard activation
// work as with QPushButton.
class ControlButton : public QAbstractButton {
public:
    explicit ControlButton(QWidget* parent = nullptr);

    void setLook(const ControlStyle& style, const ControlColors& colors, ControlTone tone);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    ControlStyle m_style;
    ControlColors m_colors;
    ControlTone m_tone = ControlTone::Accent;
};

// A momentary action button -- clicking it fires once and doesn't hold
// state, unlike ToggleSwitchWidget below. setConfig() captures the
// press/release commands, mode, repeat-while-held, long-press and debounce
// settings from PushButtonConfigEditor (controlconfigeditor.h);
// onButtonPressed()/onButtonReleased() (wired to QAbstractButton's
// pressed()/released(), not clicked(), since Momentary mode and long-press
// both need press/release as distinct events) build and emit the
// configured command on sendRequested() -- SerialWidgetBridge
// (lib/core/serialwidgetbridge.h) forwards that to the device.
class PushButtonWidget : public StyledControlWidget {
    Q_OBJECT

public:
    explicit PushButtonWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;

signals:
    void pressedRequested();
    // A fully-formed outbound command, ready for SerialManager::writeCommand().
    void sendRequested(const QByteArray& command);

protected:
    void applyControlStyle() override;

private:
    void onButtonPressed();
    void onButtonReleased();
    void sendCommand(const QString& text);

    ControlButton* m_button = nullptr;
    PushButtonCommandConfig m_config;
    QTimer* m_repeatTimer = nullptr;
    QTimer* m_longPressTimer = nullptr;
    QElapsedTimer m_debounceElapsed;
    bool m_debounceValid = false;
    bool m_pressSuppressed = false;
};

// The switch itself, painted by paintControlSwitch() at the natural size
// its look asks for (controlSwitchSize()). It slides between states over
// ~150ms when the user flips it; setChecked() with signals blocked (a config
// load) lands instantly instead.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    void setLook(const ControlStyle& style, const ControlColors& colors, const QString& offText,
                 const QString& onText);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onToggled(bool checked);

    // 0 (off) .. 1 (on) slide progress, only read while running.
    QVariantAnimation m_slideAnim;
    ControlStyle m_style;
    ControlColors m_colors;
    QString m_offText;
    QString m_onText;
};

// An on/off switch that holds its state between clicks, unlike
// PushButtonWidget above. setConfig() captures the on/off commands from
// ToggleSwitchConfigEditor and applies `defaultState` once, on the first
// config (a fresh widget or a project load) -- never on a later live edit,
// so tweaking e.g. the command text in the properties panel doesn't snap a
// user-flipped switch back to its configured default. sendRequested() fires
// the matching command whenever the user (not setConfig()) changes state.
class ToggleSwitchWidget : public StyledControlWidget {
    Q_OBJECT

public:
    explicit ToggleSwitchWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;

signals:
    void toggled(bool checked);
    void sendRequested(const QByteArray& command);

protected:
    void applyControlStyle() override;

private:
    void updateStateText();

    ControlText* m_title = nullptr;
    ToggleSwitch* m_switch = nullptr;
    ControlText* m_state = nullptr;
    ToggleCommandConfig m_config;
    bool m_configInitialized = false;
};

// The slider itself, painted by paintControlSlider(). A QAbstractSlider, so
// range, value, keyboard stepping and sliderReleased() behave as QSlider's.
class ControlSlider : public QAbstractSlider {
public:
    explicit ControlSlider(QWidget* parent = nullptr);

    // `scaleLabels`: min, middle and max, printed by looks with a scale.
    void setLook(const ControlStyle& style, const ControlColors& colors,
                 const QStringList& scaleLabels);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int valueAt(qreal x) const;

    ControlStyle m_style;
    ControlColors m_colors;
    QStringList m_scaleLabels;
};

// The value box beside a slider, painted by paintControlReadout(). Sized
// for the widest text it will show so it doesn't jump while dragging.
class ControlReadout : public QWidget {
public:
    explicit ControlReadout(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setWidestText(const QString& text);
    void setLook(const ControlStyle& style, const ControlColors& colors);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    QString m_widest;
    ControlStyle m_style;
    ControlColors m_colors;
};

// A bounded value control. setConfig() maps SliderConfigEditor's double
// min/max/step onto the underlying (integer-stepped) slider's tick range
// (see controldata.h's sliderIndexToValue/sliderValueToIndex), and applies
// `defaultValue` once, on the first config, same reasoning as
// ToggleSwitchWidget above. sendRequested() fires `commandTemplate` (with
// `{value}` substituted -- docs/PROTOCOL.md) either continuously while
// dragging (throttled to `throttleMs`) or once on release, per `sendMode`.
class SliderWidget : public StyledControlWidget {
    Q_OBJECT

public:
    explicit SliderWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;

signals:
    void valueChanged(int value);
    void sendRequested(const QByteArray& command);

protected:
    void applyControlStyle() override;

private:
    void onSliderValueChanged(int index);
    void onSliderReleased();
    void updateValueLabel(double value);
    void scheduleContinuousSend(double value);
    void sendCommandFor(double value);
    QString formatValue(double value) const;

    ControlText* m_title = nullptr;
    ControlSlider* m_slider = nullptr;
    ControlReadout* m_readout = nullptr;
    SliderCommandConfig m_config;
    bool m_configInitialized = false;
    bool m_throttleActive = false;
    bool m_hasPendingSend = false;
    double m_pendingValue = 0.0;
};

// The LED lamp, painted by paintControlLed() as large as the space it gets
// allows (up to a cap, so a big cell doesn't get a dinner plate).
class LedLamp : public QWidget {
public:
    explicit LedLamp(QWidget* parent = nullptr);

    void setLook(const ControlStyle& style, const ControlColors& colors);
    void setLit(bool lit, const QColor& color);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ControlStyle m_style;
    ControlColors m_colors;
    QColor m_color;
    bool m_lit = false;
};

// An indicator lamp bound to one telemetry field (LedConfig,
// controldata.h): lit while the field's value meets the configured
// condition, or showing the field as a 0xRRGGBB color. It reads its field
// as a TelemetryConsumer (telemetryconsumer.h), so MainWindow subscribes its
// topic like any other bound widget's.
class LedWidget : public StyledControlWidget, public TelemetryConsumer {
    Q_OBJECT

public:
    explicit LedWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;

    // Whether the lamp is lit now (ignoring the blink phase) -- exposed for
    // tests.
    bool isOn() const {
        return m_on;
    }
    QColor currentColor() const;

    QVector<TelemetryTopicRequest> topicRequests() const override;
    void onFieldSample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value) override;
    void onArraySample(const TelemetryFieldBinding& binding, quint64 timestampUs,
                       const QVector<float>& values) override;
    void onTextSample(quint32 sourceId, quint16 topicId, quint64 timestampUs,
                      const QString& text) override;

protected:
    void applyControlStyle() override;

private:
    void setValue(double value);
    void updateLamp();

    LedConfig m_config;
    QString m_deviceId;
    ControlText* m_title = nullptr;
    LedLamp* m_lamp = nullptr;
    ControlText* m_state = nullptr;
    QTimer* m_blinkTimer = nullptr;
    bool m_hasValue = false;
    double m_value = 0.0;
    bool m_on = false;
    bool m_blinkPhase = true;
};

}  // namespace traceview
