#pragma once

#include <QByteArray>
#include <btp/codec.hpp>

#include "backend/backend.h"
// Not forward-declarable: BtpSession::Framing appears in the constructor
// signature below (BtpSession itself is still only held by pointer).
#include "protocol/btpsession.h"

class QTimer;

namespace traceview {

class ProtocolRouter;
class TelemetryCatalog;
class TelemetryFieldRouter;
class BtpHandshake;
class ManifestClient;
class SubscriptionManager;
class ClockSync;
class CommandClient;
class HubBinder;
struct BtpFrame;

// Backend implementation backed by the BTP v1 client stack: BtpSession
// (COBS decode + envelope/CRC validation + reassembly), ProtocolRouter
// (dispatch by MessageType), BtpHandshake (ENTER/READY + HELLO/HELLO_RESULT
// session negotiation), ManifestClient (MANIFEST_DATA -> TelemetryCatalog),
// TelemetryFieldRouter (schema decode, fan out by field) and
// SubscriptionManager (SUBSCRIBE/UNSUBSCRIBE aggregation, topico 17). Owns
// and wires all of them internally -- this used to be inline in
// MainWindow::MainWindow() before the Backend interface existed; see
// Backend (backend/backend.h) for the contract this implements.
class BtpBackend : public Backend {
    Q_OBJECT

public:
    // `framing`/`encodeProfile` are the two axes the underlying BtpSession
    // is built on -- how frames are delimited on the link, and which profile
    // ceiling they are encoded under (see btpsession.h; they do not always
    // coincide). DeviceConnection supplies both from the Device's own
    // TransportType (devices/device.h), converted there since
    // traceview_devices can't depend on btp::codec directly.
    BtpBackend(BtpSession::Framing framing, btp::TransportProfile encodeProfile,
               QObject* parent = nullptr);
    // Convenience overload for the profiles whose framing is implied
    // (Serial/COBS, UsbHid/pre-framed) -- same reasoning as BtpSession's own
    // convenience constructor: existing call sites keep working unchanged.
    explicit BtpBackend(btp::TransportProfile transport = btp::TransportProfile::Serial,
                        QObject* parent = nullptr);
    // Declared (rather than left implicit) because m_telemetryCatalog is a
    // plain (non-QObject) heap object this class owns and frees itself.
    ~BtpBackend() override;

    quint64 addSubscriber(quint32 sourceId, quint16 topicId, quint32 requestedRateMillihz) override;
    quint64 updateSubscriber(quint64 handle, quint32 sourceId, quint16 topicId,
                             quint32 requestedRateMillihz) override;
    void removeSubscriber(quint64 handle) override;
    QVector<TopicSubscriptionState> subscriptions() const override;
    QVector<StatusTopicRecord> topicStatuses() const override;
    QVector<CatalogTopicInfo> catalogTopics() const override;

    // --- Hub role: carrying another device's traffic (topico 26) ---------
    //
    // These two are what makes this backend usable as a PARENT: a dongle that
    // relays for robots behind its radio. A HubTransport (core/hubtransport.h)
    // attaches to them, one per child device, and each child claims only the
    // frames whose source_id is its own robot's.
    //
    // Deliberately here and not on Backend: a hub channel is a BTP concept
    // (it is BTP frames being multiplexed by BTP source_id), so a transport
    // that needs it asks for a BtpBackend by name rather than widening the
    // protocol-agnostic interface with something only one protocol can mean.

    // Sends a child's already-encoded frame out over this backend's link,
    // adding only this link's framing. Never re-encodes and never recomputes
    // a CRC -- see BtpSession::sendRawFrame(), whose contract this forwards.
    bool sendChildFrame(const QByteArray& alreadyEncoded);

    // --- Endpoint role: BEING a device behind a hub (topico 28) -----------
    //
    // Turns this backend into a child: one that talks end to end with a single
    // robot through a hub, rather than with whatever sits at the end of a
    // cable. Call before connecting; a backend that is never told this stays
    // exactly what it was, which is why every existing call site is unchanged.
    //
    // `selfSourceId` is this device's own identity on the wire and must be
    // STABLE across runs -- see hubChannelSourceId() in devices/device.h for
    // why. `peerSourceId` is the robot this child addresses.
    //
    // Being a child changes three things, and each has a reason:
    //
    //  - No HELLO, and no ENTER. Those negotiate a console session, and a
    //    robot does not offer one: bally_OS accepts exactly three CONTROL
    //    object_ids over the radio (MANIFEST_REQUEST, SUBSCRIBE, UNSUBSCRIBE)
    //    and HELLO is not among them, so a handshake here would be answered by
    //    silence and the connection would never come up.
    //  - Everything the handshake used to trigger therefore hangs off the
    //    transport coming up instead: ask the robot for its manifest, resume
    //    subscriptions. There is no session to establish, so there is nothing
    //    to wait for.
    //  - No clock sync. That exchange is a "dongle set_clock" shell command
    //    addressed to a hub; sending it to a robot would be a command the
    //    robot has no reason to accept.
    //
    // `endpointKey` is the derived channel-B key (keyderivation.h's
    // deriveChannelKey(Device::peerPassword)) -- empty means "hub, but no
    // key configured yet": every sealed send (SUBSCRIBE/UNSUBSCRIBE via
    // SubscriptionManager, COMMAND_REQUEST via CommandClient) then refuses
    // rather than transmit in the clear, and every received frame with
    // ENCRYPTED set is dropped rather than forwarded unauthenticated (see
    // onSessionFrameReceived()).
    void setHubEndpoint(quint32 selfSourceId, quint32 peerSourceId, const QByteArray& endpointKey);

    // Declares, to the DONGLE this backend is connected to, that a child
    // device with `childSourceId` speaks to the robot `peerSourceId`. Called
    // on the PARENT's backend, never on the child's -- see HubBinder for why
    // the hub has to be told at all, and why re-declaring is the normal path
    // rather than an error path.
    void bindHubChild(quint32 childSourceId, quint32 peerSourceId);
    void unbindHubChild(quint32 childSourceId);

    // Best-effort graceful teardown for the console-facing serial session.
    // Emits CONTROL/SESSION_CLOSE (CLIENT_SHUTDOWN) exactly once while a
    // session is established; the owner must drain the transport's pending
    // writes before physically closing it. Returns true only when a frame was
    // emitted. Hub-channel children have no console session and return false.
    bool requestSessionClose();

    // Zero unless setHubEndpoint() made this a child.
    quint32 peerSourceId() const {
        return m_peerSourceId;
    }

signals:
    // Hands over one received frame's octets, exactly as they came off the
    // wire, tagged with the source_id already parsed out of its header.
    //
    // Raw octets rather than a decoded BtpFrame because the child decodes for
    // itself: it has its own BtpSession, under its own profile, and giving it
    // a parsed frame would force it to re-serialize to decode again -- which
    // would recompute a CRC over a re-encoded header whose identity triple is
    // the AEAD nonce of a payload this end has no key for.
    //
    // Fires once per FRAGMENT, before this session's own reassembly: the hub
    // does not reassemble, the endpoint does (the child's BtpSession has its
    // own btp::Reassembler).
    void hubFrameBytesReceived(quint32 sourceId, const QByteArray& raw);

public slots:
    void feedBytes(const QByteArray& data) override;
    void onTransportConnectionChanged(bool connected) override;
    void sendTerminalIn(const QByteArray& bytes) override;
    void sendCommand(const QByteArray& text) override;

private:
    void onTerminalFrameReceived(const traceview::BtpFrame& frame);
    // Replaces the direct BtpSession::frameReceived -> ProtocolRouter::
    // onFrameReceived connection: opens a sealed (ENCRYPTED) frame under
    // m_endpointKey before forwarding, drops it if opening fails or no key
    // is configured. On a hub child with a key configured (m_peerSourceId !=
    // 0 && key set) an UNSEALED frame is also dropped: the robot now seals
    // everything it originates on channel B -- TELEMETRY and LOG included --
    // and STATUS, the one thing it leaves unsealed, rides channel C and is
    // consumed by the dongle, never relayed here. An unsealed frame on this
    // path is therefore a downgrade or a spoof. The console backend
    // (m_peerSourceId == 0, no key) still forwards the dongle's cleartext
    // channel-A traffic unchanged.
    void onSessionFrameReceived(const traceview::BtpFrame& frame);
    // The one sequence counter every sealed message this backend originates
    // shares -- see CommandClient::configure()'s comment on why two
    // different sealed messages must never draw the same value.
    quint32 nextEndpointSequence();
    // Sends one benign frame to refresh the dongle's inactivity watchdog
    // (topico 35 B.1). Fired by m_keepaliveTimer only after that whole
    // interval passed with no other frame going out -- real traffic (chart
    // subscriptions renewing, commands, terminal) restarts the timer, so an
    // active session never pays for this.
    void sendSessionKeepalive();

    BtpSession* m_btpSession;
    ProtocolRouter* m_protocolRouter;
    TelemetryCatalog* m_telemetryCatalog;
    TelemetryFieldRouter* m_telemetryFieldRouter;
    BtpHandshake* m_btpHandshake;
    ManifestClient* m_manifestClient;
    SubscriptionManager* m_subscriptionManager;
    ClockSync* m_clockSync;
    CommandClient* m_commandClient;
    // Parent-side only: a child backend never binds anything, it IS the
    // thing being bound. Held here rather than in MainWindow because the
    // bindings have to be re-issued on every new session, and this is
    // where sessionEstablished already lands.
    HubBinder* m_hubBinder;

    // Minimal, self-contained BTP identity for TERMINAL_IN frames only --
    // there is no HELLO/MANIFEST exchange for it to learn one from (moved
    // here from the old SerialWidgetBridge::sendTerminalIn(), topico 19).
    // Random, non-zero, generated once per BtpBackend lifetime. Overwritten
    // by setHubEndpoint() with the STABLE hub identity, at which point it
    // also becomes the identity every other sealed send
    // (SubscriptionManager, CommandClient) uses -- see setHubEndpoint()'s
    // comment.
    quint32 m_terminalSourceId;
    quint32 m_terminalBootId;
    quint32 m_endpointSequence = 0;

    // Session keepalive (topico 35 B.1). Console-facing backend only: a child
    // backend never establishes a console session so the timer never starts.
    QTimer* m_keepaliveTimer = nullptr;
    bool m_sessionEstablished = false;
    // TERMINAL_IN, keepalive and SESSION_CLOSE all use m_terminalSourceId /
    // m_terminalBootId, so they must also share one sequence space. Reusing a
    // sequence with the same producer identity would violate BTP's logical
    // message identity even when the payloads have different object_ids.
    quint32 m_sessionSequence = 0;
    bool m_sessionClosing = false;

    // Both zero for the console-facing backend; both set for a child. Zero is
    // the "not a child" test rather than a separate flag, because BTP reserves
    // source_id 0 and so neither can legitimately be zero for a real endpoint.
    quint32 m_selfSourceId = 0;
    quint32 m_peerSourceId = 0;
    QByteArray m_endpointKey;

    // One-shot latch so the "unsealed frame on a sealed hub channel" warning
    // fires once, not once per dropped telemetry sample. Cleared whenever the
    // link comes back up (onTransportConnectionChanged) so a genuinely new
    // problem is reported again.
    bool m_unsealedDowngradeReported = false;
};

}  // namespace traceview
