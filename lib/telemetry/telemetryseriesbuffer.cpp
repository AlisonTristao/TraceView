#include "telemetry/telemetryseriesbuffer.h"

namespace traceview {

void TelemetrySeriesBuffer::setCapacity(int capacity) {
    m_capacity = qMax(0, capacity);
    trim();
}

void TelemetrySeriesBuffer::append(quint64 timestampUs, double value) {
    m_samples.append({timestampUs, value});
    trim();
    m_valuesDirty = true;
}

void TelemetrySeriesBuffer::clear() {
    m_samples.clear();
    m_valuesDirty = true;
}

const QVector<double>& TelemetrySeriesBuffer::values() const {
    if (!m_valuesDirty) {
        return m_valuesCache;
    }
    m_valuesCache.resize(m_samples.size());
    for (int i = 0; i < m_samples.size(); ++i) {
        m_valuesCache[i] = m_samples.at(i).value;
    }
    m_valuesDirty = false;
    return m_valuesCache;
}

void TelemetrySeriesBuffer::trim() {
    if (m_capacity <= 0) {
        return;
    }
    const int excess = m_samples.size() - m_capacity;
    if (excess <= 0) {
        return;
    }
    // One removal rather than a loop of single ones. In the append path the
    // excess is always 1 and the two are equivalent, but setCapacity() can
    // shrink the window by thousands at once -- and the cache has to be
    // invalidated here too, since that path doesn't go through append().
    m_samples.remove(0, excess);
    m_valuesDirty = true;
}

}  // namespace traceview
