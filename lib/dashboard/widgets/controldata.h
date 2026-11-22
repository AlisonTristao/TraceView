#pragma once

#include <QByteArray>
#include <QColor>
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
    // The button's color: "default" (accent), "success", "warning" or
    // "danger" -- see controlToneFromId() (controlstyle.h).
    QString variant = QStringLiteral("default");
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
    // Text shown beside the switch for each state ("Running"/"Stopped");
    // nothing is shown while both are empty. Looks with text cells
    // (ControlSwitchLook::Cells) print them inside the switch instead.
    QString onLabel;
    QString offLabel;
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

// An LED bound to one telemetry field (LedWidget, controlwidgets.h).
//   Fixed -- the LED lights in `color` while `condition` holds.
//   Rgb   -- the field is a packed 0xRRGGBB color the LED shows; black (0)
//            is off.
enum class LedColorMode { Fixed, Rgb };

// When a Fixed LED is lit, comparing the field's value with `threshold`
// where it applies.
enum class LedCondition { NonZero, Zero, Above, Below };

struct LedConfig {
    QString label;
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 fieldId = 0;
    LedColorMode colorMode = LedColorMode::Fixed;
    LedCondition condition = LedCondition::NonZero;
    double threshold = 0.5;
    // Invalid (the default) means the palette's success color.
    QColor color;
    // Blink while lit, toggling every `blinkMs`.
    bool blink = false;
    int blinkMs = 500;
    // State text under the LED; nothing is shown while both are empty.
    QString onLabel;
    QString offLabel;
    double sampleTimeMs = 100.0;
};

// Parses a LedConfigEditor JSON config; missing fields fall back to the
// defaults above. Ids are hex text ("0x0101") or numbers, as in every
// telemetry widget config.
LedConfig parseLedConfig(const QJsonObject& json);

// "fixed"/"rgb" and "nonzero"/"zero"/"above"/"below" -- the JSON values.
QString ledColorModeId(LedColorMode mode);
LedColorMode ledColorModeFromId(const QString& id);
QString ledConditionId(LedCondition condition);
LedCondition ledConditionFromId(const QString& id);

// Whether the LED is lit for a field value.
bool ledIsOn(const LedConfig& config, double value);
// The color an Rgb LED shows for a field value: the low 24 bits as
// 0xRRGGBB (negative and non-finite values read as black).
QColor ledRgbColor(double value);

}  // namespace traceview
