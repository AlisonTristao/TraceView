#include "chartdata.h"

#include <QJsonArray>
#include <QMap>
#include <QtMath>
#include <cmath>

namespace traceview {

namespace {

ChartSeriesStyle seriesStyleFromId(const QString& id) {
    static const QMap<QString, ChartSeriesStyle> kMap = {
        {"solid", ChartSeriesStyle::Solid},   {"dashed", ChartSeriesStyle::Dashed},
        {"dotted", ChartSeriesStyle::Dotted}, {"dashdot", ChartSeriesStyle::DashDot},
        {"cross", ChartSeriesStyle::Cross},   {"asterisk", ChartSeriesStyle::Asterisk},
    };
    return kMap.value(id, ChartSeriesStyle::Solid);
}

ChartSeriesConfig parseSeriesConfig(const QJsonObject& json) {
    ChartSeriesConfig series;
    series.name = json.value("name").toString();
    series.fieldId = quint16(qBound(0, json.value("fieldId").toInt(0), 65535));
    const QColor color(json.value("color").toString("#3B82F6"));
    series.color = color.isValid() ? color : QColor("#3B82F6");
    series.style = seriesStyleFromId(json.value("style").toString("solid"));
    return series;
}

quint32 parseSourceId(const QJsonObject& json) {
    return quint32(json.value("sourceId").toString("0").toULongLong(nullptr, 0));
}

quint16 parseTopicId(const QJsonObject& json) {
    return quint16(qBound(0, json.value("topicId").toString("0").toInt(nullptr, 0), 65535));
}

}  // namespace

QString chartLineInterpolationId(ChartLineInterpolation mode) {
    switch (mode) {
        case ChartLineInterpolation::ZeroOrderHold:
            return QStringLiteral("zoh");
        case ChartLineInterpolation::Stem:
            return QStringLiteral("stem");
        case ChartLineInterpolation::None:
            return QStringLiteral("none");
        default:
            return QStringLiteral("linear");
    }
}

ChartLineInterpolation chartLineInterpolationFromId(const QString& id) {
    static const QMap<QString, ChartLineInterpolation> kMap = {
        {"linear", ChartLineInterpolation::Linear},
        {"zoh", ChartLineInterpolation::ZeroOrderHold},
        {"stem", ChartLineInterpolation::Stem},
        {"none", ChartLineInterpolation::None},
    };
    return kMap.value(id, ChartLineInterpolation::Linear);
}

ChartConfig parseChartConfig(const QJsonObject& json) {
    ChartConfig config;
    config.sourceId = parseSourceId(json);
    config.topicId = parseTopicId(json);

    const QJsonObject xAxis = json.value("xAxis").toObject();
    config.xAxisMode = xAxis.value("mode").toString("samples") == "time" ? ChartXAxisMode::Time
                                                                         : ChartXAxisMode::Samples;
    config.sampleTimeMs = xAxis.value("sampleTimeMs").toDouble(100.0);
    config.xLimit = xAxis.value("limit").toInt(500);

    const QJsonObject yAxis = json.value("yAxis").toObject();
    config.yAxisMode = yAxis.value("mode").toString("auto") == "fixed" ? ChartYAxisMode::Fixed
                                                                       : ChartYAxisMode::Auto;
    config.yMin = yAxis.value("min").toDouble(0.0);
    config.yMax = yAxis.value("max").toDouble(100.0);
    config.yUnit = yAxis.value("unit").toString();
    config.showGrid = yAxis.value("grid").toBool(true);

    for (const QJsonValue& value : json.value("series").toArray()) {
        config.series.append(parseSeriesConfig(value.toObject()));
    }
    return config;
}

int chartBufferCapacity(const ChartConfig& config) {
    if (config.xAxisMode == ChartXAxisMode::Time) {
        const double sampleTimeMs = qMax(0.001, config.sampleTimeMs);
        const qint64 samples = qint64(std::ceil((qMax(1, config.xLimit) * 1000.0) / sampleTimeMs));
        return int(qBound(qint64(1), samples, qint64(1'000'000)));
    }
    return qBound(1, config.xLimit, 1'000'000);
}

QVector<TelemetrySeriesBuffer> resizeChartBuffers(const QVector<TelemetrySeriesBuffer>& previous,
                                                  const ChartConfig& config) {
    QVector<TelemetrySeriesBuffer> result(config.series.size());
    for (int i = 0; i < result.size() && i < previous.size(); ++i) {
        result[i] = previous[i];
    }

    const int capacity = chartBufferCapacity(config);
    for (TelemetrySeriesBuffer& buffer : result) {
        buffer.setCapacity(capacity);
    }
    return result;
}

void appendFieldSample(QVector<TelemetrySeriesBuffer>& buffers, const ChartConfig& config,
                       quint16 fieldId, quint64 timestampUs, double value) {
    for (int i = 0; i < config.series.size() && i < buffers.size(); ++i) {
        if (config.series[i].fieldId == fieldId) {
            buffers[i].append(timestampUs, value);
        }
    }
}

GaugeSeriesConfig parseGaugeSeriesConfig(const QJsonObject& json) {
    GaugeSeriesConfig series;
    series.name = json.value("name").toString();
    series.fieldId = quint16(qBound(0, json.value("fieldId").toInt(0), 65535));
    const QColor color(json.value("color").toString("#3B82F6"));
    series.color = color.isValid() ? color : QColor("#3B82F6");
    return series;
}

GaugeConfig parseGaugeConfig(const QJsonObject& json) {
    GaugeConfig config;
    config.sourceId = parseSourceId(json);
    config.topicId = parseTopicId(json);
    config.min = json.value("min").toDouble(0.0);
    config.max = json.value("max").toDouble(100.0);
    config.unit = json.value("unit").toString();
    config.decimals = json.value("decimals").toInt(0);

    if (json.contains("series")) {
        for (const QJsonValue& value : json.value("series").toArray()) {
            config.series.append(parseGaugeSeriesConfig(value.toObject()));
        }
    } else if (json.contains("fieldId")) {
        // Pre-multi-ring save: a bare top-level fieldId instead of a series
        // array. Migrated in-memory to a single unnamed default-colored ring.
        GaugeSeriesConfig legacy;
        legacy.fieldId = quint16(qBound(0, json.value("fieldId").toInt(0), 65535));
        config.series.append(legacy);
    } else {
        // Never configured at all (freshly added widget, config still the
        // default empty object) -- default to one ring so a brand-new gauge
        // still shows a placeholder track instead of a blank widget, same as
        // before multi-ring support existed.
        config.series.append(GaugeSeriesConfig());
    }
    return config;
}

}  // namespace traceview
