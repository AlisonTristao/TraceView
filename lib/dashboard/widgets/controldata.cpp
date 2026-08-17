#include "controldata.h"

#include <QtMath>

namespace traceview {

PushButtonCommandConfig parsePushButtonCommandConfig(const QJsonObject& json) {
    PushButtonCommandConfig config;
    config.mode = json.value("mode").toString("momentary") == "pulse" ? PushButtonMode::Pulse
                                                                        : PushButtonMode::Momentary;
    config.onPress = json.value("onPress").toString();
    config.onRelease = json.value("onRelease").toString();
    config.repeatWhileHeld = json.value("repeatWhileHeld").toBool(false);
    config.repeatIntervalMs = json.value("repeatIntervalMs").toInt(200);

    const QJsonObject longPress = json.value("longPress").toObject();
    config.longPressEnabled = longPress.value("enabled").toBool(false);
    config.longPressThresholdMs = longPress.value("thresholdMs").toInt(600);
    config.longPressCommand = longPress.value("command").toString();

    config.debounceMs = json.value("debounceMs").toInt(150);
    return config;
}

ToggleCommandConfig parseToggleCommandConfig(const QJsonObject& json) {
    ToggleCommandConfig config;
    config.onCommand = json.value("onCommand").toString();
    config.offCommand = json.value("offCommand").toString();
    config.defaultState = json.value("defaultState").toBool(false);
    return config;
}

SliderCommandConfig parseSliderCommandConfig(const QJsonObject& json) {
    SliderCommandConfig config;
    config.min = json.value("min").toDouble(0.0);
    config.max = json.value("max").toDouble(100.0);
    config.step = json.value("step").toDouble(1.0);
    config.defaultValue = json.value("defaultValue").toDouble(50.0);
    config.unit = json.value("unit").toString();
    config.showValue = json.value("showValue").toBool(true);
    config.sendMode =
        json.value("sendMode").toString("continuous") == "onRelease" ? SliderSendMode::OnRelease : SliderSendMode::Continuous;
    config.throttleMs = json.value("throttleMs").toInt(100);
    config.commandTemplate = json.value("commandTemplate").toString();
    return config;
}

int sliderTickCount(const SliderCommandConfig& config) {
    const double step = qMax(0.0001, config.step);
    const double range = qMax(0.0, config.max - config.min);
    return qMax(1, qRound(range / step));
}

double sliderIndexToValue(const SliderCommandConfig& config, int index) {
    const double step = qMax(0.0001, config.step);
    return qBound(config.min, config.min + index * step, qMax(config.min, config.max));
}

int sliderValueToIndex(const SliderCommandConfig& config, double value) {
    const double step = qMax(0.0001, config.step);
    return qBound(0, qRound((value - config.min) / step), sliderTickCount(config));
}

QByteArray buildSliderCommand(const SliderCommandConfig& config, double value) {
    if (config.commandTemplate.isEmpty()) {
        return QByteArray();
    }
    QString command = config.commandTemplate;
    command.replace("{value}", QString::number(value));
    return command.toUtf8();
}

} // namespace traceview
