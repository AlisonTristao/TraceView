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
//
// The storage stays a plain QVector trimmed from the front, and that is a
// measured decision rather than an oversight. Qt 6's QList keeps free space
// at BOTH ends of its allocation, so removing from the front advances the
// begin pointer instead of moving the remaining elements: pushing 400k
// samples through a 5000-sample window costs ~13ms, i.e. ~33ns per append,
// with no dependence on the capacity. A hand-rolled sliding window over the
// same vector was written and benchmarked here, and came out slower -- it
// re-implements, less well, what QList already does.
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

    // The values alone, in order. Returned by reference into a cache that
    // is rebuilt only when the samples have actually changed.
    //
    // This is the buffer's hot path, and it used to be the expensive one: a
    // chart calls it once per series on every paint frame, and building a
    // fresh QVector<double> each time meant an allocation plus a full copy
    // per series per frame. Measured on a 5000-sample series, 2000 reads
    // cost 199ms uncached against 13ms for the 400k appends that filled it
    // -- the read path was an order of magnitude more expensive than the
    // write path it existed to serve. Cached, the same 2000 reads are
    // unmeasurable; the dirty flag costs the append path ~3ms across those
    // 400k appends.
    //
    // QVector is implicitly shared, so a caller copying the result into its
    // own container (chartwidgets.cpp builds a QVector<QVector<double>> per
    // frame) pays a refcount bump rather than another copy. Holding that
    // copy across a later append() is safe -- it detaches then, which is
    // why valuesIsStableBetweenAppends() pins the behaviour -- but it costs
    // the copy that was just avoided, so callers should not keep it beyond
    // the frame that read it.
    const QVector<double>& values() const;

private:
    void trim();

    int m_capacity = 0;
    QVector<Sample> m_samples;
    // Mutable: rebuilding the cache is a representation change values()
    // may need to make, and it does not change what this buffer holds.
    mutable QVector<double> m_valuesCache;
    mutable bool m_valuesDirty = true;
};

}  // namespace traceview
