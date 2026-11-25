// Native crash capture on Linux and macOS: sigaction handlers for the fatal
// signals plus std::set_terminate, writing "stack.txt" with backtrace().
//
// ASYNC-SIGNAL-SAFETY RULE: a signal handler can interrupt any code, including
// malloc() halfway through or a thread holding a mutex. Inside the handler we
// may only call async-signal-safe functions (open, write, close, raise,
// signal, sigaction, getpid...): no Qt, no malloc/new, no printf/iostreams,
// no locks. So everything is prepared in install(): the file path sits in a
// static buffer (the file itself is only created by the handler, so a clean
// run leaves no empty stack.txt behind), the alternate signal stack is a
// static array, and backtrace() is called once so glibc loads libgcc's
// unwinder now instead of dlopen()-ing it (and mallocing) mid-crash.
// backtrace()/backtrace_symbols_fd() are not formally on the POSIX list but
// are the standard practice once warmed up; backtrace_symbols_fd() was made
// for this (it writes straight to the fd, no malloc).

#include "crash/crashhandler.h"

#include <QByteArray>
#include <QDir>
#include <QFile>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace traceview::CrashHandler {

namespace {

constexpr int kPathCapacity = 4096;
constexpr int kMaxFrames = 128;
constexpr std::size_t kAltStackSize = 64 * 1024;
constexpr int kSignals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};

// ---- prepared in install() ----------------------------------------------
char g_stackPath[kPathCapacity];
bool g_pathReady = false;
bool g_handlersInstalled = false;
alignas(16) char g_altStack[kAltStackSize];

// ---- handler scratch ----------------------------------------------------
// atomic_flag is guaranteed lock-free, so test_and_set() is safe in a
// handler and also stops a second crashing thread.
std::atomic_flag g_entered = ATOMIC_FLAG_INIT;
void* g_frames[kMaxFrames];

// Minimal text writer on a fixed buffer: write(2) only.
class Out {
public:
    explicit Out(int fd) : m_fd(fd) {}
    ~Out() { flush(); }
    Out(const Out&) = delete;
    Out& operator=(const Out&) = delete;

    Out& str(const char* s) {
        if (s == nullptr)
            s = "(null)";
        while (*s != '\0')
            put(*s++);
        return *this;
    }

    Out& hex(std::uintptr_t value, int minDigits = 1) {
        char digits[2 * sizeof(std::uintptr_t)];
        int n = 0;
        const int capacity = static_cast<int>(sizeof(digits));
        do {
            digits[n++] = "0123456789abcdef"[value & 0xF];
            value >>= 4;
        } while (value != 0 && n < capacity);
        while (n < minDigits && n < capacity)
            digits[n++] = '0';
        str("0x");
        while (n > 0)
            put(digits[--n]);
        return *this;
    }

    Out& dec(long long value) {
        unsigned long long magnitude = static_cast<unsigned long long>(value);
        if (value < 0) {
            put('-');
            magnitude = 0ULL - magnitude;
        }
        char digits[20];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + magnitude % 10);
            magnitude /= 10;
        } while (magnitude != 0 && n < 20);
        while (n > 0)
            put(digits[--n]);
        return *this;
    }

    void flush() {
        const char* data = m_buffer;
        std::size_t left = m_len;
        while (left > 0) {
            const ssize_t written = ::write(m_fd, data, left);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0)
                break;
            data += written;
            left -= static_cast<std::size_t>(written);
        }
        m_len = 0;
    }

private:
    void put(char c) {
        if (m_len == sizeof(m_buffer))
            flush();
        m_buffer[m_len++] = c;
    }

    int m_fd;
    char m_buffer[512];
    std::size_t m_len = 0;
};

const char* signalName(int sig) {
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGBUS: return "SIGBUS";
    default: return "signal";
    }
}

const char* signalCodeName(int sig, int code) {
    if (sig == SIGSEGV) {
        if (code == SEGV_MAPERR) return "address not mapped";
        if (code == SEGV_ACCERR) return "invalid permissions";
    } else if (sig == SIGBUS) {
        if (code == BUS_ADRALN) return "invalid address alignment";
        if (code == BUS_ADRERR) return "nonexistent physical address";
    } else if (sig == SIGFPE) {
        if (code == FPE_INTDIV) return "integer divide by zero";
        if (code == FPE_FLTDIV) return "floating-point divide by zero";
    } else if (sig == SIGILL) {
        if (code == ILL_ILLOPC) return "illegal opcode";
    }
    return nullptr;
}

int openStackFile() {
    if (!g_pathReady)
        return -1;
    int fd;
    do {
        fd = ::open(g_stackPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    } while (fd < 0 && errno == EINTR);
    return fd;
}

void writeBacktrace(Out& out, int fd) {
    out.str("\nStack:\n");
    out.flush();  // backtrace_symbols_fd writes to the fd directly
    const int count = ::backtrace(g_frames, kMaxFrames);
    if (count > 0)
        ::backtrace_symbols_fd(g_frames, count, fd);
}

void signalHandler(int sig, siginfo_t* info, void*) {
    const int savedErrno = errno;
    if (!g_entered.test_and_set()) {
        const int fd = openStackFile();
        if (fd >= 0) {
            {
                Out out(fd);
                out.str("TraceView crash report\n");
                out.str("Reason: fatal signal\n");
                out.str("Signal: ").dec(sig).str(" (").str(signalName(sig)).str(")\n");
                out.str("Pid: ").dec(static_cast<long long>(::getpid())).str("\n");
                if (info != nullptr && sig != SIGABRT) {
                    out.str("Code: ").dec(info->si_code);
                    if (const char* name = signalCodeName(sig, info->si_code))
                        out.str(" (").str(name).str(")");
                    out.str("\n");
                    out.str("Fault address: ")
                        .hex(reinterpret_cast<std::uintptr_t>(info->si_addr),
                             static_cast<int>(2 * sizeof(void*)))
                        .str("\n");
                }
                writeBacktrace(out, fd);
            }
            ::close(fd);
        }
    }
    // SA_RESETHAND already put the default action back. The signal is
    // blocked while we run, so raise() leaves it pending and it is delivered
    // -- and kills the process with the right status/core dump -- as soon as
    // the handler returns. For a hardware fault, returning would re-run the
    // faulting instruction anyway; for SIGABRT, abort() finishes the job.
    // The explicit signal() covers a second, different signal arriving while
    // the first report is being written.
    ::signal(sig, SIG_DFL);
    ::raise(sig);
    errno = savedErrno;
}

[[noreturn]] void terminateHandler() {
    if (!g_entered.test_and_set()) {
        const int fd = openStackFile();
        if (fd >= 0) {
            {
                Out out(fd);
                out.str("TraceView crash report\n");
                out.str("Reason: std::terminate (uncaught C++ exception or noexcept violation)\n");
                out.str("Pid: ").dec(static_cast<long long>(::getpid())).str("\n");
                writeBacktrace(out, fd);
            }
            ::close(fd);
        }
    }
    std::abort();  // SIGABRT handler sees g_entered and just lets it die
}

}  // namespace

void install(const QString& crashDirectory) {
    if (crashDirectory.isEmpty())
        return;
    QDir().mkpath(crashDirectory);
    const QByteArray path =
        QFile::encodeName(QDir(crashDirectory).absoluteFilePath(stackFileName()));
    if (path.size() >= kPathCapacity)
        return;
    std::memcpy(g_stackPath, path.constData(), static_cast<std::size_t>(path.size()));
    g_stackPath[path.size()] = '\0';
    g_pathReady = true;

    if (g_handlersInstalled)
        return;
    g_handlersInstalled = true;

    // Warm-up: the first backtrace() on glibc dlopen()s libgcc_s (malloc).
    void* warmup[4];
    ::backtrace(warmup, 4);

    // Alternate stack so a stack overflow can still run the handler. Note
    // sigaltstack is per thread: this covers the thread calling install()
    // (the GUI thread); other threads still get the handler, on their own
    // stack, for every crash except a stack overflow.
    stack_t altStack{};
    altStack.ss_sp = g_altStack;
    altStack.ss_size = sizeof(g_altStack);
    altStack.ss_flags = 0;
    ::sigaltstack(&altStack, nullptr);

    struct sigaction action {};
    action.sa_sigaction = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (const int sig : kSignals)
        ::sigaction(sig, &action, nullptr);

    std::set_terminate(terminateHandler);
}

bool canCaptureStack() { return true; }

void forceCrash() {
    volatile int* null = nullptr;
    *null = 0;
    std::abort();
}

}  // namespace traceview::CrashHandler
