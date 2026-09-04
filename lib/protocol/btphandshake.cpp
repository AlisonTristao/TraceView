#include "protocol/btphandshake.h"

#include <QRandomGenerator>

namespace traceview {

namespace {
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
constexpr int kMaxLineBufferBytes = 512;

}  // namespace

BtpHandshake::BtpHandshake(QObject* parent, int enterTimeoutMs, int maxEnterAttempts)
    : QObject(parent),
      m_enterTimeoutMs(enterTimeoutMs > 0 ? enterTimeoutMs : kEnterTimeoutMs),
      m_maxEnterAttempts(maxEnterAttempts > 0 ? maxEnterAttempts : kMaxEnterAttempts) {
    m_enterTimer.setSingleShot(true);
    connect(&m_enterTimer, &QTimer::timeout, this, &BtpHandshake::onEnterTimeout);
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
        // has no watchdog.
        static const QByteArray kConsoleLine = QByteArrayLiteral("BTP/1 CONSOLE\r\n");
        m_consoleWatchBuffer.append(data);
        const bool seen = m_consoleWatchBuffer.contains(kConsoleLine);
        if (m_consoleWatchBuffer.size() > kMaxLineBufferBytes) {
            m_consoleWatchBuffer.remove(0, m_consoleWatchBuffer.size() - kMaxLineBufferBytes);
        }
        if (seen) {
            m_consoleWatchBuffer.clear();
            m_state = State::Idle;
            emit consoleLineDetected();
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
        // The link-level handshake is done; the caller drives HELLO from
        // here (btp::Node::connect()) and tells us how it went via
        // onSessionEstablished() / onSessionLost().
        m_state = State::Idle;
        emit readyForHello();
    }
}

void BtpHandshake::onSessionEstablished() { m_state = State::Established; }

void BtpHandshake::onSessionLost() {
    m_state = State::Idle;
    m_consoleWatchBuffer.clear();
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
    m_state = State::Idle;
    emit enterFailed(tr("no BTP/1 READY after %1 attempts over %2 ms")
                         .arg(m_maxEnterAttempts)
                         .arg(m_maxEnterAttempts * m_enterTimeoutMs));
}

}  // namespace traceview
