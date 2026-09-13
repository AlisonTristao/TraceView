#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcSerial)
Q_DECLARE_LOGGING_CATEGORY(lcUsbHid)
Q_DECLARE_LOGGING_CATEGORY(lcConnection)
Q_DECLARE_LOGGING_CATEGORY(lcApp)

namespace traceview {

// App-wide file logger. install() opens one file per run under
// logDirectory() and installs a Qt message handler so every
// qCDebug/qCInfo/qCWarning/qCCritical call anywhere in the app -- not just
// the ones this file adds -- is appended to it, flushed after every line.
// That immediacy is the point: this exists because a serial connect
// failure once left TraceView looking like it had simply frozen, with
// nothing anywhere to say why, and a crash or hang must not take the log
// explaining it down too.
//
// Call once, after QCoreApplication::setApplicationName()/
// setOrganizationName() (logDirectory() depends on them) and before
// constructing MainWindow.
class AppLog {
public:
    static void install();

    // Absolute path to the current session's file. Empty if install()
    // hasn't run or failed to open a file in either the normal profile
    // directory or its temp-directory fallback -- logging is best-effort
    // and never blocks startup.
    static QString currentLogFilePath();
    // Directory all session log files live in -- what a "show log folder"
    // menu action should reveal. Normally under the user's own profile (no
    // admin/elevated rights needed there on either platform); install()
    // falls back to the system temp directory if that turns out to be
    // unwritable (e.g. a locked-down AV policy), and this reflects whichever
    // one it actually used.
    static QString logDirectory();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& message);
};

}  // namespace traceview
