#pragma once

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "telemetry/telemetryseriesbuffer.h"

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

// How a line chart reconstructs a continuous shape between buffered samples
// -- a chart-wide setting (unlike ChartSeriesStyle's per-series color/dash),
// picked from the header gear menu's interpolation select box rather than
// stored in ChartConfig, so it lives as plain ChartWidgetBase state
// (chartwidgets.h) rather than a field here. Declared here anyway, alongside
// the chart enums it's a sibling of, with its own id-string mapping below
// (chartLineInterpolationId()/FromId()) for the same reason ChartSeriesStyle
// round-trips through "solid"/"dashed"/etc: DashboardWidget's generic gear-menu
// interface (dashboardwidget.h) exposes the current mode as a QString id
// rather than this enum type, so that base class stays free of any one
// widget kind's types.
//   Linear         -- straight line between consecutive samples (the
//                      original, still-default behavior).
//   ZeroOrderHold   -- step/staircase: holds each sample's value flat until
//                      the next one arrives, then jumps -- how a DAC would
//                      actually replay a sampled signal.
//   Stem            -- a vertical "lollipop" from the zero baseline up to
//                      each sample, no connecting line between samples.
//   None            -- a small dot per sample, no connecting line and no
//                      baseline stem either.
enum class ChartLineInterpolation { Linear, ZeroOrderHold, Stem, None };

// Round-trips ChartLineInterpolation through the "linear"/"zoh"/"stem"/"none"
// id strings DashboardWidget::lineInterpolation()/setLineInterpolation() (and
// the gear menu's combo box, dashboardcell.cpp) traffic in. FromId() falls
// back to Linear for any unrecognized id, same as seriesStyleFromId() falling
// back to Solid.
QString chartLineInterpolationId(ChartLineInterpolation mode);
ChartLineInterpolation chartLineInterpolationFromId(const QString& id);

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

// One ring of a (possibly multi-ring) gauge: which field it tracks and the
// color its track/pointer are painted in. Mirrors ChartSeriesConfig's shape
// minus `style` -- an arc has nothing analogous to a line dash pattern.
struct GaugeSeriesConfig {
    QString name;
    quint16 fieldId = 0;  // binds to a TelemetryFieldSchema::fieldId within
                          // (sourceId, topicId) below.
    QColor color = QColor("#3B82F6");
};

// Mirrors GaugeConfigEditor::config()/setConfig() (widgets/gaugeconfigeditor.h)
// -- the single-topic subset of ChartConfig: which BTP source/topic this
// gauge reads from, plus the fixed range/unit/decimals shared by every ring
// (so concentric rings stay on one comparable scale) and, per ring, which
// field it displays and what color it's painted in. No history/axis
// settings, since a gauge only ever shows each field's current value.
struct GaugeConfig {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    double min = 0.0;
    double max = 100.0;
    QString unit;
    int decimals = 0;
    QVector<GaugeSeriesConfig> series;
};

// Parses a GaugeConfigEditor JSON config; missing fields fall back to the
// same defaults GaugeConfigEditor::setConfig() uses. A JSON object with no
// "series" array but a legacy top-level "fieldId" (saved before gauges
// supported more than one ring) is migrated in-memory to a single series --
// old saved dashboards keep loading as a one-ring gauge without a save-file
// upgrade step.
GaugeConfig parseGaugeConfig(const QJsonObject& json);

}  // namespace traceview
