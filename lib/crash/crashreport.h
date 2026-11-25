#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include "crash/sessionmarker.h"

namespace traceview {

// Everything a bug report carries, gathered from disk. Qt only, no widgets,
// so it is unit-testable.
struct CrashReport {
    bool crashed = false;       // false: opened from Help -> Report a bug
    QString appVersion;
    QString osName;             // QSysInfo::prettyProductName() + cpu arch
    QDateTime when;             // crash run's start, or now for a manual report
    QString updatedFrom;        // the crashed run had just been updated from this
    QStringList logTail;        // last lines of that run's log
    QString stackTrace;         // contents of stack.txt, empty if none
    QString dumpFilePath;       // minidump.dmp if it exists
    QString logFilePath;
    QString userDescription;    // typed in the dialog

    static constexpr int kLogTailLines = 200;

    // After a crash: from the previous session's marker info.
    static CrashReport fromCrashedSession(const SessionInfo& previous);
    // Help -> Report a bug: the current run's log, no stack.
    static CrashReport fromCurrentSession(const SessionInfo& current);

    // Plain text for the clipboard / issue body (Markdown-friendly).
    QString toText() const;
    // Writes report.txt, a copy of the log and of the dump (if any) into a
    // new folder under `parentDir`; returns that folder, or empty on failure.
    QString saveToFolder(const QString& parentDir, QString* error = nullptr) const;
    // Pre-filled "new issue" URL (title only -- the body goes by clipboard,
    // URLs have a length limit and logs can be private).
    QString issueUrl() const;

    // Last `count` lines of a text file (empty list if unreadable).
    static QStringList readTail(const QString& filePath, int count);
};

}  // namespace traceview
