#include "protocol/commandclient.h"

#include <QDateTime>
#include <QRegularExpression>

#include "backend/plaintext.h"
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

// A robot's shell colours its output with ANSI sequences (TinyShell), and a
// COMMAND_RESULT carries that output as is: made plain (backend/plaintext.h)
// before it reaches anything that shows it as text -- the log, the status
// bar, the notification history and runCommand()'s callers (the TraceKit
// panel).
//
// Shell output as log lines: plain text, "! " shell prefixes kept, blank
// lines dropped.
QStringList logLines(const QString& text) {
    const QString clean = plainText(text);
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

std::uint32_t CommandClient::issue(const QString& commandLine, QString* why) {
    if (commandLine.isEmpty() || m_targetSourceId == 0) {
        *why = tr("not connected to a device that takes commands");
        return 0U;
    }
    if (!m_hasEndpointKey || !m_hasEndpointKey()) {
        *why = tr("command not sent: endpoint key not configured");
        return 0U;
    }
    const quint32 targetBootId = m_catalog ? m_catalog->sourceBootId(m_targetSourceId) : 0;
    if (targetBootId == 0) {
        *why = tr("command not sent: robot manifest not received yet");
        return 0U;
    }

    const QByteArray commandBytes = commandLine.toUtf8();
    const std::uint32_t localId = m_node.command(
        m_targetSourceId, targetBootId, kShellActionId, kShellActionVersion,
        reinterpret_cast<const std::uint8_t*>(commandBytes.constData()),
        static_cast<std::size_t>(commandBytes.size()));
    if (localId == 0U) {
        // Every slot taken by commands still waiting for their result, or a
        // request that does not fit the node's buffers.
        const int waiting = int(m_pending.size() + m_queries.size());
        *why = waiting == 0
                   ? tr("command not sent: could not encode or seal request")
                   : tr("command not sent: %n command(s) still waiting for a result", nullptr,
                        waiting);
    }
    return localId;
}

void CommandClient::send(const QString& commandLine) {
    if (commandLine.isEmpty() || m_targetSourceId == 0) {
        return;
    }
    QString why;
    const std::uint32_t localId = issue(commandLine, &why);
    if (localId == 0U) {
        emit statusMessage(why, 5000,
                           why.contains(QLatin1String("waiting")) ||
                                   why.contains(QLatin1String("encode"))
                               ? StatusSeverity::Error
                               : StatusSeverity::Warning);
        return;
    }
    m_pending.insert(localId, commandLine);
    m_sentMs.insert(localId, QDateTime::currentMSecsSinceEpoch());
    emit logLine(kCommand, QStringLiteral("> ") + commandLine);
}

void CommandClient::query(const QString& commandLine, ResultFn done) {
    QString why;
    const std::uint32_t localId = issue(commandLine, &why);
    if (localId == 0U) {
        done(false, why);
        return;
    }
    m_queries.insert(localId, std::move(done));
    m_sentMs.insert(localId, QDateTime::currentMSecsSinceEpoch());
}

void CommandClient::notifyOutcome(OutcomeSource source) {
    if (m_pending.isEmpty() && m_queries.isEmpty()) {
        return;
    }
    const btp::CommandOutcome& outcome = m_node.command_outcome();
    if (outcome.event == btp::CommandEvent::None) {
        return;
    }
    // The node keeps its last outcome until the next one (see the header):
    // only a fresh one may settle a request. A Completed is fresh when a
    // COMMAND_RESULT was just received; on a tick the only new outcome there
    // can be is a TimedOut, and only for a request out for about the node's
    // timeout -- an old TimedOut whose slot a new request reuses is not.
    if (source == OutcomeSource::Tick) {
        if (outcome.event != btp::CommandEvent::TimedOut) {
            return;
        }
        constexpr qint64 kFreshTimeoutMs = qint64(btp::kCommandTimeoutMs) - 250;
        const qint64 sent = m_sentMs.value(outcome.local_id, -1);
        if (sent < 0 || QDateTime::currentMSecsSinceEpoch() - sent < kFreshTimeoutMs) {
            return;
        }
    }
    m_sentMs.remove(outcome.local_id);
    if (m_queries.contains(outcome.local_id)) {
        const ResultFn done = m_queries.take(outcome.local_id);
        if (outcome.event == btp::CommandEvent::TimedOut) {
            done(false, tr("no result within 5 s"));
            return;
        }
        const QString message = plainText(QString::fromUtf8(
            reinterpret_cast<const char*>(outcome.message.data), int(outcome.message.size)));
        done(outcome.status == kResultStatusSuccess, message);
        return;
    }
    if (!m_pending.contains(outcome.local_id)) {
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
    const QString message = plainText(QString::fromUtf8(
        reinterpret_cast<const char*>(outcome.message.data), int(outcome.message.size)));
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
