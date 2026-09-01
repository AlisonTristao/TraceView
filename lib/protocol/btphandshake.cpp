#include "protocol/btphandshake.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <btp/codec.hpp>
#include <btp/messages.hpp>

#include <cstdint>

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
// ENTER line is actually recognized) ever executes -- so the handshake has to
// cover a full cold boot, not just a quick text exchange.
//
// It covers it by RETRYING, not by waiting once for a long time, and the
// difference matters: a single 20s wait assumed the ENTER line itself always
// arrives, and the one thing a cold boot does is eat it. Opening the port
// asserts DTR, which can reset the ESP32-S3, so the very first ENTER is
// written into a device that is about to reboot and is simply lost. There is
// then nothing left to answer, and the old code waited out its 20 seconds,
// gave up for good, and left a device that reads "connected" in the UI and
// never speaks -- with no path back except unplugging it by hand.
//
// kMaxEnterAttempts * kEnterTimeoutMs keeps the total budget at the same 20s
// as before, so nothing regressed for a genuinely slow SD card; what changed
// is that a lost line now costs one retry instead of the whole connection.
constexpr int kEnterTimeoutMs = 4000;
constexpr int kMaxEnterAttempts = 5;
constexpr int kHelloTimeoutMs = 3000;  // spec requires HELLO_RESULT within
                                       // 2000ms of HELLO; a little slack.
constexpr int kMaxLineBufferBytes = 512;

QByteArray buildHelloPayload(quint16 maxLogicalPayload, quint16 sessionTimeoutMs) {
    btp::Hello hello{};
    hello.role = kRoleDesktop;
    // versions enumerates every envelope version this build's btp::codec can
    // speak (session-and-terminal.md section 1: "increasing", no gaps
    // required) so the dongle -- which picks the highest version common to
    // both sides -- always sees our real ceiling, not a version we happened
    // to hardcode. Today kMinimumProtocolVersion == kMaximumProtocolVersion
    // == 1, so this is a single-entry list; it grows on its own once the
    // library supports more.
    hello.version_count = btp::kMaximumProtocolVersion - btp::kMinimumProtocolVersion + 1;
    for (quint8 v = btp::kMinimumProtocolVersion; v <= btp::kMaximumProtocolVersion; ++v) {
        hello.versions[v - btp::kMinimumProtocolVersion] = v;
    }
    hello.max_logical_payload = maxLogicalPayload;
    hello.max_inflight_reassemblies = 2;
    hello.max_subscriptions = 8;
    hello.max_dedup_entries = 16;
    hello.session_timeout_ms = sessionTimeoutMs;
    for (auto& octet : hello.peer_uuid) {
        // peer_uuid: opaque, stable-for-this-run, non-zero identity. A
        // per-process random UUID is enough for topico 15's vertical slice;
        // persisting a real client identity is out of scope until a topico
        // actually needs to recognize this desktop across restarts.
        octet = static_cast<std::uint8_t>(QRandomGenerator::global()->bounded(1, 256));
    }
    hello.config_revision = 0;  // no manifest yet

    std::uint8_t buffer[64];
    std::size_t written = 0;
    if (btp::encode_hello(hello, buffer, sizeof(buffer), &written) != btp::MessageError::Ok) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(buffer), static_cast<int>(written));
}

}  // namespace

BtpHandshake::BtpHandshake(BtpSession* session, ProtocolRouter* router, QObject* parent,
                           int enterTimeoutMs, int maxEnterAttempts)
    : QObject(parent),
      m_session(session),
      m_enterTimeoutMs(enterTimeoutMs > 0 ? enterTimeoutMs : kEnterTimeoutMs),
      m_maxEnterAttempts(maxEnterAttempts > 0 ? maxEnterAttempts : kMaxEnterAttempts) {
    connect(router, &ProtocolRouter::controlFrameReceived, this,
            &BtpHandshake::onControlFrameReceived);

    m_enterTimer.setSingleShot(true);
    connect(&m_enterTimer, &QTimer::timeout, this, &BtpHandshake::onEnterTimeout);
    m_helloTimer.setSingleShot(true);
    connect(&m_helloTimer, &QTimer::timeout, this, &BtpHandshake::onHelloTimeout);
}

void BtpHandshake::start() {
    m_lineBuffer.clear();
    m_consoleWatchBuffer.clear();
    m_state = State::AwaitingReady;
    m_enterAttempts = 0;

    m_enterNonce.clear();
    for (int i = 0; i < 16; ++i) {
        m_enterNonce.append("0123456789abcdef"[QRandomGenerator::global()->bounded(16)]);
    }
    m_expectedReady = "BTP/1 READY " + m_enterNonce + "\r\n";

    sendEnter();
}

void BtpHandshake::sendEnter() {
    ++m_enterAttempts;
    emit bytesToWrite("BTP/1 ENTER " + m_enterNonce + "\r\n");
    m_enterTimer.start(m_enterTimeoutMs);
}

void BtpHandshake::feedRawBytes(const QByteArray& data) {
    if (m_state == State::Established) {
        // "BTP/1 CONSOLE\r\n" is what the dongle emits, in the clear and after
        // its last frame, whenever it leaves protocol mode -- inactivity
        // watchdog, SESSION_CLOSE, or a human typing at the bench. Nothing
        // else here would catch it: the transport is still open and BtpSession
        // has no watchdog. Fold it into the same failure path an exhausted
        // handshake uses, so the session recovers (close -> retry -> re-ENTER)
        // instead of going silently dead.
        static const QByteArray kConsoleLine = QByteArrayLiteral("BTP/1 CONSOLE\r\n");
        m_consoleWatchBuffer.append(data);
        const bool seen = m_consoleWatchBuffer.contains(kConsoleLine);
        if (m_consoleWatchBuffer.size() > kMaxLineBufferBytes) {
            m_consoleWatchBuffer.remove(0, m_consoleWatchBuffer.size() - kMaxLineBufferBytes);
        }
        if (seen) {
            m_consoleWatchBuffer.clear();
            fail(tr("dongle returned to console (BTP/1 CONSOLE)"));
        }
        return;
    }
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
    // 30000, matching bally_dongle's SerialSession::LocalLimits default (the
    // negotiated watchdog is min of the two). The desktop keepalive
    // (BtpBackend, topico 35 B.1) refreshes it every ~5s; this window is the
    // backstop for a desktop that vanished without a SESSION_CLOSE.
    const QByteArray payload =
        buildHelloPayload(/*maxLogicalPayload=*/4096, /*sessionTimeoutMs=*/30000);
    if (payload.isEmpty()) {
        fail(tr("failed to encode HELLO payload"));
        return;
    }

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

    // The 52-octet HELLO_RESULT layout (session-and-terminal.md section 2) is
    // btp::decode_hello_result: the request reference, the SUCCESS/UNSUPPORTED
    // status check, the effective limits and config_revision at offset 48.
    btp::HelloResult result{};
    const btp::MessageError err = btp::decode_hello_result(
        reinterpret_cast<const std::uint8_t*>(frame.payload.constData()),
        static_cast<std::size_t>(frame.payload.size()), &result);
    if (err != btp::MessageError::Ok) {
        fail(tr("malformed HELLO_RESULT payload"));
        return;
    }

    m_helloTimer.stop();
    if (result.status != kHelloResultSuccess) {
        fail(tr("HELLO rejected, status=%1").arg(result.status));
        return;
    }

    // selected_version is the highest version the dongle found in common with
    // what our own HELLO advertised (section 2). It can only be one of the
    // versions we offered -- anything else is a peer that isn't honoring the
    // negotiation, not a version this client can silently go along with.
    if (result.selected_version < btp::kMinimumProtocolVersion ||
        result.selected_version > btp::kMaximumProtocolVersion) {
        fail(tr("HELLO_RESULT selected an unadvertised version %1").arg(result.selected_version));
        return;
    }

    m_state = State::Established;
    emit sessionEstablished(frame.sourceId, frame.bootId, result.config_revision,
                            result.selected_version);
}

void BtpHandshake::onEnterTimeout() {
    if (m_state != State::AwaitingReady) {
        return;
    }
    // Retry rather than give up: the common reason for silence here is that
    // the ENTER line was lost to a DTR-triggered reset or arrived while the
    // dongle was still in AppRuntime::begin(), and in both cases writing it
    // again is all that is needed. Only a peer that stays silent through the
    // whole budget is a real failure.
    if (m_enterAttempts < m_maxEnterAttempts) {
        sendEnter();
        return;
    }
    fail(tr("no BTP/1 READY after %1 attempts over %2 ms")
             .arg(m_maxEnterAttempts)
             .arg(m_maxEnterAttempts * m_enterTimeoutMs));
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
