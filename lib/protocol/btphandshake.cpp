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
// The dongle's AppRuntime::begin() (bally_dongle) only starts reading the
// serial port -- and therefore only gets a chance to see "BTP/1 ENTER" at
// all -- after it finishes mounting the SD card, initializing ESP-NOW,
// opening/migrating the SQLite database and registering every shell module.
// All of that runs once, synchronously, before its tick() loop (where the
// ENTER line is actually recognized) ever executes -- so this has to cover a
// full cold boot, not just a quick text exchange. 20s is generous against a
// slow SD card; bump it further if a real device still needs more.
constexpr int kEnterTimeoutMs = 20000;
constexpr int kHelloTimeoutMs = 3000;  // spec requires HELLO_RESULT within
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
    // versions enumerates every envelope version this build's btp::codec can
    // speak (session-and-terminal.md section 1: "increasing", no gaps
    // required) so the dongle -- which picks the highest version common to
    // both sides -- always sees our real ceiling, not a version we happened
    // to hardcode. Today kMinimumProtocolVersion == kMaximumProtocolVersion
    // == 1, so this is a single-entry list; it grows on its own once the
    // library supports more.
    const quint8 versionCount = btp::kMaximumProtocolVersion - btp::kMinimumProtocolVersion + 1;
    payload.append(static_cast<char>(versionCount));  // version_count
    appendLe(payload, 0, 2);                          // flags
    appendLe(payload, maxLogicalPayload, 4);
    appendLe(payload, 2, 2);                 // max_inflight_reassemblies
    appendLe(payload, 8, 2);                 // max_subscriptions
    appendLe(payload, 16, 4);                // max_dedup_entries
    appendLe(payload, sessionTimeoutMs, 4);  // session_timeout_ms
    for (int i = 0; i < 16; ++i) {
        // peer_uuid: opaque, stable-for-this-run, non-zero identity. A
        // per-process random UUID is enough for topico 15's vertical slice;
        // persisting a real client identity is out of scope until a topico
        // actually needs to recognize this desktop across restarts.
        payload.append(static_cast<char>(QRandomGenerator::global()->bounded(1, 256)));
    }
    appendLe(payload, 0, 4);  // config_revision (no manifest yet)
    for (quint8 version = btp::kMinimumProtocolVersion; version <= btp::kMaximumProtocolVersion;
         ++version) {
        payload.append(static_cast<char>(version));
    }
    return payload;
}

}  // namespace

BtpHandshake::BtpHandshake(BtpSession* session, ProtocolRouter* router, QObject* parent)
    : QObject(parent), m_session(session) {
    connect(router, &ProtocolRouter::controlFrameReceived, this,
            &BtpHandshake::onControlFrameReceived);

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
    const QByteArray payload =
        buildHelloPayload(/*maxLogicalPayload=*/4096, /*sessionTimeoutMs=*/15000);

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
        fail(tr("failed to encode HELLO frame"));
    }
}

void BtpHandshake::onControlFrameReceived(const traceview::BtpFrame& frame) {
    if (m_state != State::AwaitingHelloResult) {
        return;
    }
    if (frame.objectId != kControlHelloResult) {
        return;  // some other CONTROL frame (e.g. STATUS); not for us
    }
    if (frame.payload.size() < 14) {
        fail(tr("HELLO_RESULT payload too short"));
        return;
    }
    const quint8 status = static_cast<quint8>(frame.payload.at(12));
    m_helloTimer.stop();
    if (status == kHelloResultSuccess) {
        // selected_version (offset 13) is the highest version the dongle
        // found in common with what our own HELLO just advertised
        // (session-and-terminal.md section 2: "the responder picks the
        // highest version it can use"). It can only be one of the versions we
        // offered --
        // anything else is a peer that isn't honoring the negotiation, not a
        // version this client can silently go along with.
        const quint8 selectedVersion = static_cast<quint8>(frame.payload.at(13));
        if (selectedVersion < btp::kMinimumProtocolVersion ||
            selectedVersion > btp::kMaximumProtocolVersion) {
            fail(tr("HELLO_RESULT selected an unadvertised version %1").arg(selectedVersion));
            return;
        }
        m_state = State::Established;
        // config_revision sits at offset 48 (session-and-terminal.md
        // section 2); a HELLO_RESULT this short predates the field (or came
        // from a peer that has no catalog yet), so treat it as 0 -- "the peer
        // publishes no manifest" is the documented meaning of 0 anyway.
        quint32 peerConfigRevision = 0;
        if (frame.payload.size() >= 52) {
            peerConfigRevision = (quint32(quint8(frame.payload.at(48))) << 0) |
                                 (quint32(quint8(frame.payload.at(49))) << 8) |
                                 (quint32(quint8(frame.payload.at(50))) << 16) |
                                 (quint32(quint8(frame.payload.at(51))) << 24);
        }
        emit sessionEstablished(frame.sourceId, frame.bootId, peerConfigRevision, selectedVersion);
    } else {
        fail(tr("HELLO rejected, status=%1").arg(status));
    }
}

void BtpHandshake::onEnterTimeout() {
    if (m_state == State::AwaitingReady) {
        fail(tr("no BTP/1 READY within %1 ms").arg(kEnterTimeoutMs));
    }
}

void BtpHandshake::onHelloTimeout() {
    if (m_state == State::AwaitingHelloResult) {
        fail(tr("no HELLO_RESULT within %1 ms").arg(kHelloTimeoutMs));
    }
}

void BtpHandshake::fail(const QString& reason) {
    m_state = State::Idle;
    emit sessionFailed(reason);
}

}  // namespace traceview
