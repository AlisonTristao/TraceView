#pragma once

#include <QHash>
#include <QString>
#include <QtGlobal>

#include "telemetry/telemetrybinding.h"
#include "telemetry/telemetryseriesbuffer.h"

namespace traceview {

// One plotted field of one device: the identity a chart series or gauge ring
// binds to (its config's deviceId/sourceId/topicId plus the series' fieldId).
// elementIndex is deliberately absent -- widgets filter samples by fieldId
// alone, and this key mirrors what they would have buffered.
struct TelemetryHistoryKey {
    QString deviceId;
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 fieldId = 0;

    bool operator==(const TelemetryHistoryKey& other) const {
        return deviceId == other.deviceId && sourceId == other.sourceId &&
               topicId == other.topicId && fieldId == other.fieldId;
    }
};

inline size_t qHash(const TelemetryHistoryKey& key, size_t seed = 0) {
    return qHashMulti(seed, key.deviceId, key.sourceId, key.topicId, key.fieldId);
}

// Keeps recent samples of every field some dashboard widget plots, in ANY
// workspace, independently of whether that widget currently exists. Switching
// workspaces destroys the old dashboard's widgets and builds the new one's
// from JSON, so a widget's own buffers cannot outlive a switch; this store
// does, and a freshly built widget seeds itself from it -- a chart comes back
// already showing what arrived while its workspace was hidden.
//
// Retention is demand-driven: only keys named in setRetention() are recorded,
// each capped at the capacity asked for (the caller passes the largest any
// consumer needs). Everything else a Backend emits is dropped on arrival, so
// topics nobody plots cost a hash lookup and nothing more. Pure data, no
// QWidget -- see tests/test_telemetryhistory.cpp.
class TelemetryHistory {
public:
    // Replaces the set of recorded keys. A key absent from `capacities` is
    // forgotten along with its samples; a kept key is resized in place (a
    // shrink trims its oldest samples); a new key starts empty. Capacities
    // below 1 are treated as 1.
    void setRetention(const QHash<TelemetryHistoryKey, int>& capacities);

    // Records one decoded sample for `deviceId`, if its field is retained.
    void append(const QString& deviceId, const TelemetryFieldBinding& binding,
                quint64 timestampUs, double value);

    // nullptr when `key` is not retained.
    const TelemetrySeriesBuffer* buffer(const TelemetryHistoryKey& key) const;

    // Empties `key`'s samples but keeps recording it. A no-op when `key` is
    // not retained.
    void clear(const TelemetryHistoryKey& key);

private:
    QHash<TelemetryHistoryKey, TelemetrySeriesBuffer> m_buffers;
};

}  // namespace traceview
