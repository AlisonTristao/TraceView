#include "protocol/btphandshake.h"

namespace traceview {

namespace {
constexpr int kMaxLineBufferBytes = 512;
}  // namespace

BtpHandshake::BtpHandshake(QObject* parent) : QObject(parent) {}

void BtpHandshake::feedRawBytes(const QByteArray& data) {
    if (!m_established) {
        return;
    }
    // "BTP/1 CONSOLE\r\n" is what the dongle emits, in the clear and after
    // its last frame, whenever it leaves protocol mode -- inactivity
    // watchdog, SESSION_CLOSE, or a human typing at the bench. Nothing else
    // here would catch it: the transport is still open and BtpSession has no
    // watchdog.
    static const QByteArray kConsoleLine = QByteArrayLiteral("BTP/1 CONSOLE\r\n");
    m_consoleWatchBuffer.append(data);
    const bool seen = m_consoleWatchBuffer.contains(kConsoleLine);
    if (m_consoleWatchBuffer.size() > kMaxLineBufferBytes) {
        m_consoleWatchBuffer.remove(0, m_consoleWatchBuffer.size() - kMaxLineBufferBytes);
    }
    if (seen) {
        m_consoleWatchBuffer.clear();
        m_established = false;
        emit consoleLineDetected();
    }
}

void BtpHandshake::onSessionEstablished() { m_established = true; }

void BtpHandshake::onSessionLost() {
    m_established = false;
    m_consoleWatchBuffer.clear();
}

}  // namespace traceview
