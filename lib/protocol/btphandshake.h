#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <cstdint>

namespace traceview {

// Drives the console -> protocolled handshake session-and-terminal.md
// section 3 describes (the plain-ASCII "BTP/1 ENTER <nonce>" / "BTP/1 READY
// <nonce>" exchange), on top of an already-open serial connection.
//
// The HELLO/HELLO_RESULT negotiation session-and-terminal.md sections 1-2
// describe moved to BtpBackend, which drives it through btp::Node::connect()
// (BTP library 2.14.0+) -- this class no longer builds or sends HELLO, and no
// longer decodes HELLO_RESULT. What stays here is genuinely link framing, not
// BTP: the plain-ASCII ENTER/READY dance precedes any BTP framing at all
// (nothing before the first COBS delimiter is BTP), and its retry policy
// (kEnterTimeoutMs * kMaxEnterAttempts, same nonce reused across retries) is
// this ONE serial link's own workaround for a cold-booting dongle, not
// anything btp::Node models (it has no ENTER concept and no retry budget for
// its own connect() -- see docs/library.md 16.3).
//
// This class also watches for the dongle's own "BTP/1 CONSOLE\r\n" line --
// printed in the clear whenever it drops back to console (its inactivity
// watchdog, a SESSION_CLOSE, a bench human) -- but only once the CALLER says
// the session is established (onSessionEstablished()): BtpHandshake itself no
// longer knows when HELLO_RESULT succeeded, since it no longer negotiates it.
//
// feedRawBytes() must see every byte off the wire, in parallel with
// BtpSession::feedBytes() -- this class only cares about plain-text lines;
// bytes that are actually COBS framing are harmless noise here
// (fragmentation-and-transports.md 3.2: nothing before the first 0x00
// delimiter is BTP, so BtpSession's own decoder already discards the
// ENTER/READY text on its side).
class BtpHandshake : public QObject {
    Q_OBJECT

public:
    // The two retry knobs default to the production policy (see the
    // constants at the top of the .cpp for why that budget is what it is).
    // They are parameters rather than hardcoded so a test can exercise the
    // whole retry path in milliseconds instead of the 20 real seconds the
    // shipped policy takes -- a test that slow would either be skipped or
    // would quietly stop covering the case.
    explicit BtpHandshake(QObject* parent = nullptr, int enterTimeoutMs = -1,
                         int maxEnterAttempts = -1);

public slots:
    // Starts a fresh handshake: generates a new ENTER nonce, emits
    // bytesToWrite() with the ENTER line. Call once per fresh connection.
    void start();

    // Feeds raw bytes exactly as they arrived off the wire (same contract as
    // BtpSession::feedBytes()).
    void feedRawBytes(const QByteArray& data);

    // The caller's own HELLO/HELLO_RESULT exchange (driven by btp::Node::
    // connect(), outside this class) succeeded -- start watching for the
    // dongle's "BTP/1 CONSOLE" line, the only thing left this class can
    // notice about the session ending underneath it.
    void onSessionEstablished();

    // The session ended some other way (transport closed, a fresh
    // reconnect about to start()) -- stop watching for CONSOLE. Idempotent;
    // safe to call even if onSessionEstablished() was never reached.
    void onSessionLost();

signals:
    // Text to write straight to the transport -- the ENTER line, the only
    // thing this class still originates itself.
    void bytesToWrite(const QByteArray& data);

    // BTP/1 READY arrived: the link-level handshake is done and the caller
    // should now send HELLO (via btp::Node::connect()) and drive it to a
    // result. No nonce/identity carried -- READY's own nonce match already
    // happened here, and HELLO's identity is the caller's Node, independent
    // of this class's own ENTER nonce.
    void readyForHello();

    // The ENTER/READY retry budget was exhausted with no READY ever seen --
    // the link itself never came up, not a HELLO-level failure. Emitted at
    // most once per start().
    void enterFailed(const QString& reason);

    // The dongle's own "BTP/1 CONSOLE\r\n" line arrived while
    // onSessionEstablished() was in effect -- proof the session (HELLO
    // included) that this class knows nothing about the details of just
    // ended on the dongle's own initiative or in answer to our own
    // SESSION_CLOSE; the caller (BtpBackend) already knows which of those it
    // is (m_sessionClosing) and reacts accordingly.
    void consoleLineDetected();

private slots:
    void onEnterTimeout();

private:
    enum class State { Idle, AwaitingReady, Established };

    // Writes the ENTER line and arms m_enterTimer. Called by start() and again
    // by onEnterTimeout() for each retry -- deliberately reusing m_enterNonce
    // rather than drawing a new one, see its declaration below.
    void sendEnter();

    State m_state = State::Idle;
    QByteArray m_lineBuffer;  // bounded scratch buffer while awaiting READY
    // Bounded scratch buffer used only once Established: the dongle prints
    // "BTP/1 CONSOLE\r\n" in the clear whenever it drops the session back to
    // console. The transport stays up and BtpSession has no watchdog, so
    // this raw-byte scan is the only thing on the desktop that can notice --
    // see feedRawBytes().
    QByteArray m_consoleWatchBuffer;
    // The nonce of the CURRENT connection attempt, drawn once in start() and
    // held across every retry. Reused rather than redrawn on purpose: a READY
    // answering an earlier ENTER can arrive after that ENTER's timeout has
    // already fired (a cold-booting dongle is slow, not silent), and with a
    // fresh nonce per retry that reply would no longer match anything. The
    // handshake would then wait for a READY the dongle has no reason to send
    // again -- it has already left console mode -- and stall until the retry
    // budget ran out. One nonce per connection keeps every reply in play.
    QByteArray m_enterNonce;
    QByteArray m_expectedReady;
    // Retries burned so far on this connection; reset by start().
    int m_enterAttempts = 0;
    // Retry policy, from the constructor. Held per-instance rather than read
    // from the file-scope constants so a test can shrink the budget.
    int m_enterTimeoutMs;
    int m_maxEnterAttempts;
    QTimer m_enterTimer;
};

}  // namespace traceview
