#include "dashboard/topicsampledispatcher.h"

#include <algorithm>
#include <iterator>

#include "backend/backend.h"

namespace traceview {

TopicSampleDispatcher* TopicSampleDispatcher::of(Backend* backend) {
    if (backend == nullptr) {
        return nullptr;
    }
    if (auto* existing =
            backend->findChild<TopicSampleDispatcher*>(QString(), Qt::FindDirectChildrenOnly)) {
        return existing;
    }
    return new TopicSampleDispatcher(backend);
}

TopicSampleDispatcher::TopicSampleDispatcher(Backend* backend) : QObject(backend) {
    connect(backend, &Backend::fieldSample, this, &TopicSampleDispatcher::dispatch);
}

void TopicSampleDispatcher::add(quint32 sourceId, quint16 topicId, QObject* receiver,
                                Handler handler) {
    if (receiver == nullptr || !handler) {
        return;
    }
    m_entries[key(sourceId, topicId)].append({receiver, std::move(handler)});
}

void TopicSampleDispatcher::remove(QObject* receiver) {
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        QVector<Entry>& entries = it.value();
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [receiver](const Entry& entry) {
                                         return entry.receiver.isNull() ||
                                                entry.receiver == receiver;
                                     }),
                      entries.end());
        it = entries.isEmpty() ? m_entries.erase(it) : std::next(it);
    }
}

int TopicSampleDispatcher::count() const {
    int total = 0;
    for (const QVector<Entry>& entries : m_entries) {
        total += int(entries.size());
    }
    return total;
}

void TopicSampleDispatcher::dispatch(const TelemetryFieldBinding& binding, quint64 timestampUs,
                                     double value) {
    const auto it = m_entries.constFind(key(binding.sourceId, binding.topicId));
    if (it == m_entries.constEnd()) {
        return;
    }
    // A copy: a handler may add or remove registrations.
    const QVector<Entry> entries = it.value();
    for (const Entry& entry : entries) {
        if (entry.receiver) {
            entry.handler(binding, timestampUs, value);
        }
    }
}

}  // namespace traceview
