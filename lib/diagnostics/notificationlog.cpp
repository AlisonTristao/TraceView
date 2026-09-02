#include "diagnostics/notificationlog.h"

#include "preferences/appsettings.h"

namespace traceview {

NotificationLog::NotificationLog(QObject* parent)
    : QObject(parent), m_capacity(AppSettings::instance().notificationHistoryCapacity()) {}

void NotificationLog::append(const NotificationEntry& entry) {
    m_entries.append(entry);
    bool droppedOldest = false;
    if (m_entries.size() > m_capacity) {
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
