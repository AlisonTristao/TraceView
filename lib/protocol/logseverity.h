#pragma once

#include <QtGlobal>

namespace traceview {

// Mirrors bally_OS's lib/Logger/LogTypes.h `logType` enum -- the LOG
// channel's object_id carries this severity, but unlike Telemetry there is
// no BTP manifest/schema for it (see TelemetryCatalog/ManifestClient), so
// this is a small, hand-kept copy rather than anything discovered on the
// wire.
enum class LogSeverity : quint8 {
    None = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Debug = 4,
    Command = 5,
};

inline const char* logSeverityToString(LogSeverity severity) {
    switch (severity) {
        case LogSeverity::Info: return "INFO";
        case LogSeverity::Warn: return "WARN";
        case LogSeverity::Error: return "ERRO";
        case LogSeverity::Debug: return "DEBG";
        case LogSeverity::Command: return "CMDO";
        case LogSeverity::None: return "NONE";
    }
    return "UNKN";
}

}  // namespace traceview
