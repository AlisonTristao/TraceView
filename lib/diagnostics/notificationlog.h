#pragma once

#include <QObject>
#include <QVector>

#include "diagnostics/notificationentry.h"

namespace traceview {

// In-memory history of every status-bar message posted during this run of the
// app. MainWindow owns one instance and funnels every toast through it (see
// MainWindow::postStatus) in addition to calling statusBar()->showMessage();
// NotificationHistoryWindow renders it.
//
// A bounded ring: the oldest entries are dropped once kCapacity is reached, so
// a long session with a chatty link cannot grow this without limit. Nothing
// here is persisted -- it starts empty on every launch, same as the status bar
// itself.
class NotificationLog : public QObject {
    Q_OBJECT

public:
    static constexpr int kCapacity = 500;

    explicit NotificationLog(QObject* parent = nullptr);

    // Oldest-first, at most kCapacity entries.
    const QVector<NotificationEntry>& entries() const {
        return m_entries;
    }

    void append(const NotificationEntry& entry);
    void clear();

signals:
    // `entry` is the one just appended. `droppedOldest` is true when adding it
    // pushed the buffer over kCapacity and the front entry was evicted, so a
    // view can drop its first row instead of rebuilding.
    void entryAdded(const traceview::NotificationEntry& entry, bool droppedOldest);
    void cleared();

private:
    QVector<NotificationEntry> m_entries;
};

}  // namespace traceview
