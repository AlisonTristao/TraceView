#pragma once

#include <QString>
#include <QtGlobal>

#include "protocol/logseverity.h"

namespace traceview {

// One fully reassembled LOG message read back from a .blog file (see
// LogFileReader) -- fragments already joined into the original UTF-8 text.
// timestampUs is the firmware's own monotonic per-boot microsecond clock
// (BTP_V1.md section 6.3), not wall-clock time: there is no reference point
// in the file to convert it against, so it is kept and displayed exactly as
// recorded.
struct LogEntry {
    quint64 timestampUs = 0;
    quint32 sourceId = 0;
    quint32 bootId = 0;
    quint32 sequence = 0;
    LogSeverity severity = LogSeverity::None;
    QString message;
};

}  // namespace traceview
