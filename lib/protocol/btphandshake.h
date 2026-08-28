#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <cstdint>

namespace traceview {

class BtpSession;
class ProtocolRouter;
struct BtpFrame;

// Drives the console -> protocolled handshake session-and-terminal.md
// section 3 describes (the plain-ASCII "BTP/1 ENTER <nonce>" / "BTP/1 READY
// <nonce>" exchange) and the HELLO/HELLO_RESULT negotiation
// session-and-terminal.md sections 1-2 describe, on top of an already-open
// serial connection.
// Deliberately left out of topico 14's BtpSession (framing only, no session
// concept) and topico 19's terminal work; topico 15 ("fatia vertical de
// telemetria binaria") is the first topico that needs a live session against
// real hardware to validate against, so this is where it lands.
//
// feedRawBytes() must see every byte off the wire, in parallel with
// BtpSession::feedBytes() -- this class only cares about the plain-text
// READY line that precedes any BTP framing; bytes that are actually COBS
// framing are harmless noise here (fragmentation-and-transports.md 3.2:
// nothing before the
// first 0x00 delimiter is BTP, so BtpSession's own decoder already discards
// the ENTER/READY text on its side).
class BtpHandshake : public QObject {
    Q_OBJECT

public:
    // The two retry knobs default to the production policy (see the
    // constants at the top of the .cpp for why that budget is what it is).
    // They are parameters rather than hardcoded so a test can exercise the
    // whole retry path in milliseconds instead of the 20 real seconds the
    // shipped policy takes -- a test that slow would either be skipped or
    // would quietly stop covering the case.
    explicit BtpHandshake(BtpSession* session, ProtocolRouter* router, QObject* parent = nullptr,
                          int enterTimeoutMs = -1, int maxEnterAttempts = -1);

public slots:
    // Starts a fresh handshake: generates a new ENTER nonce and client
    // identity, emits bytesToWrite() with the ENTER line. Call once per
    // fresh connection, after BtpSession::reset().
    void start();

    // Feeds raw bytes exactly as they arrived off the wire (same contract as
    // BtpSession::feedBytes()).
    void feedRawBytes(const QByteArray& data);

signals:
    // Text to write straight to the transport -- only used for the ENTER
    // line; the HELLO frame itself goes out through BtpSession::sendFrame(),
    // whose own bytesToWrite() is already wired to the transport.
    void bytesToWrite(const QByteArray& data);
    // peerSourceId/peerBootId are the connected dongle's own identity, taken
    // straight from the HELLO_RESULT frame's own header (every frame the
    // dongle originates is tagged with its own BtpTransport source_id/
    // boot_id, HELLO_RESULT included) -- ClockSync needs this as
    // COMMAND_REQUEST's target_source_id/target_boot_id, the same pair
    // SerialMux::handleCommandRequest on the firmware side checks a request
    // against.
    // peerConfigRevision is HELLO_RESULT's own config_revision field
    // (session-and-terminal.md section 2, offset 48) -- the dongle's
    // manifest-catalog revision, topico 16 PASSO 5/6. ManifestClient uses it
    // to skip a redundant full target=0 re-enumeration when reconnecting to
    // the same dongle catalog it already has cached.
    // selectedVersion is HELLO_RESULT's own field (offset 13) -- the BTP
    // envelope version this session actually negotiated (see
    // session-and-terminal.md section 2: "the responder picks the highest
    // version it can use"). BtpBackend surfaces it as the
    // Device's reported btpVersion (devices/deviceconfigdialog.h) instead of
    // that field staying user-editable.
    void sessionEstablished(quint32 peerSourceId, quint32 peerBootId, quint32 peerConfigRevision,
                            quint8 selectedVersion);
    void sessionFailed(const QString& reason);

private slots:
    void onControlFrameReceived(const traceview::BtpFrame& frame);
    void onEnterTimeout();
    void onHelloTimeout();

private:
    enum class State { Idle, AwaitingReady, AwaitingHelloResult, Established };

    void sendHello();
    // Writes the ENTER line and arms m_enterTimer. Called by start() and again
    // by onEnterTimeout() for each retry -- deliberately reusing m_enterNonce
    // rather than drawing a new one, see its declaration below.
    void sendEnter();
    void fail(const QString& reason);

    BtpSession* m_session;
    State m_state = State::Idle;
    QByteArray m_lineBuffer;  // bounded scratch buffer while awaiting READY
    // Bounded scratch buffer used only once Established: the dongle prints
    // "BTP/1 CONSOLE\r\n" in the clear whenever it drops the session back to
    // console (its watchdog, a SESSION_CLOSE, a bench human). The transport
    // stays up and BtpSession has no watchdog, so this raw-byte scan is the
    // only thing on the desktop that can notice -- see feedRawBytes().
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
    QTimer m_helloTimer;
};

}  // namespace traceview
