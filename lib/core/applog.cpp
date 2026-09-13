#include "core/applog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

Q_LOGGING_CATEGORY(lcSerial, "traceview.serial")
Q_LOGGING_CATEGORY(lcUsbHid, "traceview.usbhid")
Q_LOGGING_CATEGORY(lcConnection, "traceview.connection")
Q_LOGGING_CATEGORY(lcApp, "traceview.app")

namespace traceview {

namespace {

// Session files beyond this are pruned, oldest first, on the next
// install() -- enough to look back several launches without the log
// directory growing without bound (there is no in-session size rotation:
// one file per run keeps a session self-contained and easy to attach to a
// bug report).
constexpr int kMaxKeptSessions = 20;

QFile* g_logFile = nullptr;
QtMessageHandler g_previousHandler = nullptr;
QString g_logFilePath;
QString g_logDirectory;

QMutex& logMutex() {
    static QMutex mutex;
    return mutex;
}

const char* levelLabel(QtMsgType type) {
    switch (type) {
        case QtDebugMsg:
            return "DEBUG";
        case QtInfoMsg:
            return "INFO ";
        case QtWarningMsg:
            return "WARN ";
        case QtCriticalMsg:
            return "ERROR";
        case QtFatalMsg:
            return "FATAL";
    }
    return "?????";
}

void pruneOldSessions(const QDir& dir) {
    QFileInfoList files =
        dir.entryInfoList({QStringLiteral("traceview_*.log")}, QDir::Files, QDir::Name);
    while (files.size() > kMaxKeptSessions) {
        QFile::remove(files.takeFirst().absoluteFilePath());
    }
}

// The normal home for the log: per-user, no elevated rights needed anywhere
// this resolves (%APPDATA%/OrgName/AppName on Windows, ~/.local/share/... on
// Linux) -- an admin-less machine is not a reason this would fail.
QString preferredLogDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/logs");
}

// Used only if the profile directory above turns out to be unwritable
// (a locked-down corporate policy or AV product blocking it, not admin
// rights) -- TempLocation is as close to "always writable by this user" as
// Qt can promise, and giving up on the log entirely here would recreate the
// exact silent failure this class exists to prevent.
QString fallbackLogDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
           QStringLiteral("/TraceView/logs");
}

// mkpath()s `dir`, prunes old sessions in it, and opens a fresh session
// file there. Returns nullptr (having leaked nothing) if the directory or
// file couldn't be created, so the caller can fall back to another
// location.
QFile* openSessionFile(const QDir& dir) {
    if (!dir.mkpath(QStringLiteral("."))) {
        return nullptr;
    }
    pruneOldSessions(dir);

    const QString fileName = QStringLiteral("traceview_%1.log")
                                  .arg(QDateTime::currentDateTime().toString(
                                      QStringLiteral("yyyyMMdd_HHmmss")));
    auto* file = new QFile(dir.filePath(fileName));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Text)) {
        delete file;
        return nullptr;
    }
    return file;
}

}  // namespace

QString AppLog::logDirectory() {
    // Whichever directory install() actually managed to open a file in --
    // preferredLogDirectory() unless that turned out to be unwritable, in
    // which case this reflects the fallback so "Open log folder" still
    // points at the right place. Before install() runs (or if it hasn't
    // been called at all), falls back to just naming the preferred one.
    return g_logDirectory.isEmpty() ? preferredLogDirectory() : g_logDirectory;
}

QString AppLog::currentLogFilePath() {
    return g_logFilePath;
}

void AppLog::install() {
    QDir dir(preferredLogDirectory());
    QFile* file = openSessionFile(dir);
    if (file == nullptr) {
        dir = QDir(fallbackLogDirectory());
        file = openSessionFile(dir);
    }
    if (file == nullptr) {
        // Neither location is writable -- proceed without a log file
        // rather than block startup over diagnostics.
        return;
    }

    g_logDirectory = dir.absolutePath();
    g_logFilePath = file->fileName();
    g_logFile = file;
    g_previousHandler = qInstallMessageHandler(&AppLog::messageHandler);

    qCInfo(lcApp) << "log started for" << QCoreApplication::applicationName()
                  << QCoreApplication::applicationVersion() << "at" << g_logFilePath;
}

void AppLog::messageHandler(QtMsgType type, const QMessageLogContext& context,
                            const QString& message) {
    if (g_logFile != nullptr) {
        QMutexLocker locker(&logMutex());
        QTextStream stream(g_logFile);
        stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " ["
               << levelLabel(type) << "] " << (context.category ? context.category : "default")
               << ": " << message << '\n';
        stream.flush();
        g_logFile->flush();
    }
    // Chained rather than replaced outright so running from a terminal/IDE
    // still shows the same messages on stderr as before this existed.
    if (g_previousHandler != nullptr) {
        g_previousHandler(type, context, message);
    }
}

}  // namespace traceview
