#include "multisourcechartwidget.h"

#include <QJsonArray>

#include "dashboard/widgetconfigeditor.h"

namespace traceview {

namespace {

quint64 fieldKey(quint32 sourceId, quint16 topicId, quint16 fieldId) {
    return (quint64(sourceId) << 32) | (quint64(topicId) << 16) | fieldId;
}

// The rate to ask each topic for: one sample per X-axis step, as a line
// chart's own subscription does (MainWindow's requestedRateMillihzFor()).
quint32 rateFor(double sampleTimeMs) {
    if (!(sampleTimeMs > 0.0)) {
        return 10'000;  // 10 Hz
    }
    return quint32(qBound(1.0, 1'000'000.0 / sampleTimeMs, 4'000'000'000.0));
}

}  // namespace

MultiSourceLineChartWidget::Binding MultiSourceLineChartWidget::Binding::fromJson(
    const QJsonObject& json) {
    Binding binding;
    binding.deviceId = json.value(QLatin1String("deviceId")).toString();
    binding.sourceId =
        quint32(json.value(QLatin1String("sourceId")).toString("0").toULongLong(nullptr, 0));
    binding.topicId = quint16(
        qBound(0, json.value(QLatin1String("topicId")).toString("0").toInt(nullptr, 0), 65535));
    binding.fieldId = quint16(qBound(0, json.value(QLatin1String("fieldId")).toInt(0), 65535));
    binding.topicName = json.value(QLatin1String("topicName")).toString();
    binding.fieldName = json.value(QLatin1String("fieldName")).toString();
    return binding;
}

QJsonObject MultiSourceLineChartWidget::Binding::toJson() const {
    // Always the id keys, even unresolved: the binding resolver re-points a
    // binding that has them (by its names) and leaves alone one that hasn't.
    QJsonObject json{{"deviceId", deviceId},
                     {"sourceId", formatHexId(sourceId, 8)},
                     {"topicId", formatHexId(topicId, 4)},
                     {"fieldId", int(fieldId)}};
    if (!topicName.isEmpty()) {
        json.insert(QStringLiteral("topicName"), topicName);
    }
    if (!fieldName.isEmpty()) {
        json.insert(QStringLiteral("fieldName"), fieldName);
    }
    return json;
}

MultiSourceLineChartWidget::MultiSourceLineChartWidget(QWidget* parent)
    : DummyLineChartWidget(parent) {}

QString MultiSourceLineChartWidget::seriesKey(int series) {
    return QStringLiteral("s%1").arg(series);
}

QVector<MultiSourceLineChartWidget::Binding> MultiSourceLineChartWidget::bindingsOf(
    const QJsonObject& config) {
    const QJsonObject bind = config.value(QLatin1String("bind")).toObject();
    const int count = int(config.value(QLatin1String("series")).toArray().size());
    QVector<Binding> bindings;
    for (int i = 0; i < count; ++i) {
        bindings.append(Binding::fromJson(bind.value(seriesKey(i)).toObject()));
    }
    return bindings;
}

void MultiSourceLineChartWidget::setConfig(const QJsonObject& config) {
    m_bindings = bindingsOf(config);
    // The line chart underneath knows series i by the field id i + 1, on no
    // topic of its own: onFieldSample() below routes each sample to it.
    QJsonObject chart = config;
    chart.remove(QStringLiteral("bind"));
    chart.insert(QStringLiteral("sourceId"), QStringLiteral("0x00000000"));
    chart.insert(QStringLiteral("topicId"), QStringLiteral("0x0000"));
    QJsonArray series = config.value(QLatin1String("series")).toArray();
    m_routes.clear();
    for (int i = 0; i < series.size(); ++i) {
        QJsonObject entry = series[i].toObject();
        entry.insert(QStringLiteral("fieldId"), i + 1);
        series[i] = entry;
        const Binding& binding = m_bindings[i];
        if (binding.isBound()) {
            m_routes[fieldKey(binding.sourceId, binding.topicId, binding.fieldId)].append(
                quint16(i + 1));
        }
    }
    chart.insert(QStringLiteral("series"), series);
    DummyLineChartWidget::setConfig(chart);
    m_rateMillihz = rateFor(this->config().sampleTimeMs);
}

QVector<TelemetryTopicRequest> MultiSourceLineChartWidget::topicRequests() const {
    QVector<TelemetryTopicRequest> requests;
    for (const Binding& binding : m_bindings) {
        if (!binding.isBound()) {
            continue;
        }
        bool listed = false;
        for (const TelemetryTopicRequest& request : std::as_const(requests)) {
            listed = listed || (request.deviceId == binding.deviceId &&
                                request.sourceId == binding.sourceId &&
                                request.topicId == binding.topicId);
        }
        if (!listed) {
            requests.append({binding.deviceId, binding.sourceId, binding.topicId, m_rateMillihz});
        }
    }
    return requests;
}

void MultiSourceLineChartWidget::onFieldSample(const TelemetryFieldBinding& binding,
                                               quint64 timestampUs, double value) {
    const auto it = m_routes.constFind(fieldKey(binding.sourceId, binding.topicId, binding.fieldId));
    if (it == m_routes.constEnd()) {
        return;
    }
    for (const quint16 series : *it) {
        appendFieldSample(series, timestampUs, value);
    }
}

void MultiSourceLineChartWidget::onArraySample(const TelemetryFieldBinding&, quint64,
                                               const QVector<float>&) {}

void MultiSourceLineChartWidget::onTextSample(quint32, quint16, quint64, const QString&) {}

}  // namespace traceview
