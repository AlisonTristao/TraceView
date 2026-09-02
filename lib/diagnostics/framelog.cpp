#include "diagnostics/framelog.h"

#include "preferences/appsettings.h"

namespace traceview {

FrameLog::FrameLog(QObject* parent)
    : QObject(parent), m_capacity(AppSettings::instance().frameLogCapacity()) {}

void FrameLog::append(FrameLogEntry entry) {
    entry.seq = m_nextSeq++;
    entry.wallClock = QDateTime::currentDateTime();
    m_entries.append(entry);

    bool droppedOldest = false;
    if (m_entries.size() > m_capacity) {
        m_entries.removeFirst();
        droppedOldest = true;
    }
    emit entryAdded(m_entries.last(), droppedOldest);
}

void FrameLog::recordFrame(FrameDirection direction, const QString& deviceId,
                           const QString& deviceName, const BtpFrame& frame) {
    FrameLogEntry entry;
    entry.direction = direction;
    entry.deviceId = deviceId;
    entry.deviceName = deviceName;
    entry.frame = frame;
    append(std::move(entry));
}

void FrameLog::recordDecodeError(const QString& deviceId, const QString& deviceName,
                                 const QString& reason) {
    FrameLogEntry entry;
    entry.direction = FrameDirection::Inbound;  // decode failures are always on the read path
    entry.deviceId = deviceId;
    entry.deviceName = deviceName;
    entry.decodeError = true;
    entry.errorText = reason;
    append(std::move(entry));
}

void FrameLog::clear() {
    if (m_entries.isEmpty()) {
        return;
    }
    m_entries.clear();
    emit cleared();
}

}  // namespace traceview
