#pragma once

#include <QAbstractButton>
#include <QElapsedTimer>
#include <QVariantAnimation>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgets/controldata.h"

class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace traceview {

// A momentary action button — clicking it fires once and doesn't hold
// state, unlike ToggleSwitchWidget below. setConfig() captures the
// press/release commands, mode, repeat-while-held, long-press and debounce
// settings from PushButtonConfigEditor (controlconfigeditor.h);
// onButtonPressed()/onButtonReleased() (wired to QAbstractButton's
// pressed()/released(), not clicked(), since Momentary mode and long-press
// both need press/release as distinct events) build and emit the
// configured command on sendRequested() -- SerialWidgetBridge
// (lib/core/serialwidgetbridge.h) forwards that to
// SerialManager::writeCommand() (BACKEND_TODO.txt Task 9).
class PushButtonWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit PushButtonWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override {
        return false;
    }
    void setConfig(const QJsonObject& config) override;
    void setEditModeHint(bool editMode) override;

signals:
    void pressedRequested();
    // A fully-formed outbound command, ready for SerialManager::writeCommand().
    void sendRequested(const QByteArray& command);

private:
    void onButtonPressed();
    void onButtonReleased();
    void sendCommand(const QString& text);

    QPushButton* m_button = nullptr;
    PushButtonCommandConfig m_config;
    QTimer* m_repeatTimer = nullptr;
    QTimer* m_longPressTimer = nullptr;
    QElapsedTimer m_debounceElapsed;
    bool m_debounceValid = false;
    bool m_pressSuppressed = false;
};

// A small, fixed-size iOS/Android-style slide switch: a pill-shaped track
// with a circular thumb that slides to the accent-filled side when checked
// and back when unchecked. QAbstractButton, not QPushButton -- this paints
// itself entirely in paintEvent() rather than through QSS (no stylesheet.cpp
// rule targets it), the same "draw it, don't fake it" approach used for the
// hand-drawn glyphs in stylesheet.cpp/ribbonicons.cpp/dashboardcell.cpp.
// ToggleSwitchWidget below is the only place this is used.
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT

public:
    explicit ToggleSwitch(QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void onToggled(bool checked);

    // 0 (off/left) .. 1 (on/right) slide progress. Only driven while
    // m_slideAnim is actually running (real user clicks -- see onToggled());
    // paintEvent() falls back to isChecked()'s 0/1 value the rest of the
    // time, so setChecked() during setConfig()'s initial, signal-blocked
    // load (see ToggleSwitchWidget::setConfig()) lands the thumb instantly
    // in the right place with no animation.
    QVariantAnimation m_slideAnim;
};

// An on/off switch that holds its state between clicks, unlike
// PushButtonWidget above. setConfig() captures the on/off commands from
// ToggleSwitchConfigEditor and applies `defaultState` once, on the first
// config (a fresh widget or a project load) -- never on a later live edit,
// so tweaking e.g. the command text in the properties panel doesn't snap a
// user-flipped switch back to its configured default. sendRequested() fires
// the matching command whenever the user (not setConfig()) changes state.
class ToggleSwitchWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit ToggleSwitchWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override {
        return false;
    }
    void setConfig(const QJsonObject& config) override;
    void setEditModeHint(bool editMode) override;

signals:
    void toggled(bool checked);
    void sendRequested(const QByteArray& command);

private:
    QLabel* m_titleLabel = nullptr;
    ToggleSwitch* m_switch = nullptr;
    ToggleCommandConfig m_config;
    bool m_configInitialized = false;
};

// A bounded value control. setConfig() maps SliderConfigEditor's double
// min/max/step onto the underlying (integer-stepped) QSlider's tick range
// (see controldata.h's sliderIndexToValue/sliderValueToIndex), and applies
// `defaultValue` once, on the first config, same reasoning as
// ToggleSwitchWidget above. sendRequested() fires `commandTemplate` (with
// `{value}` substituted -- docs/PROTOCOL.md) either continuously while
// dragging (throttled to `throttleMs`) or once on release, per `sendMode`.
class SliderWidget : public DashboardWidget {
    Q_OBJECT

public:
    explicit SliderWidget(QWidget* parent = nullptr);

    bool wantsCellHeader() const override {
        return false;
    }
    void setConfig(const QJsonObject& config) override;
    void setEditModeHint(bool editMode) override;

signals:
    void valueChanged(int value);
    void sendRequested(const QByteArray& command);

private:
    void onSliderValueChanged(int index);
    void onSliderReleased();
    void updateValueLabel(double value);
    void scheduleContinuousSend(double value);
    void sendCommandFor(double value);

    QLabel* m_titleLabel = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_valueLabel = nullptr;
    SliderCommandConfig m_config;
    bool m_configInitialized = false;
    bool m_throttleActive = false;
    bool m_hasPendingSend = false;
    double m_pendingValue = 0.0;
};

}  // namespace traceview
