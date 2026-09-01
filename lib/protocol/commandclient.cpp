#include "protocol/commandclient.h"

#include <QDateTime>
#include <btp/codec.hpp>
#include <btp/messages.hpp>

#include <cstdint>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/channelseal.h"
#include "protocol/protocolrouter.h"
#include "protocol/telemetrycatalog.h"

namespace traceview {

namespace {

// BTP/docs/commands.md section 1/2.1/2.2 -- object_id values within
// MessageType::Command, and the one action_id/action_version every executor
// in the ecosystem defines for "run this one shell line and capture its
// output" (mirrors ClockSync's own copy of these constants, and
// bally_dongle's/bally_OS's BtpTransport::btp_command).
constexpr quint16 kCommandRequestObjectId = 0x0001;
constexpr quint16 kCommandResultObjectId = 0x0002;
constexpr quint16 kShellActionId = 0x0001;
constexpr quint16 kShellActionVersion = 0x0001;
constexpr quint8 kResultStatusSuccess = 0x00;

}  // namespace

CommandClient::CommandClient(BtpSession* session, ProtocolRouter* router, QObject* parent)
    : QObject(parent), m_session(session) {
    connect(router, &ProtocolRouter::commandFrameReceived, this,
            &CommandClient::onCommandFrameReceived);
    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, &CommandClient::onReplyTimeout);
}

void CommandClient::configure(quint32 selfSourceId, quint32 selfBootId, quint32 targetSourceId,
                              TelemetryCatalog* catalog, const QByteArray& endpointKey,
                              std::function<quint32()> nextSequence) {
    m_selfSourceId = selfSourceId;
    m_selfBootId = selfBootId;
    m_targetSourceId = targetSourceId;
    m_catalog = catalog;
    m_endpointKey = endpointKey;
    m_nextSequence = std::move(nextSequence);
}

void CommandClient::send(const QString& commandLine) {
    if (commandLine.isEmpty() || m_selfSourceId == 0 || m_targetSourceId == 0 ||
        !m_nextSequence || m_pending) {
        return;
    }
    if (m_endpointKey.isEmpty()) {
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

    // COMMAND_REQUEST layout (commands.md section 2.1) is btp::encode_command_request:
    // the 20-octet prefix, zero flags/reserved, and the shell line as the
    // bytes_u32 parameters body.
    btp::CommandRequest request{};
    request.target_source_id = m_targetSourceId;
    request.target_boot_id = targetBootId;
    request.action_id = kShellActionId;
    request.action_version = kShellActionVersion;
    request.parameters = {reinterpret_cast<const std::uint8_t*>(commandBytes.constData()),
                          static_cast<std::size_t>(commandBytes.size())};

    QByteArray payload(20 + commandBytes.size(), Qt::Uninitialized);
    std::size_t written = 0;
    if (btp::encode_command_request(request, reinterpret_cast<std::uint8_t*>(payload.data()),
                                    static_cast<std::size_t>(payload.size()),
                                    &written) != btp::MessageError::Ok) {
        emit statusMessage(tr("command not sent: could not encode request"), 5000,
                           StatusSeverity::Error);
        return;
    }
    payload.truncate(static_cast<int>(written));

    const quint32 sequence = m_nextSequence();

    btp::Header header{};
    header.type = btp::MessageType::Command;
    header.flags = 0;
    header.source_id = m_selfSourceId;
    header.boot_id = m_selfBootId;
    header.sequence = sequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kCommandRequestObjectId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    const QByteArray sealed = ChannelSeal::seal(m_endpointKey, header, payload);
    if (sealed.isEmpty()) {
        emit statusMessage(tr("command not sent: failed to seal request"), 5000,
                           StatusSeverity::Error);
        return;
    }

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(sealed.constData());
    frame.payload.size = static_cast<std::size_t>(sealed.size());

    if (!m_session->sendFrame(frame)) {
        return;
    }

    m_pendingSequence = sequence;
    m_pending = true;
    m_replyTimer.start(kReplyTimeoutMs);
}

void CommandClient::onCommandFrameReceived(const BtpFrame& frame) {
    if (!m_pending || frame.objectId != kCommandResultObjectId) {
        return;
    }

    // COMMAND_RESULT layout (commands.md section 2.4) is btp::decode_command_result:
    // the request reference, status, the utf8_u16 message and the bytes_u32
    // result body (which this client does not use).
    btp::CommandResult result{};
    if (btp::decode_command_result(reinterpret_cast<const std::uint8_t*>(frame.payload.constData()),
                                   static_cast<std::size_t>(frame.payload.size()),
                                   &result) != btp::MessageError::Ok) {
        return;
    }

    // Only this client's own outstanding request's reply counts (same
    // correlation rule ClockSync/SubscriptionManager use): commands.md
    // section 1, the full (request_source_id, request_boot_id,
    // reply_to_sequence) triple, never reply_to_sequence alone.
    if (result.request.request_source_id != m_selfSourceId ||
        result.request.request_boot_id != m_selfBootId ||
        result.request.reply_to_sequence != m_pendingSequence) {
        return;
    }

    m_replyTimer.stop();
    m_pending = false;

    const quint8 status = result.status;
    const quint16 errorCode = result.error_code;
    const QString message =
        QString::fromUtf8(reinterpret_cast<const char*>(result.message.data),
                          static_cast<int>(result.message.size));

    if (status != kResultStatusSuccess) {
        emit statusMessage(tr("command failed (status 0x%1, error 0x%2): %3")
                               .arg(status, 2, 16, QChar('0'))
                               .arg(errorCode, 4, 16, QChar('0'))
                               .arg(message),
                           8000, StatusSeverity::Error);
        return;
    }
    emit statusMessage(message.isEmpty() ? tr("command result: (empty)")
                                         : tr("command result: %1").arg(message),
                       8000, StatusSeverity::Success);
}

void CommandClient::onReplyTimeout() {
    m_pending = false;
    emit statusMessage(tr("command timed out waiting for a result"), 5000,
                       StatusSeverity::Warning);
}

}  // namespace traceview
