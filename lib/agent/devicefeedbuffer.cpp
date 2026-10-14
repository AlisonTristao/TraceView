#include "agent/devicefeedbuffer.h"

namespace traceview {

namespace {

// Drops UTF-8 continuation bytes left at the front by trimming mid-character.
QString decodeTail(const QByteArray& bytes) {
    int start = 0;
    while (start < bytes.size() && (quint8(bytes.at(start)) & 0xC0) == 0x80) {
        ++start;
    }
    return QString::fromUtf8(bytes.constData() + start, bytes.size() - start);
}

}  // namespace

void DeviceFeedBuffer::appendTerminal(const QString& deviceId, const QByteArray& bytes) {
    Terminal& terminal = m_terminals[deviceId];
    terminal.bytes.append(bytes);
    terminal.received += bytes.size();
    if (terminal.bytes.size() > kTerminalCapacityBytes) {
        terminal.bytes.remove(0, terminal.bytes.size() - kTerminalCapacityBytes);
    }
}

void DeviceFeedBuffer::appendLog(const QString& deviceId, const LogLine& line) {
    QVector<LogLine>& lines = m_logs[deviceId];
    lines.append(line);
    if (lines.size() > kLogCapacity) {
        lines.remove(0, lines.size() - kLogCapacity);
    }
}

void DeviceFeedBuffer::forget(const QString& deviceId) {
    m_terminals.remove(deviceId);
    m_logs.remove(deviceId);
}

QString DeviceFeedBuffer::terminal(const QString& deviceId, int maxBytes) const {
    const QByteArray& bytes = m_terminals.value(deviceId).bytes;
    if (maxBytes <= 0 || maxBytes >= bytes.size()) {
        return QString::fromUtf8(bytes);
    }
    return decodeTail(bytes.right(maxBytes));
}

QVector<DeviceFeedBuffer::LogLine> DeviceFeedBuffer::logs(const QString& deviceId,
                                                          int maxLines) const {
    const QVector<LogLine> lines = m_logs.value(deviceId);
    if (maxLines <= 0 || maxLines >= lines.size()) {
        return lines;
    }
    return lines.mid(lines.size() - maxLines);
}

qint64 DeviceFeedBuffer::terminalReceived(const QString& deviceId) const {
    return m_terminals.value(deviceId).received;
}

QString DeviceFeedBuffer::terminalSince(const QString& deviceId, qint64 mark) const {
    const Terminal terminal = m_terminals.value(deviceId);
    const qint64 fresh = terminal.received - mark;
    if (fresh <= 0) {
        return QString();
    }
    if (fresh >= terminal.bytes.size()) {
        return QString::fromUtf8(terminal.bytes);
    }
    return decodeTail(terminal.bytes.right(int(fresh)));
}

}  // namespace traceview
