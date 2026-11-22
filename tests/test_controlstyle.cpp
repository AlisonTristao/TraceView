#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QtTest>

#include "dashboard/widgets/chartstyle.h"
#include "dashboard/widgets/chartwidgets.h"
#include "dashboard/widgets/controldata.h"
#include "dashboard/widgets/controlpainting.h"
#include "dashboard/widgets/controlstyle.h"
#include "dashboard/widgets/controlwidgets.h"

using namespace traceview;

// The control style sheet (widgets/controlstyle.h), the LED's config and
// logic (widgets/controldata.h), and a paint smoke test of every control in
// every style (widgets/controlwidgets.h).
class TestControlStyle : public QObject {
    Q_OBJECT

private slots:
    void idsRoundTripWithDashboardFallback();
    void everyStyleLooksDifferent();
    void styleChoiceFollowsAppUnlessPinned();
    void retroLowercasesAndPrefixesTitles();
    void sliderFractionFollowsTheDrawnTrack();
    void ledConfigParsesWithDefaults();
    void ledConditions();
    void ledRgbColor();
    void ledWidgetReadsOnlyItsField();
    void ledWidgetRequestsItsTopic();
    void everyControlPaintsInEveryStyle();
    void chartStylesMatchTheControlStyles();
    void gaugeShapesRoundTrip();
    void everyGaugeShapePaintsInEveryChartStyle();
};

void TestControlStyle::idsRoundTripWithDashboardFallback() {
    for (ControlStyleId id : allControlStyles()) {
        QCOMPARE(controlStyleFromId(controlStyleIdString(id)), id);
        QVERIFY(!controlStyleDisplayName(id).isEmpty());
    }
    QCOMPARE(controlStyleFromId(QString()), ControlStyleId::Dashboard);
    QCOMPARE(controlStyleFromId("bogus"), ControlStyleId::Dashboard);
}

// A style that draws a control exactly like another one is a duplicate,
// not a style.
void TestControlStyle::everyStyleLooksDifferent() {
    QSet<int> buttons;
    QSet<int> switches;
    QSet<int> sliders;
    QSet<int> leds;
    for (ControlStyleId id : allControlStyles()) {
        const ControlStyle style = controlStyleFor(id);
        QCOMPARE(style.id, id);
        buttons.insert(int(style.button));
        switches.insert(int(style.switchLook));
        sliders.insert(int(style.slider));
        leds.insert(int(style.led));
    }
    const int count = allControlStyles().size();
    QCOMPARE(buttons.size(), count);
    QCOMPARE(switches.size(), count);
    QCOMPARE(sliders.size(), count);
    QCOMPARE(leds.size(), count);
}

void TestControlStyle::styleChoiceFollowsAppUnlessPinned() {
    const ControlStyleChoice missing = parseControlStyleChoice(QJsonObject());
    QVERIFY(missing.followApp);
    QCOMPARE(effectiveControlStyle(missing, "panel"), ControlStyleId::Panel);

    const ControlStyleChoice app = parseControlStyleChoice({{"style", "app"}});
    QVERIFY(app.followApp);
    QCOMPARE(effectiveControlStyle(app, "bogus"), ControlStyleId::Dashboard);

    const ControlStyleChoice pinned = parseControlStyleChoice({{"style", "retro"}});
    QVERIFY(!pinned.followApp);
    QCOMPARE(effectiveControlStyle(pinned, "panel"), ControlStyleId::Retro);
}

void TestControlStyle::retroLowercasesAndPrefixesTitles() {
    const ControlStyle retro = controlStyleFor(ControlStyleId::Retro);
    QCOMPARE(controlStyledTitle(retro, "Pump Speed"), QString("> pump speed"));
    QCOMPARE(controlStyledText(retro, "Start"), QString("start"));
    QCOMPARE(controlStyledTitle(retro, QString()), QString());

    const ControlStyle dashboard = controlStyleFor(ControlStyleId::Dashboard);
    QCOMPARE(controlStyledTitle(dashboard, "Pump Speed"), QString("Pump Speed"));
}

void TestControlStyle::sliderFractionFollowsTheDrawnTrack() {
    const QRectF rect(0, 0, 200, 40);
    for (ControlStyleId id : allControlStyles()) {
        const ControlStyle style = controlStyleFor(id);
        const ControlSliderTrack track = controlSliderTrack(rect, style);
        QVERIFY(track.left >= rect.left() && track.right <= rect.right());
        QVERIFY(track.right > track.left);
        QCOMPARE(controlSliderFractionAt(track, track.left), 0.0);
        QCOMPARE(controlSliderFractionAt(track, track.right), 1.0);
        QCOMPARE(controlSliderFractionAt(track, -50.0), 0.0);
        QCOMPARE(controlSliderFractionAt(track, 500.0), 1.0);
        QVERIFY(qAbs(controlSliderFractionAt(track, (track.left + track.right) / 2.0) - 0.5) <
                1e-9);
    }
}

void TestControlStyle::ledConfigParsesWithDefaults() {
    const LedConfig defaults = parseLedConfig(QJsonObject());
    QCOMPARE(defaults.colorMode, LedColorMode::Fixed);
    QCOMPARE(defaults.condition, LedCondition::NonZero);
    QVERIFY(!defaults.color.isValid());
    QVERIFY(!defaults.blink);
    QCOMPARE(defaults.sourceId, quint32(0));

    const LedConfig led = parseLedConfig({{"sourceId", "0x00000001"},
                                          {"topicId", "0x0101"},
                                          {"fieldId", 3},
                                          {"colorMode", "rgb"},
                                          {"condition", "above"},
                                          {"threshold", 2.5},
                                          {"color", "#ef4444"},
                                          {"blink", true},
                                          {"blinkMs", 10}});
    QCOMPARE(led.sourceId, quint32(1));
    QCOMPARE(led.topicId, quint16(0x0101));
    QCOMPARE(led.fieldId, quint16(3));
    QCOMPARE(led.colorMode, LedColorMode::Rgb);
    QCOMPARE(led.condition, LedCondition::Above);
    QCOMPARE(led.threshold, 2.5);
    QCOMPARE(led.color, QColor("#ef4444"));
    QVERIFY(led.blink);
    QCOMPARE(led.blinkMs, 50);  // clamped

    QVERIFY(!parseLedConfig({{"color", "not a color"}}).color.isValid());
    for (LedCondition c :
         {LedCondition::NonZero, LedCondition::Zero, LedCondition::Above, LedCondition::Below}) {
        QCOMPARE(ledConditionFromId(ledConditionId(c)), c);
    }
}

void TestControlStyle::ledConditions() {
    LedConfig led;
    QVERIFY(ledIsOn(led, 1.0));
    QVERIFY(ledIsOn(led, -0.1));
    QVERIFY(!ledIsOn(led, 0.0));
    QVERIFY(!ledIsOn(led, qQNaN()));

    led.condition = LedCondition::Zero;
    QVERIFY(ledIsOn(led, 0.0));
    QVERIFY(!ledIsOn(led, 1.0));

    led.condition = LedCondition::Above;
    led.threshold = 10.0;
    QVERIFY(ledIsOn(led, 10.5));
    QVERIFY(!ledIsOn(led, 10.0));

    led.condition = LedCondition::Below;
    QVERIFY(ledIsOn(led, 9.5));
    QVERIFY(!ledIsOn(led, 10.0));

    led.colorMode = LedColorMode::Rgb;
    QVERIFY(ledIsOn(led, double(0x00FF00)));
    QVERIFY(!ledIsOn(led, 0.0));
}

void TestControlStyle::ledRgbColor() {
    QCOMPARE(traceview::ledRgbColor(double(0xFF8000)), QColor(255, 128, 0));
    QCOMPARE(traceview::ledRgbColor(double(0x1FF8000)), QColor(255, 128, 0));  // low 24 bits
    QCOMPARE(traceview::ledRgbColor(-5.0), QColor(Qt::black));
    QCOMPARE(traceview::ledRgbColor(qInf()), QColor(Qt::black));
}

void TestControlStyle::ledWidgetReadsOnlyItsField() {
    LedWidget led;
    led.setConfig(
        {{"deviceId", "dev"}, {"sourceId", "0x00000001"}, {"topicId", "0x0101"}, {"fieldId", 2}});
    QVERIFY(!led.isOn());

    led.onFieldSample({1, 0x0101, 3, 0}, 0, 1.0);  // another field
    QVERIFY(!led.isOn());
    led.onFieldSample({2, 0x0101, 2, 0}, 0, 1.0);  // another source
    QVERIFY(!led.isOn());
    led.onFieldSample({1, 0x0101, 2, 0}, 0, 1.0);
    QVERIFY(led.isOn());
    led.onFieldSample({1, 0x0101, 2, 0}, 0, 0.0);
    QVERIFY(!led.isOn());

    led.onFieldSample({1, 0x0101, 2, 0}, 0, 1.0);
    QVERIFY(led.isOn());
    // Re-pointed at another field: the old value says nothing about it.
    led.setConfig(
        {{"deviceId", "dev"}, {"sourceId", "0x00000001"}, {"topicId", "0x0101"}, {"fieldId", 5}});
    QVERIFY(!led.isOn());

    led.setConfig({{"deviceId", "dev"},
                   {"sourceId", "0x00000001"},
                   {"topicId", "0x0101"},
                   {"fieldId", 5},
                   {"colorMode", "rgb"}});
    led.onFieldSample({1, 0x0101, 5, 0}, 0, double(0x0000FF));
    QVERIFY(led.isOn());
    QCOMPARE(led.currentColor(), QColor(0, 0, 255));
}

void TestControlStyle::ledWidgetRequestsItsTopic() {
    LedWidget led;
    QVERIFY(led.topicRequests().isEmpty());

    led.setConfig({{"deviceId", "dev"},
                   {"sourceId", "0x00000001"},
                   {"topicId", "0x0101"},
                   {"fieldId", 2},
                   {"sampleTimeMs", 500}});
    const QVector<TelemetryTopicRequest> requests = led.topicRequests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests[0].deviceId, QString("dev"));
    QCOMPARE(requests[0].sourceId, quint32(1));
    QCOMPARE(requests[0].topicId, quint16(0x0101));
    QCOMPARE(requests[0].rateMillihz, quint32(2000));  // 2 Hz

    led.setConfig({{"sourceId", "0x00000001"}, {"topicId", "0x0101"}});  // no device
    QVERIFY(led.topicRequests().isEmpty());
}

// Not a pixel comparison -- a smoke test that every look of every control
// paints at a few sizes (tiny ones included) without crashing or asserting.
void TestControlStyle::everyControlPaintsInEveryStyle() {
    const QList<QSize> sizes = {QSize(220, 90), QSize(40, 20), QSize(400, 160)};
    for (ControlStyleId id : allControlStyles()) {
        const QString styleId = controlStyleIdString(id);
        PushButtonWidget button;
        button.setConfig({{"style", styleId}, {"label", "Start"}, {"variant", "danger"}});
        ToggleSwitchWidget toggle;
        toggle.setConfig({{"style", styleId},
                          {"label", "Mode"},
                          {"onLabel", "Auto"},
                          {"offLabel", "Manual"},
                          {"defaultState", true}});
        SliderWidget slider;
        slider.setConfig({{"style", styleId}, {"label", "Setpoint"}, {"unit", " %"}});
        LedWidget led;
        led.setConfig({{"style", styleId},
                       {"label", "Alarm"},
                       {"onLabel", "On"},
                       {"offLabel", "Off"},
                       {"deviceId", "dev"},
                       {"sourceId", "0x00000001"},
                       {"topicId", "0x0101"},
                       {"fieldId", 1}});
        led.onFieldSample({1, 0x0101, 1, 0}, 0, 1.0);
        QCOMPARE(led.controlStyle().id, id);

        for (const QSize& size : sizes) {
            for (QWidget* widget : {static_cast<QWidget*>(&button), static_cast<QWidget*>(&toggle),
                                    static_cast<QWidget*>(&slider), static_cast<QWidget*>(&led)}) {
                widget->resize(size);
                const QImage image = widget->grab().toImage();
                QVERIFY2(!image.isNull(), qPrintable(styleId));
            }
        }
    }
}

// The chart styles carry the same five ids as the control styles, so one
// id can set a whole dashboard's look.
void TestControlStyle::chartStylesMatchTheControlStyles() {
    QCOMPARE(allChartStyles().size(), allControlStyles().size());
    for (ControlStyleId id : allControlStyles()) {
        const QString text = controlStyleIdString(id);
        QCOMPARE(chartStyleIdString(chartStyleFromId(text)), text);
    }
    QVERIFY(chartStyle(ChartStyleId::Panel).screws);
    QVERIFY(chartStyle(ChartStyleId::Panel).plotScreen);
    QVERIFY(chartStyle(ChartStyleId::Retro).accentChrome);
    QVERIFY(chartStyle(ChartStyleId::Retro).barCells);
    QVERIFY(controlStyleFor(ControlStyleId::Panel).screws);
    QVERIFY(!chartStyle(ChartStyleId::Dashboard).screws);
}

void TestControlStyle::gaugeShapesRoundTrip() {
    const QList<ChartGaugeShape> shapes = {ChartGaugeShape::Arc,     ChartGaugeShape::HalfCircle,
                                           ChartGaugeShape::Bar,     ChartGaugeShape::Number,
                                           ChartGaugeShape::Meter,   ChartGaugeShape::Display,
                                           ChartGaugeShape::Segments};
    QSet<QString> ids;
    for (ChartGaugeShape shape : shapes) {
        ids.insert(chartGaugeShapeId(shape));
        QCOMPARE(chartGaugeShapeFromId(chartGaugeShapeId(shape)), shape);
    }
    QCOMPARE(ids.size(), shapes.size());
    QCOMPARE(chartGaugeShapeFromId("bogus"), ChartGaugeShape::Arc);
}

// Smoke test: every gauge shape, one and three series, in every chart
// style, at a normal and a tiny size.
void TestControlStyle::everyGaugeShapePaintsInEveryChartStyle() {
    QJsonArray series;
    for (int i = 1; i <= 3; ++i) {
        series.append(
            QJsonObject{{"fieldId", i}, {"name", QString("S%1").arg(i)}, {"color", "#3b82f6"}});
    }
    for (const QString& shape :
         {QString("arc"), QString("halfCircle"), QString("bar"), QString("number"),
          QString("meter"), QString("display"), QString("segments")}) {
        for (ChartStyleId style : allChartStyles()) {
            for (int count : {1, 3}) {
                QJsonArray rings = series;
                while (rings.size() > count) {
                    rings.removeLast();
                }
                DummyGaugeWidget gauge;
                gauge.setConfig({{"min", 0},
                                 {"max", 100},
                                 {"unit", "%"},
                                 {"series", rings},
                                 {"view", QJsonObject{{"style", chartStyleIdString(style)},
                                                      {"gaugeShape", shape}}}});
                gauge.appendFieldSample(1, 0, 42.0);
                gauge.appendFieldSample(2, 0, 90.0);
                for (const QSize& size : {QSize(280, 280), QSize(30, 20)}) {
                    gauge.resize(size);
                    QVERIFY2(!gauge.grab().toImage().isNull(), qPrintable(shape));
                }
            }
        }
    }
}

QTEST_MAIN(TestControlStyle)
#include "test_controlstyle.moc"
