#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVector>
#include <functional>

#include "telemetry/telemetrybinding.h"

namespace traceview {

class Backend;

// Hands each Backend::fieldSample() only to the widgets reading its
// (source, topic), instead of every widget connecting to the signal and
// dropping what is not theirs: one connection per Backend and a hash lookup
// per sample, whatever the number of widgets. A high-rate device (TraceLab,
// ~10 000 samples/s) otherwise cost every chart one slot call per sample.
class TopicSampleDispatcher : public QObject {
    Q_OBJECT

public:
    using Handler = std::function<void(const TelemetryFieldBinding& binding, quint64 timestampUs,
                                       double value)>;

    // The dispatcher of `backend`, created on first use (a child of it, so
    // it goes away with it).
    static TopicSampleDispatcher* of(Backend* backend);

    // `handler` gets the samples of (sourceId, topicId) for as long as
    // `receiver` lives or until remove(receiver).
    void add(quint32 sourceId, quint16 topicId, QObject* receiver, Handler handler);
    // Every registration of `receiver`.
    void remove(QObject* receiver);

    // Registrations, for tests.
    int count() const;

private:
    explicit TopicSampleDispatcher(Backend* backend);
    void dispatch(const TelemetryFieldBinding& binding, quint64 timestampUs, double value);

    struct Entry {
        QPointer<QObject> receiver;
        Handler handler;
    };
    static quint64 key(quint32 sourceId, quint16 topicId) {
        return (quint64(sourceId) << 16) | topicId;
    }
    QHash<quint64, QVector<Entry>> m_entries;
};

}  // namespace traceview
