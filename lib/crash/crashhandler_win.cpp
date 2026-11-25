// Native crash capture on Windows: an unhandled-exception filter that writes
// "minidump.dmp" (MiniDumpWriteDump) and a symbolised "stack.txt".
//
// THE RULE (the Windows cousin of async-signal-safety): the handler runs in a
// process that is already broken. The heap may be corrupt and any other
// thread may hold any lock, so the handler must not use Qt, malloc/new, CRT
// stdio, iostreams or locks of ours. Everything it needs -- file paths,
// dbghelp's symbol handler, scratch buffers -- is prepared in install(); the
// handler only calls Win32/dbghelp on static buffers and formats text by
// hand. dbghelp itself allocates internally, which we cannot avoid; that is
// why the minidump is written FIRST: if the symbolised walk then fails, the
// dump already holds the whole story.
//
// Paths that never reach the unhandled-exception filter (abort(), pure
// virtual calls, CRT invalid-parameter checks, std::terminate) capture their
// own CONTEXT and call the same writer before ending the process.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>

#include "crash/crashhandler.h"

#include <QDir>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <stdlib.h>  // _set_purecall_handler, _set_invalid_parameter_handler

namespace traceview::CrashHandler {

namespace {

constexpr int kPathCapacity = 1024;
constexpr int kMaxFrames = 64;

// Our own exception codes for the paths that are not SEH exceptions. The
// 0xE... range is the customer-defined error range; "TV" = 0x5456.
constexpr DWORD kCodeAbort = 0xE5456001;
constexpr DWORD kCodePureCall = 0xE5456002;
constexpr DWORD kCodeInvalidParameter = 0xE5456003;
constexpr DWORD kCodeTerminate = 0xE5456004;
constexpr DWORD kCodeCppException = 0xE06D7363;  // MSVC "throw"

// ---- prepared in install() ----------------------------------------------
wchar_t g_dumpPath[kPathCapacity];
wchar_t g_stackPath[kPathCapacity];
bool g_pathsReady = false;
bool g_symbolsReady = false;
bool g_handlersInstalled = false;

// ---- handler scratch (static: the handler must not allocate) ------------
volatile LONG g_ownerThread = 0;  // thread writing the report; 0 = nobody yet
CONTEXT g_walkContext;            // StackWalk64 modifies the context it walks
CONTEXT g_capturedContext;
EXCEPTION_RECORD g_capturedRecord;
EXCEPTION_POINTERS g_capturedPointers;
alignas(SYMBOL_INFO) char g_symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
IMAGEHLP_LINE64 g_line;
IMAGEHLP_MODULE64 g_module;

// Minimal text writer on a fixed buffer: no CRT formatting, no allocation.
class Out {
public:
    explicit Out(HANDLE file) : m_file(file) {}
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

    Out& hex(DWORD64 value, int minDigits = 1) {
        char digits[16];
        int n = 0;
        do {
            digits[n++] = "0123456789abcdef"[value & 0xF];
            value >>= 4;
        } while (value != 0 && n < 16);
        while (n < minDigits && n < 16)
            digits[n++] = '0';
        str("0x");
        while (n > 0)
            put(digits[--n]);
        return *this;
    }

    Out& dec(DWORD64 value) {
        char digits[20];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0 && n < 20);
        while (n > 0)
            put(digits[--n]);
        return *this;
    }

    void flush() {
        if (m_len > 0 && m_file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(m_file, m_buffer, m_len, &written, nullptr);
        }
        m_len = 0;
    }

private:
    void put(char c) {
        if (m_len == sizeof(m_buffer))
            flush();
        m_buffer[m_len++] = c;
    }

    HANDLE m_file;
    char m_buffer[512];
    DWORD m_len = 0;
};

const char* exceptionName(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
    case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
    case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_INVALID_OPERATION: return "EXCEPTION_FLT_INVALID_OPERATION";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT: return "EXCEPTION_DATATYPE_MISALIGNMENT";
    case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
    case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
    case 0xC0000374: return "STATUS_HEAP_CORRUPTION";
    case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN";
    case kCodeCppException: return "uncaught C++ exception";
    case kCodeAbort: return "abort() called";
    case kCodePureCall: return "pure virtual function call";
    case kCodeInvalidParameter: return "CRT invalid parameter";
    case kCodeTerminate: return "std::terminate (uncaught C++ exception or noexcept violation)";
    default: return "unknown exception";
    }
}

DWORD64 programCounter(const CONTEXT& context) {
#if defined(_M_X64) || defined(__x86_64__)
    return context.Rip;
#elif defined(_M_ARM64) || defined(__aarch64__)
    return context.Pc;
#elif defined(_M_IX86) || defined(__i386__)
    return context.Eip;
#else
#error "crashhandler_win.cpp: unsupported CPU architecture"
#endif
}

// "module+0xoffset  symbol + 0xdisp  [file:line]" for one address, whatever
// dbghelp can resolve.
void writeAddress(Out& out, DWORD64 address) {
    out.hex(address, 16);
    if (!g_symbolsReady)
        return;
    const HANDLE process = GetCurrentProcess();

    ZeroMemory(&g_module, sizeof(g_module));
    g_module.SizeOfStruct = sizeof(g_module);
    if (SymGetModuleInfo64(process, address, &g_module)) {
        out.str("  ").str(g_module.ModuleName).str("+").hex(address - g_module.BaseOfImage);
    } else {
        const DWORD64 base = SymGetModuleBase64(process, address);
        if (base != 0)
            out.str("  ?+").hex(address - base);
    }

    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(g_symbolBuffer);
    ZeroMemory(g_symbolBuffer, sizeof(g_symbolBuffer));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 symbolDisplacement = 0;
    if (SymFromAddr(process, address, &symbolDisplacement, symbol)) {
        out.str("  ").str(symbol->Name).str(" + ").hex(symbolDisplacement);
        ZeroMemory(&g_line, sizeof(g_line));
        g_line.SizeOfStruct = sizeof(g_line);
        DWORD lineDisplacement = 0;
        if (SymGetLineFromAddr64(process, address, &lineDisplacement, &g_line))
            out.str("  [").str(g_line.FileName).str(":").dec(g_line.LineNumber).str("]");
    }
}

void writeFrames(Out& out, const CONTEXT& context) {
    g_walkContext = context;
    STACKFRAME64 frame;
    ZeroMemory(&frame, sizeof(frame));
    DWORD machine = 0;
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = g_walkContext.Rip;
    frame.AddrStack.Offset = g_walkContext.Rsp;
    frame.AddrFrame.Offset = g_walkContext.Rbp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = g_walkContext.Pc;
    frame.AddrStack.Offset = g_walkContext.Sp;
    frame.AddrFrame.Offset = g_walkContext.Fp;
#elif defined(_M_IX86) || defined(__i386__)
    machine = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = g_walkContext.Eip;
    frame.AddrStack.Offset = g_walkContext.Esp;
    frame.AddrFrame.Offset = g_walkContext.Ebp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;

    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    for (int i = 0; i < kMaxFrames; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, &g_walkContext, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
            break;
        if (frame.AddrPC.Offset == 0)
            break;
        out.str("#").dec(static_cast<DWORD64>(i)).str(i < 10 ? "   " : "  ");
        writeAddress(out, frame.AddrPC.Offset);
        out.str("\r\n");
    }
}

void writeDump(EXCEPTION_POINTERS* pointers) {
    const HANDLE file = CreateFileW(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    MINIDUMP_EXCEPTION_INFORMATION info;
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = pointers;
    info.ClientPointers = FALSE;
    // Thread info + memory the stacks point at: enough for locals in a
    // debugger, still a few MB at most.
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo
                                                 | MiniDumpWithIndirectlyReferencedMemory);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                      pointers != nullptr ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

void writeStack(EXCEPTION_POINTERS* pointers, const char* reason) {
    const HANDLE file = CreateFileW(g_stackPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    {
        Out out(file);
        // Modules loaded after install() (Qt plugins, drivers) are unknown to
        // dbghelp until refreshed; without this the walk stops at them.
        if (g_symbolsReady)
            SymRefreshModuleList(GetCurrentProcess());

        out.str("TraceView crash report\r\n");
        out.str("Reason: ").str(reason).str("\r\n");
        out.str("Thread: ").dec(GetCurrentThreadId()).str("\r\n");
        const EXCEPTION_RECORD* record = pointers != nullptr ? pointers->ExceptionRecord : nullptr;
        if (record != nullptr) {
            out.str("Exception: ").hex(record->ExceptionCode, 8).str(" ")
                .str(exceptionName(record->ExceptionCode)).str("\r\n");
            out.str("Address: ");
            writeAddress(out, static_cast<DWORD64>(
                                  reinterpret_cast<ULONG_PTR>(record->ExceptionAddress)));
            out.str("\r\n");
            if ((record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                 || record->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
                && record->NumberParameters >= 2) {
                const ULONG_PTR kind = record->ExceptionInformation[0];
                out.str("Access: ")
                    .str(kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "?")
                    .str(" at ").hex(record->ExceptionInformation[1], 16).str("\r\n");
            }
        }
        if (!g_symbolsReady)
            out.str("(dbghelp symbol handler unavailable: addresses only)\r\n");
        out.str("\r\nStack:\r\n");
        if (pointers != nullptr && pointers->ContextRecord != nullptr)
            writeFrames(out, *pointers->ContextRecord);
    }
    CloseHandle(file);
}

// True when this thread now owns the report. A crash inside the handler
// (same thread) gets false at once. A second thread crashing while the first
// one writes parks here: the process ends when the first one is done.
bool enterReport() {
    const LONG self = static_cast<LONG>(GetCurrentThreadId());
    const LONG previous = InterlockedCompareExchange(&g_ownerThread, self, 0);
    if (previous == 0)
        return g_pathsReady;
    if (previous == self)
        return false;
    Sleep(INFINITE);
    return false;
}

void writeReport(EXCEPTION_POINTERS* pointers, const char* reason) {
    writeDump(pointers);
    writeStack(pointers, reason);
}

// For the non-SEH paths: build EXCEPTION_POINTERS from the current context.
// The walk then starts in this function, a couple of frames above the
// caller that went wrong.
void reportFromHere(DWORD code, const char* reason) {
    if (!enterReport())
        return;
    RtlCaptureContext(&g_capturedContext);
    ZeroMemory(&g_capturedRecord, sizeof(g_capturedRecord));
    g_capturedRecord.ExceptionCode = code;
    g_capturedRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    g_capturedRecord.ExceptionAddress =
        reinterpret_cast<PVOID>(static_cast<ULONG_PTR>(programCounter(g_capturedContext)));
    g_capturedPointers.ExceptionRecord = &g_capturedRecord;
    g_capturedPointers.ContextRecord = &g_capturedContext;
    writeReport(&g_capturedPointers, reason);
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* pointers) {
    if (enterReport())
        writeReport(pointers, "unhandled exception");
    // Let Windows Error Reporting (and an attached JIT debugger) carry on.
    return EXCEPTION_CONTINUE_SEARCH;
}

// The CRT resets SIGABRT to SIG_DFL before calling this; when it returns,
// abort() goes on to end the process as it normally would.
void abortSignalHandler(int) {
    reportFromHere(kCodeAbort, "abort() called");
}

[[noreturn]] void terminateHandler() {
    reportFromHere(kCodeTerminate, "std::terminate");
    std::abort();  // SIGABRT handler sees the report is already ours
}

#if defined(_MSC_VER)
void pureCallHandler() {
    reportFromHere(kCodePureCall, "pure virtual function call");
    std::abort();
}

void invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                             uintptr_t) {
    // Returning would make the CRT function fail with EINVAL and carry on;
    // the default behaviour is to end the process, so keep it fatal.
    reportFromHere(kCodeInvalidParameter, "CRT invalid parameter");
    std::abort();
}
#endif

bool copyPath(const QString& path, wchar_t* out) {
    if (path.size() >= kPathCapacity)
        return false;
    const int length = path.toWCharArray(out);
    out[length] = L'\0';
    return true;
}

void initSymbols() {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES
                  | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    // Search the executable's folder for PDBs too (a release zip ships them
    // side by side), on top of _NT_SYMBOL_PATH and the PDB path in the image.
    wchar_t exeDir[kPathCapacity] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exeDir, kPathCapacity);
    const wchar_t* searchPath = nullptr;
    if (length > 0 && length < kPathCapacity) {
        for (DWORD i = length; i > 0; --i) {
            if (exeDir[i - 1] == L'\\' || exeDir[i - 1] == L'/') {
                exeDir[i - 1] = L'\0';
                searchPath = exeDir;
                break;
            }
        }
    }
    g_symbolsReady = SymInitializeW(GetCurrentProcess(), searchPath, TRUE) != FALSE;
}

}  // namespace

void install(const QString& crashDirectory) {
    if (crashDirectory.isEmpty())
        return;
    QDir().mkpath(crashDirectory);
    const QString base = QDir::toNativeSeparators(QDir(crashDirectory).absolutePath());
    if (!copyPath(base + QLatin1Char('\\') + dumpFileName(), g_dumpPath)
        || !copyPath(base + QLatin1Char('\\') + stackFileName(), g_stackPath))
        return;
    g_pathsReady = true;

    if (g_handlersInstalled)
        return;
    g_handlersInstalled = true;

    // dbghelp is linked at load time (CMake links dbghelp), so the handler
    // never needs LoadLibrary; the symbol handler is initialised here.
    initSymbols();

    // Leave room on this thread's stack for the handler after a stack
    // overflow (only affects the thread calling install(): the GUI thread).
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);

    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    std::signal(SIGABRT, abortSignalHandler);
    std::set_terminate(terminateHandler);
#if defined(_MSC_VER)
    _set_purecall_handler(pureCallHandler);
    _set_invalid_parameter_handler(invalidParameterHandler);
#endif
}

bool canCaptureStack() { return true; }

void forceCrash() {
    volatile int* null = nullptr;
    *null = 0;
    std::abort();
}

}  // namespace traceview::CrashHandler
