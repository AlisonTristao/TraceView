#include "chartdata.h"

#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QtMath>

#include <cmath>
#include <cstring>

namespace traceview {

namespace {

ChartByteType byteTypeFromId(const QString& id) {
    static const QMap<QString, ChartByteType> kMap = {
        {"uint8", ChartByteType::UInt8},     {"int8", ChartByteType::Int8},
        {"uint16", ChartByteType::UInt16},   {"int16", ChartByteType::Int16},
        {"uint32", ChartByteType::UInt32},   {"int32", ChartByteType::Int32},
        {"float32", ChartByteType::Float32}, {"float64", ChartByteType::Float64},
    };
    return kMap.value(id, ChartByteType::Float32);
}

ChartSeriesStyle seriesStyleFromId(const QString& id) {
    static const QMap<QString, ChartSeriesStyle> kMap = {
        {"solid", ChartSeriesStyle::Solid},     {"dashed", ChartSeriesStyle::Dashed},
        {"dotted", ChartSeriesStyle::Dotted},   {"dashdot", ChartSeriesStyle::DashDot},
        {"cross", ChartSeriesStyle::Cross},     {"asterisk", ChartSeriesStyle::Asterisk},
    };
    return kMap.value(id, ChartSeriesStyle::Solid);
}

ChartSeriesConfig parseSeriesConfig(const QJsonObject& json) {
    ChartSeriesConfig series;
    series.name = json.value("name").toString();
    series.index = json.value("index").toInt(0);
    const QColor color(json.value("color").toString("#3B82F6"));
    series.color = color.isValid() ? color : QColor("#3B82F6");
    series.style = seriesStyleFromId(json.value("style").toString("solid"));
    series.byteType = byteTypeFromId(json.value("byteType").toString("float32"));
    return series;
}

bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Assembles up to 8 raw bytes (already validated as exactly
// chartByteTypeSize(type) long) into a little-endian bit pattern, then
// reinterprets it per `type`. Manual byte assembly (rather than a raw
// pointer cast) keeps this correct regardless of host endianness.
bool decodeHexSlot(const QByteArray& slot, ChartByteType type, double* outValue) {
    const int size = chartByteTypeSize(type);
    if (slot.size() != size * 2) {
        return false;
    }
    for (char c : slot) {
        if (!isHexDigit(c)) {
            return false;
        }
    }

    const QByteArray raw = QByteArray::fromHex(slot);
    if (raw.size() != size) {
        return false;
    }

    quint64 bits = 0;
    for (int i = size - 1; i >= 0; --i) {
        bits = (bits << 8) | quint8(raw[i]);
    }

    switch (type) {
        case ChartByteType::UInt8:
            *outValue = double(quint8(bits));
            return true;
        case ChartByteType::Int8:
            *outValue = double(qint8(bits));
            return true;
        case ChartByteType::UInt16:
            *outValue = double(quint16(bits));
            return true;
        case ChartByteType::Int16:
            *outValue = double(qint16(bits));
            return true;
        case ChartByteType::UInt32:
            *outValue = double(quint32(bits));
            return true;
        case ChartByteType::Int32:
            *outValue = double(qint32(bits));
            return true;
        case ChartByteType::Float32: {
            const quint32 bits32 = quint32(bits);
            float value = 0.0f;
            std::memcpy(&value, &bits32, sizeof(value));
            *outValue = double(value);
            return true;
        }
        case ChartByteType::Float64: {
            double value = 0.0;
            std::memcpy(&value, &bits, sizeof(value));
            *outValue = value;
            return true;
        }
    }
    return false;
}

bool decodeCsvSlot(const QByteArray& slot, double* outValue) {
    bool ok = false;
    const double value = QString::fromUtf8(slot).toDouble(&ok);
    if (ok) {
        *outValue = value;
    }
    return ok;
}

} // namespace

int chartByteTypeSize(ChartByteType type) {
    switch (type) {
        case ChartByteType::UInt8:
        case ChartByteType::Int8:
            return 1;
        case ChartByteType::UInt16:
        case ChartByteType::Int16:
            return 2;
        case ChartByteType::UInt32:
        case ChartByteType::Int32:
        case ChartByteType::Float32:
            return 4;
        case ChartByteType::Float64:
            return 8;
    }
    return 4;
}

ChartConfig parseChartConfig(const QJsonObject& json) {
    ChartConfig config;
    config.format = json.value("format").toString("csv") == "bytes" ? ChartPayloadFormat::Bytes
                                                                      : ChartPayloadFormat::Csv;
    config.count = json.value("count").toInt(1);

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

QVector<QVector<double>> resizeChartBuffers(const QVector<QVector<double>>& previous, const ChartConfig& config) {
    QVector<QVector<double>> result(config.series.size());
    for (int i = 0; i < result.size() && i < previous.size(); ++i) {
        result[i] = previous[i];
    }

    const int capacity = chartBufferCapacity(config);
    for (QVector<double>& buffer : result) {
        while (buffer.size() > capacity) {
            buffer.removeFirst();
        }
    }
    return result;
}

QVector<double> decodeChartPayload(const QByteArray& payload, const ChartConfig& config) {
    // Not named "slots": that identifier collides with Qt's `slots` macro
    // (used in `signals:`/`slots:` class sections), which the preprocessor
    // strips to nothing outside of moc-generated code -- silently turning
    // this declaration into a syntax error.
    const QList<QByteArray> payloadSlots = payload.split(';');
    QVector<double> result(config.series.size(), qQNaN());

    for (int i = 0; i < config.series.size(); ++i) {
        const ChartSeriesConfig& series = config.series[i];
        if (series.index < 0 || series.index >= payloadSlots.size()) {
            continue;
        }
        const QByteArray& slot = payloadSlots[series.index];

        double value = 0.0;
        const bool ok = config.format == ChartPayloadFormat::Bytes ? decodeHexSlot(slot, series.byteType, &value)
                                                                    : decodeCsvSlot(slot, &value);
        if (ok) {
            result[i] = value;
        }
    }
    return result;
}

void appendChartSample(QVector<QVector<double>>& buffers, const ChartConfig& config, const QByteArray& payload) {
    const QVector<double> values = decodeChartPayload(payload, config);
    const int capacity = chartBufferCapacity(config);

    for (int i = 0; i < values.size() && i < buffers.size(); ++i) {
        if (qIsNaN(values[i])) {
            continue;
        }
        buffers[i].append(values[i]);
        while (buffers[i].size() > capacity) {
            buffers[i].removeFirst();
        }
    }
}

GaugeConfig parseGaugeConfig(const QJsonObject& json) {
    GaugeConfig config;
    config.format =
        json.value("format").toString("csv") == "bytes" ? ChartPayloadFormat::Bytes : ChartPayloadFormat::Csv;
    config.index = json.value("index").toInt(0);
    config.byteType = byteTypeFromId(json.value("byteType").toString("float32"));
    config.min = json.value("min").toDouble(0.0);
    config.max = json.value("max").toDouble(100.0);
    config.unit = json.value("unit").toString();
    config.decimals = json.value("decimals").toInt(0);
    return config;
}

double decodeGaugeValue(const QByteArray& payload, const GaugeConfig& config) {
    ChartConfig single;
    single.format = config.format;
    ChartSeriesConfig series;
    series.index = config.index;
    series.byteType = config.byteType;
    single.series.append(series);

    const QVector<double> values = decodeChartPayload(payload, single);
    return values.isEmpty() ? qQNaN() : values.first();
}

} // namespace traceview
