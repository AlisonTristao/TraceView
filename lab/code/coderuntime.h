#pragma once

#include <QMutex>
#include <QString>

namespace tracelab::code {

// The folder every compile reads from: tracelab.h and the libc subset, tcc's
// freestanding headers, and two objects built from tcc's runtime sources the
// first time (the helpers compiled code may call: unsigned 64-bit
// conversions, __chkstk/alloca on Windows; and the stub tcc's -bt mode
// links). Written under the application's local data folder, in a
// subfolder named after the contents, so a newer TraceView never reads an
// older copy. Safe from any thread.
class CodeRuntime {
public:
    static CodeRuntime& instance();

    // Prepares the folder on the first call; false, with `error`, when it
    // cannot be written or the runtime does not compile.
    bool ensureReady(QString* error = nullptr);

    // Valid after ensureReady().
    QString directory() const;
    // The helpers object to add to every compile.
    QString helpersObject() const;

private:
    CodeRuntime() = default;

    mutable QMutex m_mutex;
    bool m_ready = false;
    QString m_error;
    QString m_directory;
};

}  // namespace tracelab::code
