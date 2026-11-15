#include "protocol/commandclient.h"

#include <QRegularExpression>

#include "protocol/logseverity.h"
#include "protocol/telemetrycatalog.h"

namespace traceview {

namespace {
// commands.md section 2.1/2.2: the one action_id/action_version every
// executor in the ecosystem defines for "run this one shell line and capture
// its output" (mirrors ClockSync's own copy of these constants, and
// bally_dongle's/bally_OS's BtpTransport::btp_command).
constexpr quint16 kShellActionId = 0x0001;
constexpr quint16 kShellActionVersion = 0x0001;
constexpr quint8 kResultStatusSuccess = 0x00;

// Shell output as log lines: ANSI colour/cursor sequences removed (including
// one cut short where the robot truncated its result message), "! " shell
// prefixes kept, blank lines dropped.
QStringList logLines(const QString& text) {
    static const QRegularExpression ansi(QStringLiteral("\\x1B(\\[[0-9;?]*[A-Za-z]?)?"));
    QString clean = text;
    clean.remove(ansi);
    clean.remove(QLatin1Char('\r'));
    QStringList lines;
    for (const QString& line : clean.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            lines.append(line);
        }
    }
    return lines;
}

constexpr quint8 kCommand = quint8(LogSeverity::Command);
constexpr quint8 kWarn = quint8(LogSeverity::Warn);
constexpr quint8 kError = quint8(LogSeverity::Error);

}  // namespace

CommandClient::CommandClient(btp::Node& node, QObject* parent)
    : QObject(parent), m_node(node) {}

void CommandClient::configure(quint32 targetSourceId, TelemetryCatalog* catalog,
                              std::function<bool()> hasEndpointKey) {
    m_targetSourceId = targetSourceId;
    m_catalog = catalog;
    m_hasEndpointKey = std::move(hasEndpointKey);
}

void CommandClient::send(const QString& commandLine) {
    if (commandLine.isEmpty() || m_targetSourceId == 0) {
        return;
    }
    if (!m_hasEndpointKey || !m_hasEndpointKey()) {
        emit statusMessage(tr("command not sent: endpoint key not configured"), 5000,
                           StatusSeverity::Warning);
        return;
    }
    const quint32 targetBootId = m_catalog ? m_catalog->sourceBootId(m_targetSourceId) : 0;
    if (targetBootId == 0) {
        emit statusMessage(tr("command not sent: robot manifest not received yet"), 5000,
                           StatusSeverity::Warning);
        return;
    }

    const QByteArray commandBytes = commandLine.toUtf8();
    const std::uint32_t localId = m_node.command(
        m_targetSourceId, targetBootId, kShellActionId, kShellActionVersion,
        reinterpret_cast<const std::uint8_t*>(commandBytes.constData()),
        static_cast<std::size_t>(commandBytes.size()));
    if (localId == 0U) {
        // Every slot taken by commands still waiting for their result, or a
        // request that does not fit the node's buffers.
        emit statusMessage(m_pending.isEmpty()
                               ? tr("command not sent: could not encode or seal request")
                               : tr("command not sent: %n command(s) still waiting for a result",
                                    nullptr, int(m_pending.size())),
                           5000, StatusSeverity::Error);
        return;
    }
    m_pending.insert(localId, commandLine);
    emit logLine(kCommand, QStringLiteral("> ") + commandLine);
}

void CommandClient::notifyOutcome() {
    if (m_pending.isEmpty()) {
        return;
    }
    const btp::CommandOutcome& outcome = m_node.command_outcome();
    if (outcome.event == btp::CommandEvent::None || !m_pending.contains(outcome.local_id)) {
        return;
    }
    const QString command = m_pending.take(outcome.local_id);

    if (outcome.event == btp::CommandEvent::TimedOut) {
        // btp::kCommandTimeoutMs (5 s): a long command (a homing, say) may
        // still be running on the robot, whose late result is then dropped.
        emit statusMessage(tr("%1: no result within 5 s (it may still be running)").arg(command),
                           5000, StatusSeverity::Warning);
        emit logLine(kWarn, tr("%1: no result within 5 s; it may still be running").arg(command));
        return;
    }

    // Completed -- outcome.status/error_code/message are it, whatever the
    // action decided (Success is not implied by arriving at all).
    const QString message = QString::fromUtf8(
        reinterpret_cast<const char*>(outcome.message.data), int(outcome.message.size));
    if (outcome.status != kResultStatusSuccess) {
        emit statusMessage(tr("command failed (status 0x%1, error 0x%2): %3")
                               .arg(outcome.status, 2, 16, QChar('0'))
                               .arg(outcome.error_code, 4, 16, QChar('0'))
                               .arg(message),
                           8000, StatusSeverity::Error);
        for (const QString& line : logLines(message)) {
            emit logLine(kError, line);
        }
        emit logLine(kError, tr("%1 failed (status 0x%2)")
                                 .arg(command)
                                 .arg(outcome.status, 2, 16, QChar('0')));
        return;
    }
    emit statusMessage(message.isEmpty() ? tr("command result: (empty)")
                                         : tr("command result: %1").arg(message),
                       8000, StatusSeverity::Success);
    for (const QString& line : logLines(message)) {
        emit logLine(kCommand, line);
    }
}

}  // namespace traceview
