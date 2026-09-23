#include "updater/updateinstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QProcess>

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#elif defined(Q_OS_LINUX)
#include <QSaveFile>
#include <QProcessEnvironment>
#endif

namespace traceview {

#if defined(TRACEVIEW_FLATPAK_BUILD)

bool UpdateInstaller::install(const QString&, QString* reason) {
    if (reason) {
        *reason = QObject::tr("Updates are managed by Flatpak. Use 'flatpak update'.");
    }
    return false;
}

#elif defined(Q_OS_ANDROID)

// An app can't replace its own APK: this hands the verified file to the
// system package installer (ACTION_VIEW on a FileProvider content:// URI --
// Android 7+ rejects bare file:// URIs across apps). Qt's own
// ${applicationId}.qtprovider FileProvider serves it, which is why
// UpdateDownloader saves under files/ on Android. The installer takes it
// from there: it asks for the "install unknown apps" permission the first
// time (REQUEST_INSTALL_PACKAGES in android/AndroidManifest.xml), then
// kills this process itself when it replaces the package.
bool UpdateInstaller::install(const QString& downloadedFilePath, QString* reason) {
    const auto fail = [reason](const QString& message) {
        if (reason) *reason = message;
        return false;
    };
    if (!QFileInfo::exists(downloadedFilePath)) {
        return fail(QObject::tr("Downloaded installer not found: %1").arg(downloadedFilePath));
    }

    const QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return fail(QObject::tr("Couldn't start the Android package installer."));
    }
    QJniEnvironment env;

    const QString packageName =
        context.callObjectMethod("getPackageName", "()Ljava/lang/String;").toString();
    const QJniObject authority = QJniObject::fromString(packageName + QStringLiteral(".qtprovider"));
    const QJniObject path = QJniObject::fromString(downloadedFilePath);
    const QJniObject file("java/io/File", "(Ljava/lang/String;)V", path.object<jstring>());
    const QJniObject uri = QJniObject::callStaticObjectMethod(
        "androidx/core/content/FileProvider", "getUriForFile",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/io/File;)Landroid/net/Uri;",
        context.object(), authority.object<jstring>(), file.object());
    if (env.checkAndClearExceptions() || !uri.isValid()) {
        return fail(QObject::tr("Couldn't share the downloaded APK with the package installer."));
    }

    const QJniObject action = QJniObject::fromString(QStringLiteral("android.intent.action.VIEW"));
    const QJniObject mimeType =
        QJniObject::fromString(QStringLiteral("application/vnd.android.package-archive"));
    QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    intent.callObjectMethod("setDataAndType",
                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/content/Intent;",
                            uri.object(), mimeType.object<jstring>());
    constexpr jint kFlagGrantReadUriPermission = 0x00000001;
    constexpr jint kFlagActivityNewTask = 0x10000000;
    intent.callObjectMethod("addFlags", "(I)Landroid/content/Intent;",
                            kFlagGrantReadUriPermission | kFlagActivityNewTask);
    context.callMethod<void>("startActivity", "(Landroid/content/Intent;)V", intent.object());
    if (env.checkAndClearExceptions()) {
        return fail(QObject::tr("Couldn't start the Android package installer."));
    }
    return true;
}

#elif defined(Q_OS_WIN)

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

bool UpdateInstaller::install(const QString& downloadedFilePath, QString* reason) {
    const auto fail = [reason](const QString& message) {
        if (reason) *reason = message;
        return false;
    };
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QFileInfo imageInfo(appImage);
    if (appImage.isEmpty() || !imageInfo.isAbsolute() || !imageInfo.isFile()) {
        return fail(QObject::tr("Automatic installation requires running an AppImage. "
                                "Download the AppImage from the release page."));
    }
    // Resolve symlinks so an update replaces the actual image, not its launcher link.
    const QString target = imageInfo.canonicalFilePath();
    const QFileInfo targetInfo(target);
    if (!targetInfo.isWritable() || !QFileInfo(targetInfo.absolutePath()).isWritable()) {
        return fail(QObject::tr("The AppImage and its directory must be writable. "
                                "Download the update and replace it manually."));
    }
    QFile source(downloadedFilePath);
    if (QFileInfo(downloadedFilePath).canonicalFilePath() == target ||
        !source.open(QIODevice::ReadOnly)) {
        return fail(QObject::tr("Couldn't open the downloaded AppImage."));
    }
    // Type-2 AppImage: ELF magic followed by AI\x02 at offset 8.
    const QByteArray header = source.peek(11);
    if (header.size() != 11 || header.left(4) != QByteArray::fromHex("7f454c46") ||
        header.mid(8, 3) != QByteArray::fromHex("414902")) {
        return fail(QObject::tr("The downloaded file is not a supported AppImage."));
    }
    // QSaveFile stages on the same filesystem, then atomically renames. Never
    // fall back to truncating the running image. Linux keeps its mounted inode
    // alive until this process exits, even after its pathname is replaced.
    QSaveFile replacement(target);
    replacement.setDirectWriteFallback(false);
    if (!replacement.open(QIODevice::WriteOnly)) return fail(replacement.errorString());
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError)
            return fail(source.errorString());
        if (replacement.write(chunk) != chunk.size()) return fail(replacement.errorString());
    }
    if (!replacement.setPermissions(targetInfo.permissions() | QFileDevice::ExeOwner))
        return fail(replacement.errorString());
    if (!replacement.commit()) return fail(replacement.errorString());

    // Pass paths as arguments, never as shell source. Drop paths into the old
    // AppImage mount; the new AppRun will configure its own Qt/library paths.
    QProcess launcher;
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto* key : {"APPIMAGE", "APPDIR", "ARGV0", "OWD", "LD_LIBRARY_PATH",
                            "LD_PRELOAD", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH",
                            "QML2_IMPORT_PATH", "QML_IMPORT_PATH"}) {
        environment.remove(QString::fromLatin1(key));
    }
    launcher.setProcessEnvironment(environment);
    launcher.setWorkingDirectory(targetInfo.absolutePath());
    launcher.setProgram(QStringLiteral("/bin/sh"));
    launcher.setArguments({QStringLiteral("-c"),
        QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.3; done; exec \"$2\""),
        QStringLiteral("traceview-relaunch"), QString::number(QCoreApplication::applicationPid()),
        target});
    if (!launcher.startDetached()) {
        return fail(QObject::tr("The update was installed, but relaunch failed. "
                                "Close TraceView and open the AppImage again."));
    }
    return true;
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
