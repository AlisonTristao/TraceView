#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

#include "backend/statusseverity.h"

namespace traceview {

// One line of history for the status-bar messages MainWindow shows and then
// lets scroll away. Recorded by NotificationLog (notificationlog.h) and
// displayed by NotificationHistoryWindow (notificationhistorywindow.h).
//
// `source` is a free-text origin hint -- a device name for anything a
// DeviceConnection's Backend emitted, empty for app-level messages
// (workspace created, key conflict, ...). `timestamp` is wall-clock local
// time, taken when the message was posted.
struct NotificationEntry {
    QDateTime timestamp;
    QString text;
    StatusSeverity severity = StatusSeverity::Info;
    QString source;
};

}  // namespace traceview

Q_DECLARE_METATYPE(traceview::NotificationEntry)
