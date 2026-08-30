#include "diagnostics/notificationlog.h"

namespace traceview {

NotificationLog::NotificationLog(QObject* parent) : QObject(parent) {}

void NotificationLog::append(const NotificationEntry& entry) {
    m_entries.append(entry);
    bool droppedOldest = false;
    if (m_entries.size() > kCapacity) {
        m_entries.removeFirst();
        droppedOldest = true;
    }
    emit entryAdded(entry, droppedOldest);
}

void NotificationLog::clear() {
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.clear();
    emit cleared();
}

}  // namespace traceview
