#include "protocol/commandclient.h"

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
    if (commandLine.isEmpty() || m_targetSourceId == 0 || m_pendingLocalId != 0) {
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
        emit statusMessage(tr("command not sent: could not encode or seal request"), 5000,
                           StatusSeverity::Error);
        return;
    }
    m_pendingLocalId = localId;
}

void CommandClient::notifyOutcome() {
    if (m_pendingLocalId == 0U) {
        return;
    }
    const btp::CommandOutcome& outcome = m_node.command_outcome();
    if (outcome.local_id != m_pendingLocalId || outcome.event == btp::CommandEvent::None) {
        return;
    }
    m_pendingLocalId = 0U;

    if (outcome.event == btp::CommandEvent::TimedOut) {
        emit statusMessage(tr("command timed out waiting for a result"), 5000,
                           StatusSeverity::Warning);
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
        return;
    }
    emit statusMessage(message.isEmpty() ? tr("command result: (empty)")
                                         : tr("command result: %1").arg(message),
                       8000, StatusSeverity::Success);
}

}  // namespace traceview
