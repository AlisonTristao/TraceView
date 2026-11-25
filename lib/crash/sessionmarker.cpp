#include "crash/sessionmarker.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QSaveFile>

#include "core/applog.h"

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif

namespace traceview {

namespace {
// One marker per running instance: several TraceView windows may be open at
// once, and one must not take another's live marker for a crash.
constexpr char kMarkerPrefix[] = "session-";
constexpr char kMarkerSuffix[] = ".running";
constexpr int kKeptCrashFolders = 10;
constexpr char kUpdatedFromFileName[] = "updated_from.txt";

QJsonObject toJson(const SessionInfo& info) {
    QJsonObject object;
    object.insert(QStringLiteral("version"), info.version);
    object.insert(QStringLiteral("pid"), static_cast<double>(info.pid));
    object.insert(QStringLiteral("startedAt"), info.startedAt.toString(Qt::ISODate));
    object.insert(QStringLiteral("logFilePath"), info.logFilePath);
    object.insert(QStringLiteral("updatedFrom"), info.updatedFrom);
    object.insert(QStringLiteral("crashDir"), info.crashDir);
    return object;
}

SessionInfo fromJson(const QJsonObject& object) {
    SessionInfo info;
    info.version = object.value(QStringLiteral("version")).toString();
    info.pid = static_cast<qint64>(object.value(QStringLiteral("pid")).toDouble());
    info.startedAt =
        QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
    info.logFilePath = object.value(QStringLiteral("logFilePath")).toString();
    info.updatedFrom = object.value(QStringLiteral("updatedFrom")).toString();
    info.crashDir = object.value(QStringLiteral("crashDir")).toString();
    return info;
}

bool processIsAlive(qint64 pid) {
    if (pid <= 0) return false;
#if defined(Q_OS_WIN)
    HANDLE process =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD exitCode = 0;
    const bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
#endif
}

SessionInfo readMarker(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? fromJson(document.object()) : SessionInfo();
}
}  // namespace

SessionMarker::SessionMarker(const QString& directory)
    : m_directory(directory.isEmpty() ? AppLog::logDirectory() : directory) {}

QString SessionMarker::markerFilePath() const {
    return QDir(m_directory).filePath(QString::fromLatin1(kMarkerPrefix) +
                                      QString::number(QCoreApplication::applicationPid()) +
                                      QString::fromLatin1(kMarkerSuffix));
}

void SessionMarker::begin() {
    m_previous = SessionInfo();
    m_previousCrashed = false;

    // A marker whose process is gone never reached end(): that run crashed
    // or was killed. Report the newest one and remove them all, so each crash
    // is reported once. Markers of instances still running are left alone.
    // Our own pid's marker, if present, is a crashed run whose pid got reused.
    const QString ownMarker = markerFilePath();
    const QDir directory(m_directory);
    const QStringList markers = directory.entryList(
        {QString::fromLatin1(kMarkerPrefix) + QLatin1Char('*') +
         QString::fromLatin1(kMarkerSuffix)},
        QDir::Files);
    QStringList liveCrashDirs;
    for (const QString& name : markers) {
        const QString path = directory.filePath(name);
        const SessionInfo info = readMarker(path);
        if (path != ownMarker && processIsAlive(info.pid)) {
            liveCrashDirs << QDir(info.crashDir).absolutePath();
            continue;
        }
        if (!m_previousCrashed || info.startedAt > m_previous.startedAt) m_previous = info;
        m_previousCrashed = true;
        QFile::remove(path);
    }

    // A clean update-quit leaves this note instead of a marker; read it once and consume it.
    QFile updatedFile(QDir(m_directory).filePath(QString::fromLatin1(kUpdatedFromFileName)));
    if (updatedFile.exists()) {
        if (updatedFile.open(QIODevice::ReadOnly)) {
            m_previous.updatedFrom = QString::fromUtf8(updatedFile.readAll()).trimmed();
            updatedFile.close();
        }
        updatedFile.remove();
    }

    // CrashHandler::install() creates a crash folder every run; most stay
    // empty. Drop the empty ones and keep only the newest few with files.
    // Kept: the previous run's folder (the crash dialog reads it) and those
    // of other instances still running (their handler writes there).
    if (!m_previous.crashDir.isEmpty()) liveCrashDirs << QDir(m_previous.crashDir).absolutePath();
    QDir crashRoot(directory.filePath(QStringLiteral("crash")));
    const QStringList crashFolders =
        crashRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
    int kept = 0;
    for (const QString& name : crashFolders) {
        QDir folder(crashRoot.filePath(name));
        if (liveCrashDirs.contains(folder.absolutePath())) continue;
        if (folder.isEmpty() || ++kept > kKeptCrashFolders) folder.removeRecursively();
    }

    m_current = SessionInfo();
    m_current.version = QCoreApplication::applicationVersion();
    m_current.pid = QCoreApplication::applicationPid();
    m_current.startedAt = QDateTime::currentDateTime();
    m_current.logFilePath = AppLog::currentLogFilePath();
    m_current.crashDir = QDir(m_directory).filePath(
        QStringLiteral("crash/") + m_current.startedAt.toString(QStringLiteral("yyyyMMdd-HHmmss")) +
        QLatin1Char('-') + QString::number(m_current.pid));

    QDir().mkpath(m_directory);
    QSaveFile file(markerFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("SessionMarker: cannot write %s: %s", qPrintable(markerFilePath()),
                 qPrintable(file.errorString()));
        return;
    }
    file.write(QJsonDocument(toJson(m_current)).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qWarning("SessionMarker: cannot commit %s: %s", qPrintable(markerFilePath()),
                 qPrintable(file.errorString()));
    }
}

void SessionMarker::end() {
    QFile::remove(markerFilePath());
}

void SessionMarker::markUpdating(const QString& fromVersion) {
    QDir().mkpath(m_directory);
    QSaveFile file(QDir(m_directory).filePath(QString::fromLatin1(kUpdatedFromFileName)));
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("SessionMarker: cannot write update note: %s", qPrintable(file.errorString()));
        return;
    }
    file.write(fromVersion.toUtf8());
    if (!file.commit()) {
        qWarning("SessionMarker: cannot commit update note: %s", qPrintable(file.errorString()));
    }
}

SessionMarker& SessionMarker::instance() {
    static SessionMarker marker;
    return marker;
}

}  // namespace traceview
