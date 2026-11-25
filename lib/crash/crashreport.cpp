#include "crash/crashreport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>
#include <QUrl>

#include "crash/crashhandler.h"

namespace traceview {

namespace {

// Where "Open issue" sends the user. The destination is still to be decided
// (GitHub issues / e-mail / AQTech server); change only this constant.
constexpr char kIssueBaseUrl[] = "https://github.com/AlisonTristao/TraceView/issues/new";

constexpr qint64 kTailWindowBytes = 256 * 1024;

QString systemName() {
    return QStringLiteral("%1 (%2)").arg(QSysInfo::prettyProductName(),
                                         QSysInfo::currentCpuArchitecture());
}

QString readTextFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

CrashReport CrashReport::fromCrashedSession(const SessionInfo& previous) {
    CrashReport report;
    report.crashed = true;
    report.appVersion = previous.version;
    report.osName = systemName();
    report.when = previous.startedAt;
    report.updatedFrom = previous.updatedFrom;
    report.logFilePath = previous.logFilePath;
    report.logTail = readTail(previous.logFilePath, kLogTailLines);
    if (!previous.crashDir.isEmpty()) {
        const QDir dir(previous.crashDir);
        const QString stackPath = dir.filePath(CrashHandler::stackFileName());
        if (QFileInfo::exists(stackPath)) {
            report.stackTrace = readTextFile(stackPath).trimmed();
        }
        const QString dumpPath = dir.filePath(CrashHandler::dumpFileName());
        if (QFileInfo::exists(dumpPath)) {
            report.dumpFilePath = dumpPath;
        }
    }
    return report;
}

CrashReport CrashReport::fromCurrentSession(const SessionInfo& current) {
    CrashReport report;
    report.crashed = false;
    report.appVersion = QCoreApplication::applicationVersion();
    report.osName = systemName();
    report.when = QDateTime::currentDateTime();
    report.logFilePath = current.logFilePath;
    report.logTail = readTail(current.logFilePath, kLogTailLines);
    return report;
}

QStringList CrashReport::readTail(const QString& filePath, int count) {
    if (filePath.isEmpty() || count <= 0) {
        return {};
    }
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const qint64 start = qMax<qint64>(0, file.size() - kTailWindowBytes);
    file.seek(start);
    const QString text = QString::fromUtf8(file.readAll());
    QStringList lines = text.split(QLatin1Char('\n'));
    if (start > 0 && !lines.isEmpty()) {
        lines.removeFirst();  // cut mid-line by the seek
    }
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();  // the file's final newline
    }
    if (lines.size() > count) {
        lines = lines.mid(lines.size() - count);
    }
    return lines;
}

QString CrashReport::toText() const {
    QString out;
    out += crashed ? QStringLiteral("## TraceView crash report\n\n")
                   : QStringLiteral("## TraceView bug report\n\n");
    out += QStringLiteral("- Version: %1\n").arg(appVersion);
    out += QStringLiteral("- System: %1\n").arg(osName);
    out += QStringLiteral("- When: %1\n")
               .arg(when.isValid() ? when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                                   : QStringLiteral("unknown"));
    if (!updatedFrom.isEmpty()) {
        out += QStringLiteral("- Updated from: %1\n").arg(updatedFrom);
    }
    out += QStringLiteral("\n### What I was doing\n\n");
    const QString description = userDescription.trimmed();
    out += description.isEmpty() ? QStringLiteral("(not filled in)") : description;
    out += QLatin1Char('\n');
    if (!stackTrace.trimmed().isEmpty()) {
        out += QStringLiteral("\n### Stack trace\n\n```\n%1\n```\n").arg(stackTrace.trimmed());
    }
    out += QStringLiteral("\n### Log (last %1 lines)\n\n```\n%2\n```\n")
               .arg(logTail.size())
               .arg(logTail.join(QLatin1Char('\n')));
    return out;
}

QString CrashReport::saveToFolder(const QString& parentDir, QString* error) const {
    const auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return QString();
    };

    const QString name =
        QStringLiteral("TraceView-report-%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QDir parent(parentDir);
    if (!parent.mkpath(name)) {
        return fail(QStringLiteral("Could not create the folder %1").arg(parent.filePath(name)));
    }
    const QDir folder(parent.filePath(name));

    QFile reportFile(folder.filePath(QStringLiteral("report.txt")));
    if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        reportFile.write(toText().toUtf8()) < 0) {
        return fail(
            QStringLiteral("Could not write report.txt: %1").arg(reportFile.errorString()));
    }
    reportFile.close();

    if (!logFilePath.isEmpty() && QFileInfo::exists(logFilePath)) {
        if (!QFile::copy(logFilePath, folder.filePath(QStringLiteral("traceview.log")))) {
            return fail(QStringLiteral("Could not copy the log file"));
        }
    }
    if (!dumpFilePath.isEmpty() && QFileInfo::exists(dumpFilePath)) {
        if (!QFile::copy(dumpFilePath, folder.filePath(QFileInfo(dumpFilePath).fileName()))) {
            return fail(QStringLiteral("Could not copy the crash dump"));
        }
    }
    return folder.absolutePath();
}

QString CrashReport::issueUrl() const {
    const QString title = crashed ? QStringLiteral("Crash in v%1").arg(appVersion)
                                  : QStringLiteral("Bug report v%1").arg(appVersion);
    return QStringLiteral("%1?title=%2&labels=bug")
        .arg(QLatin1String(kIssueBaseUrl), QString::fromLatin1(QUrl::toPercentEncoding(title)));
}

}  // namespace traceview
