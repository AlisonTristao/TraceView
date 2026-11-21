#include "diagnostics/notificationlog.h"

#include "preferences/appsettings.h"

namespace traceview {

NotificationLog::NotificationLog(QObject* parent)
    : QObject(parent), m_capacity(AppSettings::instance().notificationHistoryCapacity()) {
    m_expiryTimer.setInterval(1000);
    connect(&m_expiryTimer, &QTimer::timeout, this, [this] {
        removeOlderThan(QDateTime::currentDateTime().addSecs(-m_maxAgeSeconds));
    });
    setMaxAgeSeconds(AppSettings::instance().notificationAutoDeleteSeconds());
    connect(&AppSettings::instance(), &AppSettings::notificationPreferencesChanged, this,
            [this] { setMaxAgeSeconds(AppSettings::instance().notificationAutoDeleteSeconds()); });
}

void NotificationLog::setMaxAgeSeconds(int seconds) {
    seconds = qMax(0, seconds);
    if (seconds == m_maxAgeSeconds && m_expiryTimer.isActive() == (seconds > 0)) {
        return;
    }
    m_maxAgeSeconds = seconds;
    if (seconds > 0) {
        m_expiryTimer.start();
        removeOlderThan(QDateTime::currentDateTime().addSecs(-seconds));
    } else {
        m_expiryTimer.stop();
    }
}

int NotificationLog::removeOlderThan(const QDateTime& cutoff) {
    int removedCount = 0;
    // Oldest-first: stop at the first entry that is new enough.
    while (!m_entries.isEmpty() && m_entries.first().timestamp < cutoff) {
        removeAt(0);
        ++removedCount;
    }
    return removedCount;
}

void NotificationLog::removeAt(int index) {
    const quint64 id = m_entries.at(index).id;
    const bool wasUnread = index >= m_entries.size() - m_unreadCount;
    m_entries.removeAt(index);
    if (wasUnread) {
        --m_unreadCount;
        m_unreadWorst = StatusSeverity::Info;
        for (int i = m_entries.size() - m_unreadCount; i < m_entries.size(); ++i) {
            if (notificationSeverityRank(m_entries.at(i).severity) >
                notificationSeverityRank(m_unreadWorst)) {
                m_unreadWorst = m_entries.at(i).severity;
            }
        }
    }
    emit removed(id);
    if (wasUnread) {
        emit unreadChanged();
    }
}

void NotificationLog::append(NotificationEntry entry) {
    entry.id = m_nextId++;
    m_entries.append(entry);
    bool droppedOldest = false;
    if (m_entries.size() > m_capacity) {
        m_entries.removeFirst();
        droppedOldest = true;
    }
    // Never more unread than entries: removeAt() reads the unread ones as the
    // newest m_unreadCount.
    m_unreadCount = qMin(m_unreadCount + 1, int(m_entries.size()));
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
            removeAt(i);
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
