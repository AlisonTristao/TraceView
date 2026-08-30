#pragma once

#include <QMetaType>

namespace traceview {

// Severity of a one-off status message (Backend::statusMessage and the
// sibling signals ClockSync/CommandClient/HubBinder emit and BtpBackend
// forwards). Purely a presentation hint: it does not change what is sent on
// the wire, only how MainWindow colours the toast and how the Notification
// History window (lib/diagnostics/notificationhistorywindow.h) groups and
// filters the entry it keeps.
//
// Info is the default the signals fall back to, so every existing
// `emit statusMessage(text, ms)` keeps compiling and keeps meaning what it
// meant; a call site says Success/Warning/Error only where it is obviously
// one of those (a handshake failing, a SUBSCRIBE rejected, a session coming
// up). The classification is made at the emit site on purpose -- inferring
// it from the message text would break the moment the text is translated.
enum class StatusSeverity { Info, Success, Warning, Error };

inline const char* statusSeverityKey(StatusSeverity severity) {
    switch (severity) {
        case StatusSeverity::Info:
            return "info";
        case StatusSeverity::Success:
            return "success";
        case StatusSeverity::Warning:
            return "warning";
        case StatusSeverity::Error:
            return "error";
    }
    return "info";
}

}  // namespace traceview

Q_DECLARE_METATYPE(traceview::StatusSeverity)
