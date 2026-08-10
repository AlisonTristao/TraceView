#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "protocol/telemetryseriesbuffer.h"

namespace traceview {

// Everything below is pure data/logic -- no QWidget, no painting -- so it can
// be unit tested with synthetic samples (see tests/test_chartdata.cpp). It
// mirrors the JSON shape ChartConfigEditor::config()/setConfig() define
// (lib/dashboard/widgets/chartconfigeditor.cpp).
//
// Since topico 14 (BTP client and data model), a series binds to a BTP
// field by identity -- (sourceId, topicId, fieldId), TELEMETRY.md section 8
// -- instead of a slot index into a delimited/hex-encoded text payload. The
// old `format`/`byteType` config (CSV vs. Bytes, per-slot primitive type)
// duplicated encoding/type information that now belongs entirely to the
// BTP schema (see protocol/telemetrycatalog.h); it has been removed rather
// than kept as a second source of truth. Actually wiring a chart's series to
// TelemetryFieldRouter::fieldSample() -- i.e., calling appendFieldSample()
// below when a matching sample arrives -- is topico 15's job ("fatia
// vertical de telemetria binaria"); this header only defines the config
// shape and the buffer bookkeeping it needs.

enum class ChartXAxisMode { Samples, Time };
enum class ChartYAxisMode { Auto, Fixed };
enum class ChartSeriesStyle { Solid, Dashed, Dotted, DashDot, Cross, Asterisk };

struct ChartSeriesConfig {
    QString name;
    quint16 fieldId = 0;  // binds to a TelemetryFieldSchema::fieldId within
                          // (sourceId, topicId) below.
    QColor color = QColor("#3B82F6");
    ChartSeriesStyle style = ChartSeriesStyle::Solid;
};

struct ChartConfig {
    quint32 sourceId = 0;  // BTP source_id this chart reads from
    quint16 topicId = 0;   // BTP topic_id (TELEMETRY.md section 2) this
                            // chart's series are fields of

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
QVector<TelemetrySeriesBuffer> resizeChartBuffers(const QVector<TelemetrySeriesBuffer>& previous,
                                                   const ChartConfig& config);

// Appends (timestampUs, value) to every series buffer in `buffers` whose
// config.series[i].fieldId == fieldId (usually zero or one, but nothing
// stops two series from plotting the same field differently styled), each
// trimmed to chartBufferCapacity(config). A no-op if no series binds to
// `fieldId`.
void appendFieldSample(QVector<TelemetrySeriesBuffer>& buffers, const ChartConfig& config, quint16 fieldId,
                        quint64 timestampUs, double value);

// Mirrors GaugeConfigEditor::config()/setConfig() (widgets/gaugeconfigeditor.h)
// -- the single-field subset of ChartConfig: which field this gauge
// displays, plus the fixed range/unit/decimals used to scale and label it.
// No history/axis settings, since a gauge only ever shows the current value.
struct GaugeConfig {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 fieldId = 0;
    double min = 0.0;
    double max = 100.0;
    QString unit;
    int decimals = 0;
};

// Parses a GaugeConfigEditor JSON config; missing fields fall back to the
// same defaults GaugeConfigEditor::setConfig() uses.
GaugeConfig parseGaugeConfig(const QJsonObject& json);

}  // namespace traceview
