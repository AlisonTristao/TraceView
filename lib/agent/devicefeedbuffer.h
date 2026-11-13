#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// What each device has said recently, kept so an AI assistant can read a
// robot's answers (read_terminal/read_device_log) without a serial monitor
// or robot log widget being open. Fed by MainWindow from every
// DeviceConnection's Backend; bounded per device, oldest dropped first.
class DeviceFeedBuffer {
public:
    static constexpr int kTerminalCapacityBytes = 64 * 1024;
    static constexpr int kLogCapacity = 500;

    struct LogLine {
        QDateTime received;
        quint64 timestampUs = 0;
        quint8 severity = 0;
        QString message;
    };

    void appendTerminal(const QString& deviceId, const QByteArray& bytes);
    void appendLog(const QString& deviceId, const LogLine& line);
    void forget(const QString& deviceId);

    // The last `maxBytes` of terminal output (the whole buffer if <= 0),
    // decoded as UTF-8. A cut multi-byte sequence at the front is dropped.
    QString terminal(const QString& deviceId, int maxBytes = 0) const;
    // The last `maxLines` log lines, oldest first (all of them if <= 0).
    QVector<LogLine> logs(const QString& deviceId, int maxLines = 0) const;
    // Total terminal bytes ever received from `deviceId` -- lets a caller
    // read only what arrived after a known point (see terminalSince()).
    qint64 terminalReceived(const QString& deviceId) const;
    // Terminal output received after `mark` (an earlier terminalReceived()),
    // as far as the buffer still holds it.
    QString terminalSince(const QString& deviceId, qint64 mark) const;

private:
    struct Terminal {
        QByteArray bytes;
        qint64 received = 0;
    };
    QHash<QString, Terminal> m_terminals;
    QHash<QString, QVector<LogLine>> m_logs;
};

}  // namespace traceview
