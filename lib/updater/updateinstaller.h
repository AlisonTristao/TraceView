#pragma once

#include <QString>

namespace traceview {

// Launches a downloaded update and hands off control to it. Both platforms
// end the same way: on success the caller quits (QCoreApplication::quit())
// and something else finishes the swap and brings TraceView back.
//
// Windows: install() writes a small PowerShell script that waits for this
// process to exit, runs the downloaded NSIS .exe fully silently (/S -- no
// wizard pages at all; CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL, see
// CMakeLists.txt, means it uninstalls the current version itself first,
// also silently), and relaunches TraceView from the same path it was
// already running from -- then launches that script detached. The one
// thing this can't make disappear is Windows' own UAC consent prompt: the
// installer's RequestExecutionLevel is admin (it installs to Program
// Files), so that dialog appears regardless of /S.
//
// Linux: copies the verified AppImage to a same-directory temporary file,
// atomically replaces APPIMAGE, and launches a detached waiter to restart it.
// Running outside an AppImage or from a read-only location refuses installation.
//
// Android: the exception to "the caller quits". install() only opens the
// system package installer on the downloaded APK; TraceView keeps running
// until the user confirms there, and Android itself stops it while
// replacing the package. The caller must NOT quit on success.
class UpdateInstaller {
public:
    // Returns false (and leaves the app running) if the update couldn't even
    // be started -- e.g. the downloaded file is missing, or (Linux only) the
    // copy/permission check failed. `reason` is set to a human-readable
    // explanation in that case. On true, the caller must quit immediately
    // (except on Android, see above): this may already have started tearing
    // down the current install.
    static bool install(const QString& downloadedFilePath, QString* reason);
};

}  // namespace traceview
