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
