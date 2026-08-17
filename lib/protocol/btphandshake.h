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

// Drives the console -> protocolled handshake TRANSPORT_SERIAL.md section 5
// describes (the plain-ASCII "BTP/1 ENTER <nonce>" / "BTP/1 READY <nonce>"
// exchange) and the HELLO/HELLO_RESULT negotiation COMMANDS_AND_ACTIONS.md
// section 5 describes, on top of an already-open serial connection.
// Deliberately left out of topico 14's BtpSession (framing only, no session
// concept) and topico 19's terminal work; topico 15 ("fatia vertical de
// telemetria binaria") is the first topico that needs a live session against
// real hardware to validate against, so this is where it lands.
//
// feedRawBytes() must see every byte off the wire, in parallel with
// BtpSession::feedBytes() -- this class only cares about the plain-text
// READY line that precedes any BTP framing; bytes that are actually COBS
// framing are harmless noise here (TRANSPORT_SERIAL.md: nothing before the
// first 0x00 delimiter is BTP, so BtpSession's own decoder already discards
// the ENTER/READY text on its side).
class BtpHandshake : public QObject {
    Q_OBJECT

public:
    explicit BtpHandshake(BtpSession* session, ProtocolRouter* router, QObject* parent = nullptr);

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
    // peerConfigRevision is HELLO_RESULT's own config_revision field
    // (COMMANDS_AND_ACTIONS.md section 5, offset 48) -- the dongle's
    // manifest-catalog revision, topico 16 PASSO 5/6. ManifestClient uses it
    // to skip a redundant full target=0 re-enumeration when reconnecting to
    // the same dongle catalog it already has cached.
    void sessionEstablished(quint32 peerConfigRevision);
    void sessionFailed(const QString& reason);

private slots:
    void onControlFrameReceived(const traceview::BtpFrame& frame);
    void onEnterTimeout();
    void onHelloTimeout();

private:
    enum class State { Idle, AwaitingReady, AwaitingHelloResult, Established };

    void sendHello();
    void fail(const QString& reason);

    BtpSession* m_session;
    State m_state = State::Idle;
    QByteArray m_lineBuffer;   // bounded scratch buffer while awaiting READY
    QByteArray m_expectedReady;
    QTimer m_enterTimer;
    QTimer m_helloTimer;
};

}  // namespace traceview
