#include "protocol/clocksync.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"

namespace traceview {

namespace {

// BTP/docs/commands.md sections 1 and 2 -- object_id values within
// MessageType::Command, and the one action_id/action_version every
// executor in the ecosystem defines for "run this one shell line and capture
// its output" (bally_dongle's BtpTransport::btp_command mirrors this exact
// layout; each side of the wire defines its own copy of these
// normatively-fixed constants, same as btpbackend.cpp already does for
// TERMINAL_IN/OUT).
constexpr quint16 kCommandRequestObjectId = 0x0001;
constexpr quint16 kCommandResultObjectId = 0x0002;
constexpr quint16 kShellActionId = 0x0001;
constexpr quint16 kShellActionVersion = 0x0001;
constexpr std::size_t kRequestPrefixSize = 20;
constexpr std::size_t kResultPrefixSize = 22;  // up to and including message_size
constexpr quint8 kResultStatusSuccess = 0x00;

constexpr int kReplyTimeoutMs = 3000;
// "desregulado" per the user's own framing: only correct a clock that is
// actually off by a noticeable amount, not one a few seconds of round-trip
// jitter would explain.
constexpr qint64 kDriftToleranceSecs = 5;

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

ClockSync::ClockSync(BtpSession* session, ProtocolRouter* router, QObject* parent)
    : QObject(parent), m_session(session) {
    connect(router, &ProtocolRouter::commandFrameReceived, this,
            &ClockSync::onCommandFrameReceived);
    // Private per-process identity, same construction ManifestClient/
    // SerialWidgetBridge use for their own request channels -- see this
    // class's header comment for why it need not match BtpHandshake's HELLO
    // identity.
    m_clientSourceId = QRandomGenerator::global()->generate() | 1u;
    m_clientBootId = QRandomGenerator::global()->generate() | 1u;

    m_replyTimer.setSingleShot(true);
    connect(&m_replyTimer, &QTimer::timeout, this, &ClockSync::onReplyTimeout);
}

void ClockSync::onSessionEstablished(quint32 peerSourceId, quint32 peerBootId) {
    m_targetSourceId = peerSourceId;
    m_targetBootId = peerBootId;
    requestClock();
}

void ClockSync::requestClock() {
    sendShellCommand(QStringLiteral("dongle clock"), Pending::Clock);
}

void ClockSync::sendShellCommand(const QString& commandLine, Pending expecting) {
    if (m_targetSourceId == 0 || m_targetBootId == 0) {
        return;
    }

    const QByteArray commandBytes = commandLine.toUtf8();

    QByteArray payload;
    payload.reserve(int(kRequestPrefixSize) + commandBytes.size());
    appendLe32(payload, m_targetSourceId);
    appendLe32(payload, m_targetBootId);
    appendLe16(payload, kShellActionId);
    appendLe16(payload, kShellActionVersion);
    appendLe16(payload, 0);  // flags
    appendLe16(payload, 0);  // reserved
    appendLe32(payload, static_cast<quint32>(commandBytes.size()));
    payload.append(commandBytes);

    const quint32 sequence = m_nextSequence++;

    btp::Header header{};
    header.type = btp::MessageType::Command;
    header.flags = 0;
    header.source_id = m_clientSourceId;
    header.boot_id = m_clientBootId;
    header.sequence = sequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kCommandRequestObjectId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    frame.payload.size = static_cast<std::size_t>(payload.size());

    if (!m_session->sendFrame(frame)) {
        return;
    }

    m_pendingSequence = sequence;
    m_pending = expecting;
    m_replyTimer.start(kReplyTimeoutMs);
}

void ClockSync::onCommandFrameReceived(const BtpFrame& frame) {
    if (m_pending == Pending::None || frame.objectId != kCommandResultObjectId) {
        return;
    }
    if (static_cast<std::size_t>(frame.payload.size()) < kResultPrefixSize) {
        return;  // malformed COMMAND_RESULT; nothing usable
    }

    // Only this client's own outstanding request's reply counts -- a stray
    // or late COMMAND_RESULT from a previous, already-timed-out attempt (or
    // some other tool sharing this session) must not be mistaken for it.
    const quint32 requestSourceId = readLe32(frame.payload, 0);
    const quint32 requestBootId = readLe32(frame.payload, 4);
    const quint32 replyToSequence = readLe32(frame.payload, 8);
    if (requestSourceId != m_clientSourceId || requestBootId != m_clientBootId ||
        replyToSequence != m_pendingSequence) {
        return;
    }

    m_replyTimer.stop();
    const Pending completed = m_pending;
    m_pending = Pending::None;

    const quint8 status = quint8(frame.payload.at(16));
    const quint16 messageSize = readLe16(frame.payload, 20);
    if (static_cast<std::size_t>(frame.payload.size()) < kResultPrefixSize + messageSize) {
        return;  // truncated; nothing usable
    }
    const QString message =
        QString::fromUtf8(frame.payload.constData() + int(kResultPrefixSize), messageSize);

    if (status != kResultStatusSuccess) {
        emit statusMessage(tr("dongle clock sync failed: %1").arg(message), 8000);
        return;
    }

    if (completed == Pending::Clock) {
        static const QRegularExpression kEpochPattern(QStringLiteral("epoch=(\\d+)"));
        const QRegularExpressionMatch match = kEpochPattern.match(message);
        if (!match.hasMatch()) {
            return;  // unexpected reply shape; nothing to correct against
        }

        const qint64 dongleEpoch = match.captured(1).toLongLong();
        const qint64 driftSecs = QDateTime::currentSecsSinceEpoch() - dongleEpoch;
        if (qAbs(driftSecs) <= kDriftToleranceSecs) {
            return;  // close enough
        }

        const QString hostTime =
            QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        sendShellCommand(QStringLiteral("dongle set_clock \"%1\"").arg(hostTime),
                         Pending::SetClock);
    } else {
        emit statusMessage(tr("dongle clock corrected (%1)").arg(message), 5000);
    }
}

void ClockSync::onReplyTimeout() {
    m_pending = Pending::None;
}

}  // namespace traceview
