#include "protocol/telemetryseriesbuffer.h"

namespace traceview {

void TelemetrySeriesBuffer::setCapacity(int capacity) {
    m_capacity = qMax(0, capacity);
    trim();
}

void TelemetrySeriesBuffer::append(quint64 timestampUs, double value) {
    m_samples.append({timestampUs, value});
    trim();
}

void TelemetrySeriesBuffer::clear() {
    m_samples.clear();
}

QVector<double> TelemetrySeriesBuffer::values() const {
    QVector<double> result;
    result.reserve(m_samples.size());
    for (const Sample& sample : m_samples) {
        result.append(sample.value);
    }
    return result;
}

void TelemetrySeriesBuffer::trim() {
    if (m_capacity <= 0) {
        return;
    }
    while (m_samples.size() > m_capacity) {
        m_samples.removeFirst();
    }
}

}  // namespace traceview
