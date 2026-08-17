#pragma once

#include <QVector>
#include <QtGlobal>

namespace traceview {

// A bounded ring of (timestamp_us, value) pairs for one field binding --
// replaces the old chart buffers, which were plain QVector<double> with
// nowhere to keep the origin's timestamp. Consumers that only need the
// values (e.g. a Samples-mode X axis, which counts points rather than time)
// can still get a plain QVector<double> via values(); anything that needs
// the real origin timestamp (a Time-mode axis, or any future re-derivation
// of elapsed time) uses samples() instead. Pure data, no QWidget -- see
// tests/test_telemetryseriesbuffer.cpp.
class TelemetrySeriesBuffer {
public:
    struct Sample {
        quint64 timestampUs = 0;
        double value = 0.0;
    };

    // 0 means unbounded -- not recommended for a live feed, but a safe
    // default before a widget's config has set a real capacity.
    void setCapacity(int capacity);
    int capacity() const { return m_capacity; }

    // Appends one sample, trimming from the front (oldest first) to stay
    // within capacity() -- mirrors the old appendChartSample()'s trimming
    // contract, just per-buffer instead of the caller doing it.
    void append(quint64 timestampUs, double value);
    void clear();

    const QVector<Sample>& samples() const { return m_samples; }
    QVector<double> values() const;

private:
    void trim();

    int m_capacity = 0;
    QVector<Sample> m_samples;
};

}  // namespace traceview
