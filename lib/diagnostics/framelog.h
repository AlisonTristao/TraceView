#pragma once

#include <QObject>
#include <QVector>

#include "diagnostics/framelogentry.h"
#include "protocol/btpframe.h"

namespace traceview {

// App-wide capture buffer for the BTP traffic monitor. MainWindow owns one
// instance and feeds it from every device connection's BtpBackend
// (frameObserved / frameDecodeFailed), tagging each entry with that device.
// BtpMonitorTab reads it and renders the table.
//
// A bounded ring (kCapacity, oldest dropped first): high-rate TELEMETRY can
// fill this quickly and the monitor is a live inspector, not a session
// recorder -- Export writes the current buffer out on demand. `seq` numbers
// keep climbing across evictions so a row's identity is stable even after the
// entry behind it scrolls out.
//
// Nothing here filters: TELEMETRY frames are captured like any other and
// BtpMonitorTab hides them with a view toggle. "Paused" is also a view-level
// concept (the tab stops appending rows); the buffer keeps filling so
// unpausing shows what was missed.
class FrameLog : public QObject {
    Q_OBJECT

public:
    static constexpr int kCapacity = 2000;

    explicit FrameLog(QObject* parent = nullptr);

    // Oldest-first, at most the configured capacity entries.
    const QVector<FrameLogEntry>& entries() const {
        return m_entries;
    }

    // Records one observed frame. `wallClock` is stamped here.
    void recordFrame(FrameDirection direction, const QString& deviceId, const QString& deviceName,
                     const BtpFrame& frame);
    // Records one decode/reassembly failure reported for `deviceId`.
    void recordDecodeError(const QString& deviceId, const QString& deviceName,
                           const QString& reason);

    void clear();

signals:
    // `entry` is the one just appended; `droppedOldest` true when it evicted
    // the front entry (kCapacity reached).
    void entryAdded(const traceview::FrameLogEntry& entry, bool droppedOldest);
    void cleared();

private:
    void append(FrameLogEntry entry);

    QVector<FrameLogEntry> m_entries;
    quint64 m_nextSeq = 1;
    int m_capacity = kCapacity;
};

}  // namespace traceview
