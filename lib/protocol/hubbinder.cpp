#include "protocol/hubbinder.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"

namespace traceview {

namespace {

// Same normatively-fixed constants ClockSync declares its own copy of
// (BTP/docs/commands.md sections 1 and 2).
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

HubBinder::HubBinder(BtpSession* session, ProtocolRouter* router, QObject* parent)
    : QObject(parent), m_session(session) {
    connect(router, &ProtocolRouter::commandFrameReceived, this,
            &HubBinder::onCommandFrameReceived);
    m_clientSourceId = QRandomGenerator::global()->generate() | 1u;
    m_clientBootId = QRandomGenerator::global()->generate() | 1u;
}

QString HubBinder::formatSourceId(quint32 sourceId) {
    return QStringLiteral("0x%1").arg(sourceId, 8, 16, QLatin1Char('0'));
}

void HubBinder::bindChild(quint32 childSourceId, quint32 peerSourceId) {
    // Zero is not a legal BTP source_id, and here it is also how "not
    // configured yet" spells itself (an unsaved device, or a hub child whose
    // robot has not been picked). Holding it would mean issuing a bind the
    // dongle can only reject.
    if (childSourceId == 0 || peerSourceId == 0) {
        return;
    }
    m_bindings.insert(childSourceId, peerSourceId);
    if (m_targetSourceId == 0) {
        return;  // no session yet; resendAll() will issue it when one comes up
    }
    sendShellCommand(QStringLiteral("hub -bind %1, %2")
                         .arg(formatSourceId(childSourceId), formatSourceId(peerSourceId)),
                     childSourceId);
}

void HubBinder::unbindChild(quint32 childSourceId) {
    if (m_bindings.remove(childSourceId) == 0) {
        return;  // nothing was held for this child
    }
    if (m_targetSourceId == 0) {
        return;
    }
    sendShellCommand(QStringLiteral("hub -unbind %1").arg(formatSourceId(childSourceId)),
                     childSourceId);
}

void HubBinder::onSessionEstablished(quint32 peerSourceId, quint32 peerBootId) {
    m_targetSourceId = peerSourceId;
    m_targetBootId = peerBootId;
    resendAll();
}

void HubBinder::onSessionLost() {
    // The intent survives; only the dongle's copy of it is gone. Clearing
    // m_bindings here would lose exactly the state that has to outlive a
    // dropped cable, which is the whole reason this class holds it.
    m_targetSourceId = 0;
    m_targetBootId = 0;
    m_inFlight.clear();
}

void HubBinder::resendAll() {
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it) {
        sendShellCommand(QStringLiteral("hub -bind %1, %2")
                             .arg(formatSourceId(it.key()), formatSourceId(it.value())),
                         it.key());
    }
}

void HubBinder::sendShellCommand(const QString& commandLine, quint32 aboutChild) {
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
        emit statusMessage(tr("could not send hub binding for device 0x%1")
                               .arg(aboutChild, 8, 16, QLatin1Char('0')),
                           8000);
        return;
    }
    m_inFlight.insert(sequence, aboutChild);
}

void HubBinder::onCommandFrameReceived(const BtpFrame& frame) {
    if (frame.objectId != kCommandResultObjectId) {
        return;
    }
    if (static_cast<std::size_t>(frame.payload.size()) < kResultPrefixSize) {
        return;  // malformed COMMAND_RESULT; nothing usable
    }

    const quint32 requestSourceId = readLe32(frame.payload, 0);
    const quint32 requestBootId = readLe32(frame.payload, 4);
    const quint32 replyToSequence = readLe32(frame.payload, 8);
    if (requestSourceId != m_clientSourceId || requestBootId != m_clientBootId) {
        return;  // somebody else's round trip on this shared session
    }

    const auto pending = m_inFlight.constFind(replyToSequence);
    if (pending == m_inFlight.constEnd()) {
        return;
    }
    const quint32 aboutChild = pending.value();
    m_inFlight.remove(replyToSequence);

    const quint8 status = quint8(frame.payload.at(16));
    if (status == kResultStatusSuccess) {
        return;  // the common case, and deliberately quiet
    }

    // A rejected bind is the one thing that must never pass unnoticed: the
    // device it names will look connected and route nowhere, which is exactly
    // the failure this whole class exists to remove. The dongle's own reason
    // is carried in the result message ("tabela de vinculo cheia", ...).
    const quint16 messageSize = readLe16(frame.payload, 20);
    QString reason;
    if (static_cast<std::size_t>(frame.payload.size()) >= kResultPrefixSize + messageSize) {
        reason = QString::fromUtf8(frame.payload.constData() + int(kResultPrefixSize), messageSize);
    }
    emit statusMessage(tr("hub rejected the binding for device 0x%1: %2")
                           .arg(aboutChild, 8, 16, QLatin1Char('0'))
                           .arg(reason),
                       8000);
}

}  // namespace traceview
