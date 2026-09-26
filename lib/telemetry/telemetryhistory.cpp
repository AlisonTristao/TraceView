#include "telemetry/telemetryhistory.h"

namespace traceview {

void TelemetryHistory::setRetention(const QHash<TelemetryHistoryKey, int>& capacities) {
    QHash<TelemetryHistoryKey, TelemetrySeriesBuffer> kept;
    kept.reserve(capacities.size());
    for (auto it = capacities.constBegin(); it != capacities.constEnd(); ++it) {
        TelemetrySeriesBuffer buffer = m_buffers.value(it.key());
        buffer.setCapacity(qMax(1, it.value()));
        kept.insert(it.key(), buffer);
    }
    m_buffers = kept;
}

void TelemetryHistory::append(const QString& deviceId, const TelemetryFieldBinding& binding,
                              quint64 timestampUs, double value) {
    const auto it =
        m_buffers.find({deviceId, binding.sourceId, binding.topicId, binding.fieldId});
    if (it != m_buffers.end()) {
        it->append(timestampUs, value);
    }
}

const TelemetrySeriesBuffer* TelemetryHistory::buffer(const TelemetryHistoryKey& key) const {
    const auto it = m_buffers.constFind(key);
    return it == m_buffers.constEnd() ? nullptr : &it.value();
}

void TelemetryHistory::clear(const TelemetryHistoryKey& key) {
    const auto it = m_buffers.find(key);
    if (it != m_buffers.end()) {
        it->clear();
    }
}

}  // namespace traceview
