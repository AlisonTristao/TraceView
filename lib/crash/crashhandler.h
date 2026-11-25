#pragma once

#include <QString>

namespace traceview {

// Native crash capture. One header, one .cpp per platform chosen by CMake:
//   crashhandler_win.cpp   -- SetUnhandledExceptionFilter + MiniDumpWriteDump
//   crashhandler_posix.cpp -- sigaction + backtrace_symbols_fd (Linux/macOS)
//   crashhandler_stub.cpp  -- Android/iOS: does nothing
// Callers never ask which platform they are on, only canCaptureStack().
//
// RULE for the platform files: everything the handler needs (paths, file
// descriptors, buffers) is prepared in install(). Inside the handler itself
// only async-signal-safe calls -- no Qt, no malloc, no locks, no iostreams.
// See CONTRIBUTING.md, "Crash reports".
namespace CrashHandler {

// Arms the handler. Files go to `crashDirectory` (created here, before any
// crash): "stack.txt" always, plus "minidump.dmp" on Windows.
void install(const QString& crashDirectory);

// Whether install() on this platform writes a stack trace at all.
bool canCaptureStack();

// Name of the stack file inside a crash directory.
inline QString stackFileName() { return QStringLiteral("stack.txt"); }
inline QString dumpFileName() { return QStringLiteral("minidump.dmp"); }

// Developer mode "Force crash": dereferences a null pointer.
[[noreturn]] void forceCrash();

}  // namespace CrashHandler

}  // namespace traceview
