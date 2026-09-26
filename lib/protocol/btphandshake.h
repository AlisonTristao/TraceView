#pragma once

#include <QByteArray>
#include <QObject>

namespace traceview {

// What is left of the serial console handshake. There is no ENTER/READY
// exchange any more: every transport, serial included, opens with HELLO
// straight away (BtpBackend::onTransportConnectionChanged()), and HELLO/
// HELLO_RESULT itself is btp::Node::connect() in BtpBackend.
//
// This class only watches for the dongle's own "BTP/1 CONSOLE\r\n" line --
// printed in the clear whenever it drops back to console (its inactivity
// watchdog, a SESSION_CLOSE, a bench human) -- and only once the CALLER says
// the session is established (onSessionEstablished()): it does not know when
// HELLO_RESULT succeeded, since it does not negotiate it.
//
// feedRawBytes() must see every byte off the wire, in parallel with
// BtpSession::feedBytes() -- this class only cares about plain-text lines;
// bytes that are actually COBS framing are harmless noise here
// (fragmentation-and-transports.md 3.2: nothing before the first 0x00
// delimiter is BTP, so BtpSession's own decoder already discards the
// CONSOLE text on its side).
class BtpHandshake : public QObject {
    Q_OBJECT

public:
    explicit BtpHandshake(QObject* parent = nullptr);

public slots:
    // Feeds raw bytes exactly as they arrived off the wire (same contract as
    // BtpSession::feedBytes()).
    void feedRawBytes(const QByteArray& data);

    // The caller's own HELLO/HELLO_RESULT exchange (driven by btp::Node::
    // connect(), outside this class) succeeded -- start watching for the
    // dongle's "BTP/1 CONSOLE" line, the only thing left this class can
    // notice about the session ending underneath it.
    void onSessionEstablished();

    // The session ended some other way (transport closed, a fresh HELLO about
    // to go out) -- stop watching for CONSOLE. Idempotent; safe to call even
    // if onSessionEstablished() was never reached.
    void onSessionLost();

signals:
    // The dongle's own "BTP/1 CONSOLE\r\n" line arrived while
    // onSessionEstablished() was in effect -- proof the session (HELLO
    // included) that this class knows nothing about the details of just
    // ended on the dongle's own initiative or in answer to our own
    // SESSION_CLOSE; the caller (BtpBackend) already knows which of those it
    // is (m_sessionClosing) and reacts accordingly.
    void consoleLineDetected();

private:
    bool m_established = false;
    // Bounded scratch buffer used only once established: the dongle prints
    // "BTP/1 CONSOLE\r\n" in the clear whenever it drops the session back to
    // console. The transport stays up and BtpSession has no watchdog, so
    // this raw-byte scan is the only thing on the desktop that can notice --
    // see feedRawBytes().
    QByteArray m_consoleWatchBuffer;
};

}  // namespace traceview
