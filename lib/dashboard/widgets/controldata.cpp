#include "controldata.h"

#include <QtMath>

namespace traceview {

PushButtonCommandConfig parsePushButtonCommandConfig(const QJsonObject& json) {
    PushButtonCommandConfig config;
    config.label = json.value("label").toString();
    config.variant = json.value("variant").toString(QStringLiteral("default"));
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
    config.label = json.value("label").toString();
    config.onLabel = json.value("onLabel").toString();
    config.offLabel = json.value("offLabel").toString();
    config.onCommand = json.value("onCommand").toString();
    config.offCommand = json.value("offCommand").toString();
    config.defaultState = json.value("defaultState").toBool(false);
    return config;
}

SliderCommandConfig parseSliderCommandConfig(const QJsonObject& json) {
    SliderCommandConfig config;
    config.label = json.value("label").toString();
    config.min = json.value("min").toDouble(0.0);
    config.max = json.value("max").toDouble(100.0);
    config.step = json.value("step").toDouble(1.0);
    config.defaultValue = json.value("defaultValue").toDouble(50.0);
    config.unit = json.value("unit").toString();
    config.showValue = json.value("showValue").toBool(true);
    config.sendMode = json.value("sendMode").toString("continuous") == "onRelease"
                          ? SliderSendMode::OnRelease
                          : SliderSendMode::Continuous;
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

namespace {

quint32 parseHexOrNumber(const QJsonValue& value, quint32 maximum) {
    qulonglong parsed = 0;
    if (value.isDouble()) {
        parsed = qulonglong(qMax(0.0, value.toDouble()));
    } else {
        parsed = value.toString().trimmed().toULongLong(nullptr, 0);
    }
    return quint32(qMin<qulonglong>(parsed, maximum));
}

}  // namespace

QString ledColorModeId(LedColorMode mode) {
    return mode == LedColorMode::Rgb ? QStringLiteral("rgb") : QStringLiteral("fixed");
}

LedColorMode ledColorModeFromId(const QString& id) {
    return id == QLatin1String("rgb") ? LedColorMode::Rgb : LedColorMode::Fixed;
}

QString ledConditionId(LedCondition condition) {
    switch (condition) {
        case LedCondition::Zero:
            return QStringLiteral("zero");
        case LedCondition::Above:
            return QStringLiteral("above");
        case LedCondition::Below:
            return QStringLiteral("below");
        case LedCondition::NonZero:
            break;
    }
    return QStringLiteral("nonzero");
}

LedCondition ledConditionFromId(const QString& id) {
    for (LedCondition condition :
         {LedCondition::NonZero, LedCondition::Zero, LedCondition::Above, LedCondition::Below}) {
        if (ledConditionId(condition) == id) {
            return condition;
        }
    }
    return LedCondition::NonZero;
}

LedConfig parseLedConfig(const QJsonObject& json) {
    LedConfig config;
    config.label = json.value("label").toString();
    config.sourceId = parseHexOrNumber(json.value("sourceId"), 0xFFFFFFFFu);
    config.topicId = quint16(parseHexOrNumber(json.value("topicId"), 0xFFFFu));
    config.fieldId = quint16(parseHexOrNumber(json.value("fieldId"), 0xFFFFu));
    config.colorMode = ledColorModeFromId(json.value("colorMode").toString());
    config.condition = ledConditionFromId(json.value("condition").toString());
    config.threshold = json.value("threshold").toDouble(0.5);
    // QColor(QString), not QColor::fromString(): the AppImage builds
    // against Qt 6.2, which doesn't have it.
    const QColor color(json.value("color").toString());
    if (color.isValid()) {
        config.color = color;
    }
    config.blink = json.value("blink").toBool(false);
    config.blinkMs = qBound(50, json.value("blinkMs").toInt(500), 10'000);
    config.onLabel = json.value("onLabel").toString();
    config.offLabel = json.value("offLabel").toString();
    config.sampleTimeMs = qMax(1.0, json.value("sampleTimeMs").toDouble(100.0));
    return config;
}

bool ledIsOn(const LedConfig& config, double value) {
    if (config.colorMode == LedColorMode::Rgb) {
        return ledRgbColor(value) != QColor(Qt::black);
    }
    switch (config.condition) {
        case LedCondition::Zero:
            return value == 0.0;
        case LedCondition::Above:
            return value > config.threshold;
        case LedCondition::Below:
            return value < config.threshold;
        case LedCondition::NonZero:
            break;
    }
    return value != 0.0 && !qIsNaN(value);
}

QColor ledRgbColor(double value) {
    if (!qIsFinite(value) || value <= 0.0) {
        return QColor(Qt::black);
    }
    const quint32 rgb = quint32(qulonglong(value) & 0xFFFFFFu);
    return QColor::fromRgb(QRgb(0xFF000000u | rgb));
}

}  // namespace traceview
