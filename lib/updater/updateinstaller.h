#pragma once

#include <QString>

namespace traceview {

// Launches a downloaded update and hands off control to it. Both platforms
// end the same way: on success the caller quits (QCoreApplication::quit())
// and something else finishes the swap and brings TraceView back.
//
// Windows: the downloaded NSIS .exe is simply run, not silently -- the user
// already confirmed "Update Now" in UpdateAvailableDialog, so the installer
// wizard appearing once more is expected, not automation running behind
// their back. CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL (see CMakeLists.txt)
// means it uninstalls the current version itself before installing the new
// one; nothing here duplicates that.
//
// Linux: there is no installer, just a .tar.gz -- install() extracts it with
// the system `tar` binary, writes a small shell script that waits for this
// process to exit, copies the extracted tree over the current install
// directory, and relaunches TraceView, then launches that script detached.
// This only works if the install directory is writable by the current user
// (true for anything extracted under $HOME); see the .cpp for the fallback
// when it isn't.
class UpdateInstaller {
public:
    // Returns false (and leaves the app running) if the update couldn't even
    // be started -- e.g. the downloaded file is missing, or (Linux only) the
    // extraction/permission check failed. `reason` is set to a human-readable
    // explanation in that case. On true, the caller must quit immediately:
    // this may already have started tearing down the current install.
    static bool install(const QString& downloadedFilePath, QString* reason);
};

}  // namespace traceview
