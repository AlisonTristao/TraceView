#pragma once

#include <QDateTime>
#include <QString>

namespace traceview {

// What the marker file of one run says about it.
struct SessionInfo {
    QString version;      // app version that wrote the marker
    qint64 pid = 0;
    QDateTime startedAt;
    QString logFilePath;  // AppLog::currentLogFilePath() of that run
    QString updatedFrom;  // set by markUpdating(): version that quit to update
    QString crashDir;     // where CrashHandler writes stack/dump files for that run
};

// One small file per running instance ("session-<pid>.running") that exists
// only while that instance runs. begin() at startup: a marker whose process
// is gone never reached end() -- it crashed, was killed, or the machine lost
// power -- and previousSessionCrashed() says so. Markers of other instances
// still running are left alone. end() runs on a clean quit (aboutToQuit),
// which is also how TraceView leaves to install an update, so an update
// never looks like a crash. Qt only; same on every platform.
class SessionMarker {
public:
    // `directory` defaults to AppLog::logDirectory(); tests pass a temp dir.
    explicit SessionMarker(const QString& directory = QString());

    // Reads the previous run's marker (if any), then writes this run's.
    void begin();
    // Removes the marker. Safe to call twice.
    void end();
    // Records, before quitting to install an update, the version being left;
    // the next run reads it back as previousSession().updatedFrom.
    void markUpdating(const QString& fromVersion);

    bool previousSessionCrashed() const { return m_previousCrashed; }
    // The previous run's info. updatedFrom is filled after a clean
    // update-quit too, when previousSessionCrashed() is false.
    const SessionInfo& previousSession() const { return m_previous; }
    const SessionInfo& currentSession() const { return m_current; }

    QString markerFilePath() const;
    // Per-run folder for crash files: <directory>/crash/<yyyyMMdd-HHmmss>-<pid>.
    QString currentCrashDirectory() const { return m_current.crashDir; }

    // App-wide instance used by main.cpp, MainWindow and the updater.
    static SessionMarker& instance();

private:
    QString m_directory;
    SessionInfo m_previous;
    SessionInfo m_current;
    bool m_previousCrashed = false;
};

}  // namespace traceview
