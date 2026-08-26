#include "protocol/commandclient.h"

#include <QDateTime>
#include <btp/codec.hpp>

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
constexpr std::size_t kRequestPrefixSize = 20;
constexpr std::size_t kResultPrefixSize = 22;  // up to and including message_size
constexpr quint8 kResultStatusSuccess = 0x00;

void appendLe32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value));
    out.append(static_cast<char>(value >> 8));
    out.append(static_cast<char>(value >> 16));
    out.append(static_cast<char>(value >> 24));
}

void appendLe16(QByteArray& out, quint16 value) {
    out.append(static_cast<char>(value));
    out.append(static_cast<char>(value >> 8));
}

quint32 readLe32(const QByteArray& data, int offset) {
    return quint32(quint8(data.at(offset))) | (quint32(quint8(data.at(offset + 1))) << 8) |
           (quint32(quint8(data.at(offset + 2))) << 16) |
           (quint32(quint8(data.at(offset + 3))) << 24);
}

quint16 readLe16(const QByteArray& data, int offset) {
    return quint16(quint8(data.at(offset))) | (quint16(quint8(data.at(offset + 1))) << 8);
}

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
        emit statusMessage(tr("command not sent: endpoint key not configured"), 5000);
        return;
    }
    const quint32 targetBootId = m_catalog ? m_catalog->sourceBootId(m_targetSourceId) : 0;
    if (targetBootId == 0) {
        emit statusMessage(tr("command not sent: robot manifest not received yet"), 5000);
        return;
    }

    const QByteArray commandBytes = commandLine.toUtf8();

    QByteArray payload;
    payload.reserve(int(kRequestPrefixSize) + commandBytes.size());
    appendLe32(payload, m_targetSourceId);
    appendLe32(payload, targetBootId);
    appendLe16(payload, kShellActionId);
    appendLe16(payload, kShellActionVersion);
    appendLe16(payload, 0);  // flags
    appendLe16(payload, 0);  // reserved
    appendLe32(payload, static_cast<quint32>(commandBytes.size()));
    payload.append(commandBytes);

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
        emit statusMessage(tr("command not sent: failed to seal request"), 5000);
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
    if (static_cast<std::size_t>(frame.payload.size()) < kResultPrefixSize) {
        return;
    }

    // Only this client's own outstanding request's reply counts (same
    // correlation rule ClockSync/SubscriptionManager use): commands.md
    // section 1, the full (request_source_id, request_boot_id,
    // reply_to_sequence) triple, never reply_to_sequence alone.
    const quint32 requestSourceId = readLe32(frame.payload, 0);
    const quint32 requestBootId = readLe32(frame.payload, 4);
    const quint32 replyToSequence = readLe32(frame.payload, 8);
    if (requestSourceId != m_selfSourceId || requestBootId != m_selfBootId ||
        replyToSequence != m_pendingSequence) {
        return;
    }

    m_replyTimer.stop();
    m_pending = false;

    const quint8 status = quint8(frame.payload.at(16));
    const quint16 errorCode = readLe16(frame.payload, 18);
    const quint16 messageSize = readLe16(frame.payload, 20);
    if (static_cast<std::size_t>(frame.payload.size()) < kResultPrefixSize + messageSize) {
        return;  // truncated; nothing usable
    }
    const QString message =
        QString::fromUtf8(frame.payload.constData() + int(kResultPrefixSize), messageSize);

    if (status != kResultStatusSuccess) {
        emit statusMessage(tr("command failed (status 0x%1, error 0x%2): %3")
                               .arg(status, 2, 16, QChar('0'))
                               .arg(errorCode, 4, 16, QChar('0'))
                               .arg(message),
                           8000);
        return;
    }
    emit statusMessage(message.isEmpty() ? tr("command result: (empty)")
                                         : tr("command result: %1").arg(message),
                       8000);
}

void CommandClient::onReplyTimeout() {
    m_pending = false;
    emit statusMessage(tr("command timed out waiting for a result"), 5000);
}

}  // namespace traceview
