#pragma once

#include <QObject>
#include <QVector>

#include "diagnostics/notificationentry.h"

namespace traceview {

// In-memory history of every notification posted during this run of the app:
// MainWindow funnels every status message and every robot LOG record through
// it (see MainWindow::postStatus). The bell's panel (NotificationPanel) and
// NotificationHistoryWindow render it.
//
// A bounded ring: the oldest entries are dropped once the capacity
// (AppSettings::notificationHistoryCapacity, kCapacity by default) is
// reached, so a long session with a chatty link cannot grow this without
// limit. Nothing here is persisted -- it starts empty on every launch.
//
// It also counts what arrived since the panel was last looked at (unread),
// and the worst severity among those, for the bell's badge.
class NotificationLog : public QObject {
    Q_OBJECT

public:
    static constexpr int kCapacity = 500;

    explicit NotificationLog(QObject* parent = nullptr);

    // Oldest-first, at most the configured capacity entries.
    const QVector<NotificationEntry>& entries() const {
        return m_entries;
    }

    // Assigns the entry's id; counts it as unread.
    void append(NotificationEntry entry);
    void clear();
    // Dismisses one entry (the panel's X). False when it is no longer here.
    bool remove(quint64 id);

    int unreadCount() const {
        return m_unreadCount;
    }
    // Worst severity among the unread entries; Info when there are none.
    StatusSeverity unreadWorstSeverity() const {
        return m_unreadWorst;
    }
    void markAllRead();

signals:
    // `entry` is the one just appended. `droppedOldest` is true when adding it
    // pushed the buffer over kCapacity and the front entry was evicted, so a
    // view can drop its first row instead of rebuilding.
    void entryAdded(const traceview::NotificationEntry& entry, bool droppedOldest);
    void cleared();
    void removed(quint64 id);
    void unreadChanged();

private:
    QVector<NotificationEntry> m_entries;
    int m_capacity = kCapacity;
    quint64 m_nextId = 1;
    int m_unreadCount = 0;
    StatusSeverity m_unreadWorst = StatusSeverity::Info;
};

// Whether a notification also pops up as a card over the dashboard (it always
// lands in the log). Nothing pops up while the panel is open -- the entry
// shows up in it directly. Otherwise everything does in "all" mode; in
// bell-only mode only errors do, and only if `errorPopups` is on.
bool notificationPopsUp(StatusSeverity severity, bool bellOnly, bool errorPopups, bool panelOpen);

// Info < Success < Warning < Error, for the badge colour.
int notificationSeverityRank(StatusSeverity severity);

}  // namespace traceview
