#pragma once

#include <QString>
#include <QVector>

#include "protocol/logentry.h"

namespace traceview {

// Reads a bally_OS ".blog" file: a headerless, back-to-back sequence of BTP
// v1 MessageType::Log frames -- no manifest, no file-level header at all
// (see bally_OS/lib/Logger/Logger.cpp flush_to_sd()/flush_logs_to()). Frames
// were fragmented against the EspNow transport profile when flushed
// (Logger.cpp slices records into btp::kEspNowMaxPayloadSize chunks, the
// same limit the live ESP-NOW send path uses), so this reader decodes with
// that same profile.
//
// Reassembly here is a plain sequential accumulator, not btp::Reassembler:
// the firmware always flushes one logical record's fragments back-to-back
// and in order (see flush_logs_to's per-record loop), so a file never needs
// Reassembler's out-of-order/timeout handling -- a fragment_index that
// doesn't extend the record currently being accumulated just ends it.
//
// Pure parser, no UI -- same "store" shape as ProjectStore (load() + a
// result + lastError()), so MainWindow/LogViewer can surface failures with a
// QMessageBox the same way onOpenProject() does.
class LogFileReader {
public:
    // Parses the whole file into entries(). Returns false only if the file
    // itself could not be opened -- a malformed or truncated frame partway
    // through is skipped (counted via skippedFrameCount(), not fatal) and
    // parsing continues from the next frame boundary, so a still-being-
    // written .blog can be opened mid-flush without losing the entries
    // already durable on disk.
    bool load(const QString& filePath);

    QVector<LogEntry> entries() const { return m_entries; }
    QString lastError() const { return m_lastError; }
    // Frames that failed to decode (bad magic/version/CRC/etc.) or a
    // fragment that didn't extend the record being accumulated -- surfaced
    // as a count rather than failing load() outright.
    int skippedFrameCount() const { return m_skippedFrameCount; }

private:
    QVector<LogEntry> m_entries;
    QString m_lastError;
    int m_skippedFrameCount = 0;
};

}  // namespace traceview
