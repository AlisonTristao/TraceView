#include "dashboard/widgetagenthelp.h"

#include <QHash>

namespace traceview {

namespace {

// Shared by every telemetry-bound widget.
const char kBinding[] =
    "Binding: deviceId (from get_app_state), sourceId as hex text \"0x00000001\", topicId as "
    "hex text \"0x0101\" (both from list_signals).";

}  // namespace

QString widgetAgentHelp(const QString& typeId) {
    static const QHash<QString, QString> kHelp = {
        {"dummy_line",
         QString::fromLatin1(
             "Line chart of one topic's fields over time. %1 series: [{fieldId, name, color "
             "\"#rrggbb\", style solid|dashed|dotted|dashdot|cross|asterisk, unit, min?, max?}], "
             "one line per field. xAxis: {mode samples|time, limit (points kept, default 500), "
             "sampleTimeMs (subscribe period, default 100)}. yAxis: {mode auto|fixed, min, max, "
             "unit, decimals, grid bool, autoAxis bool}. Name the Y axis \"Quantity [unit]\" "
             "through yAxis.unit.")
             .arg(QLatin1String(kBinding))},
        {"dummy_bar",
         QString::fromLatin1("Bar chart: latest value of each field as a bar. Same config as "
                             "dummy_line (%1 series, xAxis, yAxis).")
             .arg(QLatin1String(kBinding))},
        {"dummy_gauge",
         QString::fromLatin1(
             "Radial gauge, one ring per field. %1 min, max, unit, decimals, series: [{fieldId, "
             "name, color}].")
             .arg(QLatin1String(kBinding))},
        {"text_board",
         QString::fromLatin1(
             "Fixed-pitch text report a topic's text (or bytes, drawn as a raster image) "
             "replaces as it arrives. %1 sampleTimeMs, text (shown until data arrives).")
             .arg(QLatin1String(kBinding))},
        {"audio_spectrum",
         QString::fromLatin1(
             "Audio spectrum/spectrogram of a stream topic. %1 seqFieldId, rateFieldId, "
             "samplesFieldId (fields of that topic), fullScale, volume 0-100.")
             .arg(QLatin1String(kBinding))},
        {"serial_monitor",
         QStringLiteral("Raw text terminal of devices' serial lines, one tab per device. "
                        "tabs: [{deviceId}].")},
        {"robot_log", QStringLiteral("Table of the robots' log records, one tab per device. tabs: "
                                     "[{deviceId}].")},
        {"chat",
         QStringLiteral("Chat-style message board (visual only). userName, ownMessagesOnRight "
                        "bool.")},
        {"push_button",
         QStringLiteral(
             "Button that sends a text command to a device. deviceId, label, variant "
             "default|success|warning|danger, mode momentary|pulse, onPress, onRelease "
             "(command text), repeatWhileHeld bool, repeatIntervalMs, longPress: {enabled, "
             "thresholdMs, command}, debounceMs, confirmBeforeSend bool.")},
        {"toggle_switch",
         QStringLiteral("On/off switch that sends a command on each change. deviceId, label, "
                        "onLabel, offLabel, defaultState bool, onCommand, offCommand, "
                        "confirmBeforeToggle bool.")},
        {"slider",
         QStringLiteral(
             "Slider that sends its value to a device. deviceId, label, min, max, step, "
             "defaultValue, unit, showValue bool, sendMode continuous|onRelease, throttleMs, "
             "commandTemplate (command text, {value} replaced by the slider value).")},
    };
    return kHelp.value(typeId);
}

}  // namespace traceview
