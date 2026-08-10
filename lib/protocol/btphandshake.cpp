#include "protocol/btphandshake.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"

namespace traceview {

namespace {
constexpr quint16 kControlHello = 0x0001;
constexpr quint16 kControlHelloResult = 0x0002;
constexpr quint8 kRoleDesktop = 0x03;
constexpr quint8 kHelloResultSuccess = 0x00;
constexpr int kEnterTimeoutMs = 8000;   // generous: the dongle may still be
                                        // mid boot-animation/date prompt
                                        // (StartupConfig) when we connect.
constexpr int kHelloTimeoutMs = 3000;   // spec requires HELLO_RESULT within
                                        // 2000ms of HELLO; a little slack.
constexpr int kMaxLineBufferBytes = 512;

// Appends `value` as `width` little-endian bytes.
void appendLe(QByteArray& out, quint32 value, int width) {
    for (int i = 0; i < width; ++i) {
        out.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

QByteArray buildHelloPayload(quint16 maxLogicalPayload, quint16 sessionTimeoutMs) {
    QByteArray payload;
    payload.append(static_cast<char>(kRoleDesktop));
    payload.append(static_cast<char>(1));  // version_count
    appendLe(payload, 0, 2);               // flags
    appendLe(payload, maxLogicalPayload, 4);
    appendLe(payload, 2, 2);   // max_inflight_reassemblies
    appendLe(payload, 8, 2);   // max_subscriptions
    appendLe(payload, 16, 4);  // max_dedup_entries
    appendLe(payload, sessionTimeoutMs, 4);  // session_timeout_ms
    for (int i = 0; i < 16; ++i) {
        // peer_uuid: opaque, stable-for-this-run, non-zero identity. A
        // per-process random UUID is enough for topico 15's vertical slice;
        // persisting a real client identity is out of scope until a topico
        // actually needs to recognize this desktop across restarts.
        payload.append(static_cast<char>(QRandomGenerator::global()->bounded(1, 256)));
    }
    appendLe(payload, 0, 4);              // config_revision (no manifest yet)
    payload.append(static_cast<char>(1));  // versions = [1]
    return payload;
}

}  // namespace

BtpHandshake::BtpHandshake(BtpSession* session, ProtocolRouter* router, QObject* parent)
    : QObject(parent), m_session(session) {
    connect(router, &ProtocolRouter::controlFrameReceived, this, &BtpHandshake::onControlFrameReceived);

    m_enterTimer.setSingleShot(true);
    connect(&m_enterTimer, &QTimer::timeout, this, &BtpHandshake::onEnterTimeout);
    m_helloTimer.setSingleShot(true);
    connect(&m_helloTimer, &QTimer::timeout, this, &BtpHandshake::onHelloTimeout);
}

void BtpHandshake::start() {
    m_lineBuffer.clear();
    m_state = State::AwaitingReady;

    QByteArray nonce;
    for (int i = 0; i < 16; ++i) {
        nonce.append("0123456789abcdef"[QRandomGenerator::global()->bounded(16)]);
    }
    m_expectedReady = "BTP/1 READY " + nonce + "\r\n";

    emit bytesToWrite("BTP/1 ENTER " + nonce + "\r\n");
    m_enterTimer.start(kEnterTimeoutMs);
}

void BtpHandshake::feedRawBytes(const QByteArray& data) {
    if (m_state != State::AwaitingReady) {
        return;
    }
    m_lineBuffer.append(data);
    if (m_lineBuffer.size() > kMaxLineBufferBytes) {
        m_lineBuffer.remove(0, m_lineBuffer.size() - kMaxLineBufferBytes);
    }
    if (m_lineBuffer.contains(m_expectedReady)) {
        m_enterTimer.stop();
        m_state = State::AwaitingHelloResult;
        m_helloTimer.start(kHelloTimeoutMs);
        sendHello();
    }
}

void BtpHandshake::sendHello() {
    const QByteArray payload = buildHelloPayload(/*maxLogicalPayload=*/4096, /*sessionTimeoutMs=*/15000);

    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = QRandomGenerator::global()->generate() | 1u;  // non-zero
    header.boot_id = QRandomGenerator::global()->generate() | 1u;    // non-zero
    header.sequence = 1;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kControlHello;
    header.fragment_index = 0;
    header.fragment_count = 1;

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    frame.payload.size = static_cast<std::size_t>(payload.size());

    if (!m_session->sendFrame(frame)) {
        fail(QStringLiteral("failed to encode HELLO frame"));
    }
}

void BtpHandshake::onControlFrameReceived(const traceview::BtpFrame& frame) {
    if (m_state != State::AwaitingHelloResult) {
        return;
    }
    if (frame.objectId != kControlHelloResult) {
        return;  // some other CONTROL frame (e.g. STATUS); not for us
    }
    if (frame.payload.size() < 13) {
        fail(QStringLiteral("HELLO_RESULT payload too short"));
        return;
    }
    const quint8 status = static_cast<quint8>(frame.payload.at(12));
    m_helloTimer.stop();
    if (status == kHelloResultSuccess) {
        m_state = State::Established;
        emit sessionEstablished();
    } else {
        fail(QStringLiteral("HELLO rejected, status=%1").arg(status));
    }
}

void BtpHandshake::onEnterTimeout() {
    if (m_state == State::AwaitingReady) {
        fail(QStringLiteral("no BTP/1 READY within %1 ms").arg(kEnterTimeoutMs));
    }
}

void BtpHandshake::onHelloTimeout() {
    if (m_state == State::AwaitingHelloResult) {
        fail(QStringLiteral("no HELLO_RESULT within %1 ms").arg(kHelloTimeoutMs));
    }
}

void BtpHandshake::fail(const QString& reason) {
    m_state = State::Idle;
    emit sessionFailed(reason);
}

}  // namespace traceview
