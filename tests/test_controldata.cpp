#include <QtTest>

#include <QJsonObject>

#include "dashboard/widgets/controldata.h"

using traceview::buildSliderCommand;
using traceview::parsePushButtonCommandConfig;
using traceview::parseSliderCommandConfig;
using traceview::parseToggleCommandConfig;
using traceview::PushButtonCommandConfig;
using traceview::PushButtonMode;
using traceview::SliderCommandConfig;
using traceview::sliderIndexToValue;
using traceview::SliderSendMode;
using traceview::sliderTickCount;
using traceview::sliderValueToIndex;
using traceview::ToggleCommandConfig;

namespace {

class TestControlData : public QObject {
    Q_OBJECT

private slots:
    void pushButtonParsesDefaultsFromEmptyConfig();
    void pushButtonParsesExplicitConfig();

    void toggleParsesDefaultsFromEmptyConfig();
    void toggleParsesExplicitConfig();

    void sliderParsesDefaultsFromEmptyConfig();
    void sliderParsesExplicitConfig();

    void sliderTickCountMatchesRangeAndStep();
    void sliderIndexValueRoundTrip();
    void sliderValueClampsToRange();

    void buildSliderCommandSubstitutesValue();
    void buildSliderCommandEmptyTemplateYieldsEmptyBytes();
};

void TestControlData::pushButtonParsesDefaultsFromEmptyConfig() {
    const PushButtonCommandConfig config = parsePushButtonCommandConfig(QJsonObject());

    QVERIFY(config.label.isEmpty());
    QCOMPARE(config.mode, PushButtonMode::Momentary);
    QVERIFY(config.onPress.isEmpty());
    QVERIFY(config.onRelease.isEmpty());
    QVERIFY(!config.repeatWhileHeld);
    QCOMPARE(config.repeatIntervalMs, 200);
    QVERIFY(!config.longPressEnabled);
    QCOMPARE(config.longPressThresholdMs, 600);
    QVERIFY(config.longPressCommand.isEmpty());
    QCOMPARE(config.debounceMs, 150);
}

void TestControlData::pushButtonParsesExplicitConfig() {
    QJsonObject json;
    json["label"] = "Fire";
    json["mode"] = "pulse";
    json["onPress"] = "PRESS";
    json["onRelease"] = "RELEASE";
    json["repeatWhileHeld"] = true;
    json["repeatIntervalMs"] = 50;
    QJsonObject longPress;
    longPress["enabled"] = true;
    longPress["thresholdMs"] = 900;
    longPress["command"] = "HOLD";
    json["longPress"] = longPress;
    json["debounceMs"] = 20;

    const PushButtonCommandConfig config = parsePushButtonCommandConfig(json);
    QCOMPARE(config.label, QStringLiteral("Fire"));
    QCOMPARE(config.mode, PushButtonMode::Pulse);
    QCOMPARE(config.onPress, QStringLiteral("PRESS"));
    QCOMPARE(config.onRelease, QStringLiteral("RELEASE"));
    QVERIFY(config.repeatWhileHeld);
    QCOMPARE(config.repeatIntervalMs, 50);
    QVERIFY(config.longPressEnabled);
    QCOMPARE(config.longPressThresholdMs, 900);
    QCOMPARE(config.longPressCommand, QStringLiteral("HOLD"));
    QCOMPARE(config.debounceMs, 20);
}

void TestControlData::toggleParsesDefaultsFromEmptyConfig() {
    const ToggleCommandConfig config = parseToggleCommandConfig(QJsonObject());
    QVERIFY(config.label.isEmpty());
    QVERIFY(config.onCommand.isEmpty());
    QVERIFY(config.offCommand.isEmpty());
    QVERIFY(!config.defaultState);
}

void TestControlData::toggleParsesExplicitConfig() {
    QJsonObject json;
    json["label"] = "Pump";
    json["onCommand"] = "ON";
    json["offCommand"] = "OFF";
    json["defaultState"] = true;

    const ToggleCommandConfig config = parseToggleCommandConfig(json);
    QCOMPARE(config.label, QStringLiteral("Pump"));
    QCOMPARE(config.onCommand, QStringLiteral("ON"));
    QCOMPARE(config.offCommand, QStringLiteral("OFF"));
    QVERIFY(config.defaultState);
}

void TestControlData::sliderParsesDefaultsFromEmptyConfig() {
    const SliderCommandConfig config = parseSliderCommandConfig(QJsonObject());
    QVERIFY(config.label.isEmpty());
    QCOMPARE(config.min, 0.0);
    QCOMPARE(config.max, 100.0);
    QCOMPARE(config.step, 1.0);
    QCOMPARE(config.defaultValue, 50.0);
    QVERIFY(config.unit.isEmpty());
    QVERIFY(config.showValue);
    QCOMPARE(config.sendMode, SliderSendMode::Continuous);
    QCOMPARE(config.throttleMs, 100);
    QVERIFY(config.commandTemplate.isEmpty());
}

void TestControlData::sliderParsesExplicitConfig() {
    QJsonObject json;
    json["label"] = "Speed";
    json["min"] = -10.0;
    json["max"] = 10.0;
    json["step"] = 0.5;
    json["defaultValue"] = 2.5;
    json["unit"] = "V";
    json["showValue"] = false;
    json["sendMode"] = "onRelease";
    json["throttleMs"] = 250;
    json["commandTemplate"] = "SET {value}";

    const SliderCommandConfig config = parseSliderCommandConfig(json);
    QCOMPARE(config.label, QStringLiteral("Speed"));
    QCOMPARE(config.min, -10.0);
    QCOMPARE(config.max, 10.0);
    QCOMPARE(config.step, 0.5);
    QCOMPARE(config.defaultValue, 2.5);
    QCOMPARE(config.unit, QStringLiteral("V"));
    QVERIFY(!config.showValue);
    QCOMPARE(config.sendMode, SliderSendMode::OnRelease);
    QCOMPARE(config.throttleMs, 250);
    QCOMPARE(config.commandTemplate, QStringLiteral("SET {value}"));
}

void TestControlData::sliderTickCountMatchesRangeAndStep() {
    SliderCommandConfig config;
    config.min = 0.0;
    config.max = 100.0;
    config.step = 1.0;
    QCOMPARE(sliderTickCount(config), 100);

    config.min = -10.0;
    config.max = 10.0;
    config.step = 0.5;
    QCOMPARE(sliderTickCount(config), 40);
}

void TestControlData::sliderIndexValueRoundTrip() {
    SliderCommandConfig config;
    config.min = -10.0;
    config.max = 10.0;
    config.step = 0.5;

    for (int index : {0, 1, 10, 40}) {
        const double value = sliderIndexToValue(config, index);
        QCOMPARE(sliderValueToIndex(config, value), index);
    }
    QCOMPARE(sliderIndexToValue(config, 0), -10.0);
    QCOMPARE(sliderIndexToValue(config, 40), 10.0);
}

void TestControlData::sliderValueClampsToRange() {
    SliderCommandConfig config;
    config.min = 0.0;
    config.max = 10.0;
    config.step = 1.0;

    QCOMPARE(sliderValueToIndex(config, -5.0), 0);
    QCOMPARE(sliderValueToIndex(config, 500.0), sliderTickCount(config));
}

void TestControlData::buildSliderCommandSubstitutesValue() {
    SliderCommandConfig config;
    config.commandTemplate = "SET {value} now";
    QCOMPARE(buildSliderCommand(config, 42.0), QByteArrayLiteral("SET 42 now"));
}

void TestControlData::buildSliderCommandEmptyTemplateYieldsEmptyBytes() {
    SliderCommandConfig config;
    QVERIFY(config.commandTemplate.isEmpty());
    QVERIFY(buildSliderCommand(config, 1.0).isEmpty());
}

} // namespace

QTEST_MAIN(TestControlData)
#include "test_controldata.moc"
