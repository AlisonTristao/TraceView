#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace traceview {

// Everything below is pure data/logic -- no QWidget -- mirroring the split
// chartdata.h uses for the chart/gauge widgets (BACKEND_TODO.txt Task 7/8).
// It mirrors the JSON shape PushButtonConfigEditor/ToggleSwitchConfigEditor/
// SliderConfigEditor already define (widgets/controlconfigeditor.cpp) and the
// outbound wire format closed in docs/PROTOCOL.md ("Outbound: control
// commands").

enum class PushButtonMode { Momentary, Pulse };

struct PushButtonCommandConfig {
    // Shown as the button's own text (there's no room for a separate title
    // on a control this small) -- PushButtonWidget falls back to a generic
    // default when this is empty, same as before this field was wired up.
    QString label;
    PushButtonMode mode = PushButtonMode::Momentary;
    QString onPress;
    QString onRelease;
    bool repeatWhileHeld = false;
    int repeatIntervalMs = 200;
    bool longPressEnabled = false;
    int longPressThresholdMs = 600;
    QString longPressCommand;
    int debounceMs = 150;
};

// Parses a PushButtonConfigEditor JSON config; missing fields fall back to
// the same defaults PushButtonConfigEditor::setConfig() uses.
PushButtonCommandConfig parsePushButtonCommandConfig(const QJsonObject& json);

struct ToggleCommandConfig {
    // Shown above the switch, centered (the switch itself has no room for
    // text) -- hidden entirely when empty rather than reserving blank
    // space. Unlike PushButtonWidget's label, there's no generic fallback
    // here -- an unlabeled switch is a normal, common state, not a
    // misconfiguration.
    QString label;
    QString onCommand;
    QString offCommand;
    bool defaultState = false;
};

// Parses a ToggleSwitchConfigEditor JSON config; missing fields fall back to
// the same defaults ToggleSwitchConfigEditor::setConfig() uses.
ToggleCommandConfig parseToggleCommandConfig(const QJsonObject& json);

enum class SliderSendMode { Continuous, OnRelease };

struct SliderCommandConfig {
    // Shown above the slider, centered -- same "hidden when empty, no
    // generic fallback" reasoning as ToggleCommandConfig::label above.
    QString label;
    double min = 0.0;
    double max = 100.0;
    double step = 1.0;
    double defaultValue = 50.0;
    QString unit;
    bool showValue = true;
    SliderSendMode sendMode = SliderSendMode::Continuous;
    int throttleMs = 100;
    QString commandTemplate;
};

// Parses a SliderConfigEditor JSON config; missing fields fall back to the
// same defaults SliderConfigEditor::setConfig() uses.
SliderCommandConfig parseSliderCommandConfig(const QJsonObject& json);

// QSlider is integer-stepped; these map its 0-based tick index to/from the
// configured double range so a fractional `step` (e.g. 0.5) still works.
// Always at least 1 tick, even for a degenerate/misconfigured range.
int sliderTickCount(const SliderCommandConfig& config);
double sliderIndexToValue(const SliderCommandConfig& config, int index);
int sliderValueToIndex(const SliderCommandConfig& config, double value);

// Substitutes the one placeholder the outbound spec defines -- `{value}` in
// `commandTemplate`, replaced with `value` (see docs/PROTOCOL.md) -- and
// encodes the result as UTF-8. Returns an empty QByteArray if
// `commandTemplate` is empty (nothing to send).
QByteArray buildSliderCommand(const SliderCommandConfig& config, double value);

} // namespace traceview
