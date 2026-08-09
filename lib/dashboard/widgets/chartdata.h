#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace traceview {

// Everything below is pure data/logic -- no QWidget, no painting -- so it can
// be unit tested with synthetic JSON/payloads (see tests/test_chartdata.cpp).
// It mirrors the JSON shape ChartConfigEditor::config()/setConfig() already
// define (lib/dashboard/widgets/chartconfigeditor.cpp) and the wire format
// closed in docs/PROTOCOL.md.

enum class ChartPayloadFormat { Csv, Bytes };
enum class ChartXAxisMode { Samples, Time };
enum class ChartYAxisMode { Auto, Fixed };
enum class ChartSeriesStyle { Solid, Dashed, Dotted, DashDot, Cross, Asterisk };

// Matches ChartConfigEditor's kByteTypeIds -- the primitive a "Bytes" format
// slot is packed as (docs/PROTOCOL.md "Inbound: payload encoding").
enum class ChartByteType { UInt8, Int8, UInt16, Int16, UInt32, Int32, Float32, Float64 };

// Width in bytes of one hex-encoded slot for `type` (PROTOCOL.md: hex string
// length is always exactly 2 * this).
int chartByteTypeSize(ChartByteType type);

struct ChartSeriesConfig {
    QString name;
    int index = 0;
    QColor color = QColor("#3B82F6");
    ChartSeriesStyle style = ChartSeriesStyle::Solid;
    ChartByteType byteType = ChartByteType::Float32;
};

struct ChartConfig {
    ChartPayloadFormat format = ChartPayloadFormat::Csv;
    int count = 1;

    ChartXAxisMode xAxisMode = ChartXAxisMode::Samples;
    double sampleTimeMs = 100.0;
    int xLimit = 500;

    ChartYAxisMode yAxisMode = ChartYAxisMode::Auto;
    double yMin = 0.0;
    double yMax = 100.0;
    QString yUnit;
    bool showGrid = true;

    QVector<ChartSeriesConfig> series;
};

// Parses a ChartConfigEditor JSON config; missing fields fall back to the
// same defaults ChartConfigEditor::setConfig() uses, so an empty/default
// QJsonObject (a freshly-inserted item with no edits yet) behaves the same
// as what the properties panel would show.
ChartConfig parseChartConfig(const QJsonObject& json);

// How many samples of history a series buffer should retain: `xLimit`
// directly in Samples mode, or the number of samples spanning `xLimit`
// seconds at `sampleTimeMs` in Time mode. Always >= 1.
int chartBufferCapacity(const ChartConfig& config);

// Re-derives one buffer per config.series (in order) from `previous`,
// carrying over history for a series still at the same row position --
// editing a series' name/color/style shouldn't clear its data -- then
// trimming every buffer to chartBufferCapacity(config). Safe to call with
// `previous` empty (fresh widget) or a different series count (added/
// removed/reordered rows just don't carry over past their old position).
QVector<QVector<double>> resizeChartBuffers(const QVector<QVector<double>>& previous, const ChartConfig& config);

// Decodes one payload line (docs/PROTOCOL.md "Inbound: payload encoding")
// into one value per config.series, in order. A series whose slot is
// missing (payload has fewer ';'-separated slots than its `index`) or
// malformed for its declared Format/byteType yields qQNaN() at that
// position; callers should skip NaN entries rather than treat them as 0.
QVector<double> decodeChartPayload(const QByteArray& payload, const ChartConfig& config);

// Decodes `payload` and appends each non-NaN value to its series' buffer,
// trimming from the front to stay within chartBufferCapacity(config). A
// slot that decodes as NaN this frame is simply skipped for that series
// this call (see decodeChartPayload) rather than padded with a gap value.
void appendChartSample(QVector<QVector<double>>& buffers, const ChartConfig& config, const QByteArray& payload);

// Mirrors GaugeConfigEditor::config()/setConfig() (widgets/gaugeconfigeditor.h)
// -- the single-series subset of ChartConfig: how to read the one slot this
// gauge displays (Format/index/byteType), plus the fixed range/unit/decimals
// used to scale and label it. No history/axis settings, since a gauge only
// ever shows the current value.
struct GaugeConfig {
    ChartPayloadFormat format = ChartPayloadFormat::Csv;
    int index = 0;
    ChartByteType byteType = ChartByteType::Float32;
    double min = 0.0;
    double max = 100.0;
    QString unit;
    int decimals = 0;
};

// Parses a GaugeConfigEditor JSON config; missing fields fall back to the
// same defaults GaugeConfigEditor::setConfig() uses.
GaugeConfig parseGaugeConfig(const QJsonObject& json);

// Decodes one payload line's slot at config.index per config.format/byteType
// (docs/PROTOCOL.md "Inbound: payload encoding"). Returns qQNaN() if the
// slot is missing or malformed -- callers should show "no data" rather than
// treat that as 0. Reuses decodeChartPayload's per-slot decoding with a
// synthetic single-series ChartConfig.
double decodeGaugeValue(const QByteArray& payload, const GaugeConfig& config);

} // namespace traceview
