#include "externaleditor.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace tracelab::external {

QString findVsCode() {
#ifdef Q_OS_WIN
    // The launcher on the PATH is bin\code.cmd; Code.exe beside bin takes
    // the same arguments without a console.
    const QString launcher = QStandardPaths::findExecutable(QStringLiteral("code"));
    if (!launcher.isEmpty()) {
        const QString exe = QFileInfo(launcher).absoluteDir().absoluteFilePath(
            QStringLiteral("../Code.exe"));
        if (QFileInfo::exists(exe)) {
            return QDir::cleanPath(exe);
        }
    }
    const QStringList roots = {qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs"),
                               qEnvironmentVariable("ProgramFiles"),
                               qEnvironmentVariable("ProgramFiles(x86)")};
    for (const QString& root : roots) {
        const QString exe = root + QStringLiteral("/Microsoft VS Code/Code.exe");
        if (!root.isEmpty() && QFileInfo::exists(exe)) {
            return QDir::cleanPath(exe);
        }
    }
    return QString();
#else
    QString launcher = QStandardPaths::findExecutable(QStringLiteral("code"));
#ifdef Q_OS_MACOS
    const QString bundled = QStringLiteral(
        "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
    if (launcher.isEmpty() && QFileInfo::exists(bundled)) {
        launcher = bundled;
    }
#endif
    return launcher;
#endif
}

QStringList vsCodeArguments(const QString& path, int line, const QString& folder) {
    const QString file = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QStringList arguments;
    const QString root = QFileInfo(folder).absoluteFilePath();
    if (!folder.isEmpty() &&
        QFileInfo(path).absoluteFilePath().startsWith(root + QLatin1Char('/'))) {
        arguments.append(QDir::toNativeSeparators(root));
    }
    if (line > 0) {
        arguments << QStringLiteral("--goto") << QStringLiteral("%1:%2").arg(file).arg(line);
    } else {
        arguments.append(file);
    }
    return arguments;
}

bool openInVsCode(const QString& program, const QString& path, int line, const QString& folder) {
    if (program.isEmpty()) {
        return false;
    }
    return QProcess::startDetached(program, vsCodeArguments(path, line, folder));
}

bool hasChooser() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

bool openWithChooser(const QString& path) {
#ifdef Q_OS_WIN
    // OpenAs_RunDLL takes the rest of the command line, unquoted, as the
    // file.
    QProcess process;
    process.setProgram(QStringLiteral("rundll32.exe"));
    process.setNativeArguments(QStringLiteral("shell32.dll,OpenAs_RunDLL ") +
                               QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    return process.startDetached();
#else
    Q_UNUSED(path);
    return false;
#endif
}

bool openWithDefaultApp(const QString& path) {
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()))) {
        return true;
    }
    return openWithChooser(path);
}

}  // namespace tracelab::external
