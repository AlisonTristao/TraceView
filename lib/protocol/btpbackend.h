#pragma once

#include <QByteArray>
#include <btp/codec.hpp>
#include <btp/node.hpp>
#include <optional>

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
// (dispatch by MessageType), BtpHandshake (ENTER/READY link handshake),
// btp::Node (HELLO/HELLO_RESULT session negotiation + COMMAND_REQUEST/RESULT
// client wire mechanics -- library 2.14.0+, see the class's own comment
// below), ManifestClient (MANIFEST_DATA -> TelemetryCatalog),
// TelemetryFieldRouter (schema decode, fan out by field) and
// SubscriptionManager (SUBSCRIBE/UNSUBSCRIBE aggregation, topico 17). Owns
// and wires all of them internally -- this used to be inline in
// MainWindow::MainWindow() before the Backend interface existed; see
// Backend (backend/backend.h) for the contract this implements.
//
// btp::Node adoption here is deliberately narrow, not "everything Node could
// do": ManifestClient / SubscriptionManager keep their own wire mechanics.
// ManifestClient because TelemetryCatalog holds MANY sources' schemas at
// once (a hub enumerates every robot it knows) while btp::Catalog / learn_
// catalog() model exactly ONE peer's catalogue -- the mismatch is the same
// "hub aggregator is out of Node's per-node model, layer on top" boundary
// docs/library.md 16.9 already draws for subscriptions. SubscriptionManager
// because it aggregates several widgets into one wire SUBSCRIBE per (source,
// topic) and can re-send a DIFFERENT rate for a topic that already has one
// in flight -- btp::SubscriptionClient's one-slot-per-subscribe()-call model
// has no "change this pending request's rate in place" operation to express
// that with. Both keep sending through BtpSession directly, and their
// SUBSCRIBE_RESULT / MANIFEST_DATA still arrive the ordinary way
// (BtpSession::frameReceived -> onSessionFrameReceived -> ProtocolRouter),
// completely unaffected by Node.
//
// What DOES fit Node's per-peer model cleanly, and is what m_node is for:
//   - the console session's HELLO handshake (connect() -- one peer, the
//     dongle; a hub child never HELLOs at all, see setHubEndpoint());
//   - COMMAND_REQUEST/RESULT (CommandClient -- always exactly one target,
//     one outstanding request at a time, already the model
//     btp::CommandClient assumes);
//   - the ONE outgoing sequence counter every sealed message this backend
//     originates must share (m_node->endpoint(), reached through
//     nextEndpointSequence() -- unchanged call sites in SubscriptionManager
///    and sendTerminalIn()).
//
// m_node sees every decoded frame TWICE: once through BtpSession's own
// reassembly (frameReceived, feeding the untouched ManifestClient/
// SubscriptionManager/ProtocolRouter path), and once, per physical fragment,
// through onRawFrameForNode() (fed by BtpSession::frameBytesReceived,
// re-decoded and handed to btp::Node::receive(const DecodedFrame&) -- BTP
// 2.35.0). The second pass is a deliberately redundant SHADOW pipeline: m_node
// has no catalog/subscriptions attached, so every frame type but HELLO_RESULT
// / COMMAND_RESULT (and, for a hub-child target, its own outgoing traffic)
// comes back NodeRx::Complete/DroppedFrame and is silently ignored there --
// nothing downstream of it changes. The redundancy (decoding twice, a second
// small reassembly pool) is deliberately paid to keep the existing, already-
// correct BtpSession pipeline completely untouched rather than threading
// m_node into it.
class BtpBackend : public Backend, public btp::NodeConfig {
    Q_OBJECT

public:
    // `framing`/`encodeProfile` are the two axes the underlying BtpSession
    // is built on -- how frames are delimited on the link, and which profile
    // ceiling they are encoded under (see btpsession.h; they do not always
    // coincide). DeviceConnection supplies both from the Device's own
    // TransportType (devices/device.h), converted there since
    // traceview_devices can't depend on btp::codec directly.
    BtpBackend(BtpSession::Framing framing, const btp::TransportLimits& encodeProfile,
               QObject* parent = nullptr);
    // Convenience overload for the profiles whose framing is implied
    // (Serial/COBS, UsbHid/pre-framed) -- same reasoning as BtpSession's own
    // convenience constructor: existing call sites keep working unchanged.
    explicit BtpBackend(const btp::TransportLimits& transport = btp::kSerialTransport,
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
    // Must be called BEFORE the first onTransportConnectionChanged(true) --
    // m_node->begin() (which latches the wire identity into btp::Endpoint,
    // library-side, once and for this object's whole life -- see the class
    // comment) runs on that first connect and reads m_terminalSourceId/
    // m_terminalBootId as they stand at that moment. Every existing caller
    // already configures a hub child before opening its transport.
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

    // Hub child only (0 otherwise, and 0 until the first one): the wall-clock
    // ms (QDateTime::currentMSecsSinceEpoch) at which a frame that genuinely
    // originated at the ROBOT was last accepted here -- a channel-B frame that
    // passed AEAD open, or, on an unkeyed child, a forwarded data frame.
    // MainWindow::reconcileHubChildPresence polls this as the end-to-end "the
    // robot is really talking to us" signal, the one that does not depend on
    // the dongle's second-hand hub.peers view. The hub's own plaintext control
    // answers (MANIFEST_DATA / SUBSCRIBE_RESULT, served from its cache) do NOT
    // count: they prove the dongle is reachable, not the robot.
    qint64 lastPeerDataFrameMsSinceEpoch() const {
        return m_lastPeerDataFrameMs;
    }

    // ---- btp::NodeConfig -- m_node's own dependencies, see the class comment ----
    bool send(const std::uint8_t* frame, std::size_t frame_size) override;
    bool has_seal() const noexcept override;
    bool seal(const btp::Header& header, std::uint16_t payload_size,
              const std::uint8_t* plaintext, std::uint8_t* out) override;
    bool has_open() const noexcept override;
    bool open(const btp::Header& header, std::uint16_t sealed_size,
              const std::uint8_t* sealed, std::uint8_t* out_plaintext) override;

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

    // Every BTP frame this backend's session decodes (Inbound) or emits
    // (Outbound), plus the reason for any frame that would not decode/
    // reassemble -- for the BTP traffic monitor (lib/diagnostics/framelog.h).
    // Inbound frames are handed over BEFORE onSessionFrameReceived() opens the
    // AEAD seal, so a channel-B payload is still ciphertext here: that is the
    // point, the monitor shows the raw frame and offers to open it with a
    // typed password. Nothing on the protocol path consumes these; with no
    // monitor attached they have no receiver.
    void frameObserved(traceview::FrameDirection direction, const traceview::BtpFrame& frame);
    void frameDecodeFailed(const QString& reason);

public slots:
    void feedBytes(const QByteArray& data) override;
    void onTransportConnectionChanged(bool connected) override;
    void sendTerminalIn(const QByteArray& bytes) override;
    void sendCommand(const QByteArray& text) override;
    void onPeerPresence(bool online, quint32 bootId) override;

private:
    // Type of m_node -- see the class comment for what it does and does not
    // manage. Sized for m_node's own SHADOW reassembly (HELLO_RESULT /
    // COMMAND_RESULT are small; anything bigger that lands here -- a big
    // MANIFEST_DATA on the same console link, say -- simply does not fit and
    // is dropped by m_node alone, harmlessly: BtpSession's own, separately-
    // sized reassembly is what actually delivers it). No catalog/
    // subscriptions/served-commands: those axes are never enabled here.
    using BackendNode =
        btp::StaticNode</*Slots=*/2, /*SlotBytes=*/4096, /*SealBytes=*/1024,
                        /*ScratchBytes=*/16, /*CatalogTopics=*/1, /*CatalogFields=*/1,
                        /*CatalogStringBytes=*/16, /*MaxSubscriptions=*/1,
                        /*MaxCommands=*/2, /*CommandBytes=*/16,
                        /*CatalogSourceInfo=*/0>;
    static constexpr std::uint64_t kNodeReassemblyTimeoutMs = 4000U;
    // How long connect() waits for HELLO_RESULT -- matches the old
    // BtpHandshake kHelloTimeoutMs (spec requires it within 2000ms, a little
    // slack).
    static constexpr std::uint64_t kHelloTimeoutMs = 3000U;
    // Drives m_node->tick() (connection watchdog, command timeout) even when
    // no bytes are arriving to piggyback it on.
    static constexpr int kNodeTickIntervalMs = 250;

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
    // different sealed messages must never draw the same value. Reserved
    // through m_node->endpoint() -- the SAME btp::Endpoint m_node->command()
    // itself draws from for COMMAND_REQUEST, so the two can never collide.
    quint32 nextEndpointSequence();
    // Sends one benign frame to refresh the dongle's inactivity watchdog
    // (topico 35 B.1). Fired by m_keepaliveTimer only after that whole
    // interval passed with no other frame going out -- real traffic (chart
    // subscriptions renewing, commands, terminal) restarts the timer, so an
    // active session never pays for this.
    void sendSessionKeepalive();

    // m_node's shadow-pipeline feed -- see the class comment. Re-decodes one
    // already-validated fragment (BtpSession validated it once already; this
    // is a second, cheap, deterministic decode, not a re-encode) and hands it
    // to m_node->receive(). Reacts to the two outcomes m_node manages:
    // InitiatorHandled (the HELLO handshake) and CommandHandled.
    void onRawFrameForNode(quint32 sourceId, const QByteArray& raw);
    // BtpHandshake::readyForHello() -- builds the same btp::Hello this
    // backend has always advertised and drives it through m_node->connect().
    void onReadyForHello();
    void onNodeConnected();
    void onNodeConnectFailed(const QString& reason);
    // BtpHandshake::consoleLineDetected() -- the dongle dropped back to
    // console; treated as session loss unless this backend asked for it
    // (m_sessionClosing).
    void onConsoleLineDetected();
    // Drives m_node->tick() on a steady cadence and polls for a command
    // timeout tick() alone cannot push a NodeRx for (see the class comment
    // on CommandClient::notifyOutcome()).
    void onNodeTick();

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

    // m_node holds `*this` (the NodeConfig above) by reference -- see
    // btp::Node's own comment on why that is safe for a member declared
    // after the object it points at. std::optional because the FINAL wire
    // identity (setHubEndpoint(), if this becomes a child) is only known
    // after construction; emplaced once, in the constructor body, once
    // source_id/boot_id/transport (the NodeConfig base fields) are set.
    std::optional<BackendNode> m_node;
    // begin() (btp::Endpoint::configure(), library-side) latches the wire
    // identity and resets the sequence counter to 1 -- exactly ONCE for this
    // object's whole life, on the first connect, guarded by this. Calling it
    // again on a later reconnect (m_terminalBootId stays fixed) would reissue
    // sequence numbers a sealed message already used under the same
    // (source_id, boot_id) -- an AEAD nonce reuse.
    bool m_nodeBegun = false;
    QTimer* m_nodeTickTimer = nullptr;

    // Minimal, self-contained BTP identity for TERMINAL_IN frames only --
    // there is no HELLO/MANIFEST exchange for it to learn one from (moved
    // here from the old SerialWidgetBridge::sendTerminalIn(), topico 19).
    // Random, non-zero, generated once per BtpBackend lifetime. Overwritten
    // by setHubEndpoint() with the STABLE hub identity, at which point it
    // also becomes the identity every other sealed send
    // (SubscriptionManager, CommandClient, m_node) uses -- see
    // setHubEndpoint()'s comment.
    quint32 m_terminalSourceId;
    quint32 m_terminalBootId;
    quint32 m_endpointSequence = 0;  // fallback only -- see nextEndpointSequence()

    // Session keepalive (topico 35 B.1). Console-facing backend only: a child
    // backend never establishes a console session so the timer never starts.
    QTimer* m_keepaliveTimer = nullptr;
    bool m_sessionEstablished = false;
    // TERMINAL_IN, keepalive and SESSION_CLOSE all use m_terminalSourceId /
    // m_terminalBootId when UNSEALED (the console path -- cleartext, so a
    // shared sequence space is not a nonce concern there); this is that
    // unsealed path's own counter, independent of nextEndpointSequence()'s
    // sealed one.
    quint32 m_sessionSequence = 0;
    bool m_sessionClosing = false;

    // Both zero for the console-facing backend; both set for a child. Zero is
    // the "not a child" test rather than a separate flag, because BTP reserves
    // source_id 0 and so neither can legitimately be zero for a real endpoint.
    quint32 m_selfSourceId = 0;
    quint32 m_peerSourceId = 0;
    QByteArray m_endpointKey;

    // Whoever this backend last handshook with: the dongle, for the console
    // backend (m_peerSourceId == 0). Set from onNodeConnected(). Lets the
    // ManifestClient::sourceInfoReported handler tell "the dongle's own
    // source_info" apart from a robot's that merely passed through the
    // target=0 enumeration -- the same job m_peerSourceId does for a child.
    quint32 m_sessionPeerSourceId = 0;

    // One-shot latch so the "unsealed frame on a sealed hub channel" warning
    // fires once, not once per dropped telemetry sample. Cleared whenever the
    // link comes back up (onTransportConnectionChanged) so a genuinely new
    // problem is reported again.
    bool m_unsealedDowngradeReported = false;

    // Hub child only. requestCatalogFor() is one-shot on connect, but the
    // dongle answers from a cache that is empty until it has itself primed the
    // robot's manifest -- so a child that connects first (common right after a
    // dongle/robot reboot) gets one NOT_FOUND and would sit catalog-less
    // forever. This re-asks on a slow tick until sourceDescribed() fires for
    // m_peerSourceId; stopped on that, and on disconnect.
    QTimer* m_childCatalogRetryTimer = nullptr;
    bool m_childCatalogReceived = false;

    // Hub child only, fed by onPeerPresence() from MainWindow's reconcile.
    // m_peerOnline drives only the "waiting vs failed" status text now (the
    // card dot is driven end-to-end, see m_lastPeerDataFrameMs);
    // m_childPeerBootId is the boot the last applied MANIFEST_DATA described
    // (0 = none yet) and is what a reboot is detected against -- see the
    // ManifestClient::sourceDescribed handler.
    bool m_peerOnline = true;
    quint32 m_childPeerBootId = 0;
    // Hub child only: wall-clock ms of the last frame accepted here that came
    // from the robot itself (AEAD-opened, or forwarded on an unkeyed child).
    // 0 = none this link. Exposed via lastPeerDataFrameMsSinceEpoch(); reset
    // in onTransportConnectionChanged().
    qint64 m_lastPeerDataFrameMs = 0;
    // Rate-limits the boot-change-triggered catalog re-request in
    // onPeerPresence(), which is fed by a 1 Hz reconcile.
    qint64 m_lastPresenceCatalogRequestMs = 0;
    // MANIFEST_REQUESTs sent since the last time the child's catalog arrived,
    // for the "still waiting" vs "catalog is not coming" distinction the
    // status bar draws while the robot is online.
    int m_childCatalogAttempts = 0;
};

}  // namespace traceview
