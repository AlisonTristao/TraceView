#include "crash/crashhandler.h"

#include <cstdlib>

// Android and iOS: no native stack capture yet. The SessionMarker still
// notices the crash and the dialog still shows the log; it just hides the
// "Technical details" stack section because canCaptureStack() is false.
namespace traceview::CrashHandler {

void install(const QString&) {}

bool canCaptureStack() { return false; }

void forceCrash() {
    volatile int* null = nullptr;
    *null = 0;
    std::abort();
}

}  // namespace traceview::CrashHandler
