#include "updater/updateinstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>

#if defined(Q_OS_LINUX)
#include <QTemporaryDir>
#endif

namespace traceview {

#if defined(Q_OS_WIN)

namespace {

// Waits for `pid` (this process, already about to quit) to exit, runs the
// NSIS installer completely silently (/S -- CPACK_NSIS_ENABLE_UNINSTALL_
// BEFORE_INSTALL means it uninstalls the current version itself first, also
// silently), and relaunches TraceView from the same path it was already
// running from. /S skips the directory-picker page entirely, so an
// upgrade-in-place always lands back in whatever directory the previous
// install already registered -- the same one this running copy's own
// applicationFilePath() points at.
//
// One prompt is unavoidable here: the installer's RequestExecutionLevel is
// admin (it installs to Program Files), so Windows shows its own UAC
// consent dialog no matter how this process is launched. /S only removes
// the installer's own wizard pages, not that OS-level gate.
QString buildApplyScript(qint64 pid, const QString& installerPath, const QString& exePath) {
    return QStringLiteral(
               "while (Get-Process -Id %1 -ErrorAction SilentlyContinue) { "
               "Start-Sleep -Milliseconds 300 }\n"
               "Start-Process -FilePath '%2' -ArgumentList '/S' -Wait\n"
               "Start-Process -FilePath '%3'\n")
        .arg(pid)
        .arg(QDir::toNativeSeparators(installerPath), QDir::toNativeSeparators(exePath));
}

}  // namespace

bool UpdateInstaller::install(const QString& downloadedFilePath, QString* reason) {
    if (!QFileInfo::exists(downloadedFilePath)) {
        if (reason) {
            *reason = QObject::tr("Downloaded installer not found: %1").arg(downloadedFilePath);
        }
        return false;
    }

    const QString scriptPath =
        QDir::tempPath() + QStringLiteral("/traceview_update_apply.ps1");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (reason) {
            *reason = QObject::tr("Couldn't write the update script.");
        }
        return false;
    }
    script.write(buildApplyScript(QCoreApplication::applicationPid(), downloadedFilePath,
                                   QCoreApplication::applicationFilePath())
                     .toUtf8());
    script.close();

    return QProcess::startDetached(
        QStringLiteral("powershell.exe"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
         QStringLiteral("-File"), scriptPath});
}

#elif defined(Q_OS_LINUX)

namespace {

// Waits for `pid` to exit, replaces `installDir`'s contents with
// `stagingDir`'s, and relaunches `exePath` -- run detached so it survives
// this process quitting. `cp -a ".../." installDir/` copies over the
// existing tree (including dotfiles) rather than removing it first: a
// failure partway (e.g. a permission the writability check below didn't
// catch) leaves whatever files were already copied instead of an empty
// install directory.
QString buildApplyScript(qint64 pid, const QString& stagingDir, const QString& installDir,
                          const QString& exePath) {
    return QStringLiteral(
               "#!/bin/sh\n"
               "while kill -0 %1 2>/dev/null; do sleep 0.3; done\n"
               "if cp -a \"%2/.\" \"%3/\"; then\n"
               "  rm -rf \"%2\"\n"
               "  nohup \"%4\" >/dev/null 2>&1 &\n"
               "fi\n")
        .arg(pid)
        .arg(stagingDir, installDir, exePath);
}

}  // namespace

bool UpdateInstaller::install(const QString& downloadedFilePath, QString* reason) {
    if (!QFileInfo::exists(downloadedFilePath)) {
        if (reason) {
            *reason = QObject::tr("Downloaded archive not found: %1").arg(downloadedFilePath);
        }
        return false;
    }

    // applicationDirPath() is .../bin for a Linux install (see CMakeLists.txt's
    // install(TARGETS ... RUNTIME DESTINATION bin)); its parent is the tree
    // the tarball originally expanded into, wherever the user put it.
    QDir installQDir(QCoreApplication::applicationDirPath());
    installQDir.cdUp();
    const QFileInfo installDirInfo(installQDir.absolutePath());
    if (!installDirInfo.isWritable()) {
        if (reason) {
            *reason = QObject::tr(
                          "%1 is not writable by this user -- download the new version from "
                          "the release page and replace it manually.")
                          .arg(installDirInfo.absoluteFilePath());
        }
        return false;
    }

    // setAutoRemove(false): the apply script deletes this directory itself
    // once the copy succeeds, after this object (and the process it belongs
    // to) is already gone.
    QTemporaryDir stagingDir;
    stagingDir.setAutoRemove(false);
    if (!stagingDir.isValid()) {
        if (reason) {
            *reason = QObject::tr("Couldn't create a temporary directory to extract into.");
        }
        return false;
    }

    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("xzf"), downloadedFilePath, QStringLiteral("-C"),
                                       stagingDir.path()});
    if (!tar.waitForFinished(60000) || tar.exitStatus() != QProcess::NormalExit ||
        tar.exitCode() != 0) {
        if (reason) {
            *reason = QObject::tr("Couldn't extract the downloaded archive.");
        }
        return false;
    }

    // CPack's TGZ generator wraps the tree in one top-level
    // "TraceView-<version>-<platform>" directory (CPACK_PACKAGE_FILE_NAME);
    // find it rather than assuming a fixed name, since that names the
    // version being installed TO, not the one already running.
    const QDir extracted(stagingDir.path());
    const QStringList topLevel = extracted.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    const QString sourceDir =
        topLevel.size() == 1 ? extracted.filePath(topLevel.first()) : extracted.path();

    const QString scriptPath = stagingDir.path() + QStringLiteral("_apply.sh");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly)) {
        if (reason) {
            *reason = QObject::tr("Couldn't write the update script.");
        }
        return false;
    }
    script.write(buildApplyScript(QCoreApplication::applicationPid(), sourceDir,
                                   installDirInfo.absoluteFilePath(),
                                   QCoreApplication::applicationFilePath())
                     .toUtf8());
    script.close();
    script.setPermissions(script.permissions() | QFileDevice::ExeOwner);

    return QProcess::startDetached(QStringLiteral("/bin/sh"), {scriptPath});
}

#else

bool UpdateInstaller::install(const QString&, QString* reason) {
    if (reason) {
        *reason = QObject::tr("Automatic install isn't supported on this platform.");
    }
    return false;
}

#endif

}  // namespace traceview
