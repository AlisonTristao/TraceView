#include "diagnostics/notificationlog.h"

#include "preferences/appsettings.h"

namespace traceview {

NotificationLog::NotificationLog(QObject* parent)
    : QObject(parent), m_capacity(AppSettings::instance().notificationHistoryCapacity()) {}

void NotificationLog::append(NotificationEntry entry) {
    entry.id = m_nextId++;
    m_entries.append(entry);
    bool droppedOldest = false;
    if (m_entries.size() > m_capacity) {
        m_entries.removeFirst();
        droppedOldest = true;
    }
    ++m_unreadCount;
    if (notificationSeverityRank(entry.severity) > notificationSeverityRank(m_unreadWorst)) {
        m_unreadWorst = entry.severity;
    }
    emit entryAdded(entry, droppedOldest);
    emit unreadChanged();
}

void NotificationLog::clear() {
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.clear();
    emit cleared();
    markAllRead();
}

bool NotificationLog::remove(quint64 id) {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            m_entries.removeAt(i);
            emit removed(id);
            return true;
        }
    }
    return false;
}

void NotificationLog::markAllRead() {
    if (m_unreadCount == 0) {
        return;
    }
    m_unreadCount = 0;
    m_unreadWorst = StatusSeverity::Info;
    emit unreadChanged();
}

int notificationSeverityRank(StatusSeverity severity) {
    switch (severity) {
        case StatusSeverity::Info:
            return 0;
        case StatusSeverity::Success:
            return 1;
        case StatusSeverity::Warning:
            return 2;
        case StatusSeverity::Error:
            return 3;
    }
    return 0;
}

bool notificationPopsUp(StatusSeverity severity, bool bellOnly, bool errorPopups, bool panelOpen) {
    if (panelOpen) return false;
    if (!bellOnly) return true;
    return errorPopups && severity == StatusSeverity::Error;
}

}  // namespace traceview
