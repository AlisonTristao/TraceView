#include "protocol/btpbackend.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btphandshake.h"
#include "protocol/btpsession.h"
#include "protocol/channelseal.h"
#include "protocol/clocksync.h"
#include "protocol/commandclient.h"
#include "protocol/hubbinder.h"
#include "protocol/manifestclient.h"
#include "protocol/protocolrouter.h"
#include "protocol/subscriptionmanager.h"
#include "protocol/telemetrycatalog.h"
#include "protocol/telemetryfieldrouter.h"

namespace traceview {

namespace {

// BTP/docs/commands.md section 1's `object_id` namespaces -- object_id
// values within MessageType::Terminal. Mirrors t_dongle_develop's
// SerialSession::kTerminalInObjectId/kTerminalOutObjectId (lib/SerialSession/
// SerialSession.h); each side of the wire defines its own copy of these
// normatively-fixed constants, same as topico 13 did on the dongle.
constexpr quint16 kTerminalInObjectId = 0x0001;
constexpr quint16 kTerminalOutObjectId = 0x0002;

// object_id for CONTROL/MANIFEST_REQUEST -- same local-copy convention as the
// terminal ids above (mirrors ManifestClient's own kControlManifestRequest).
constexpr quint16 kControlManifestRequest = 0x0003;
constexpr quint16 kControlSessionClose = 0x000A;

// session-and-terminal.md section 4.1. The dongle handles this request in
// its protocol loop immediately; 500 ms is the allowance for returning
// SESSION_CLOSE_RESULT and cleaning its queues before console ownership.
constexpr quint8 kCloseReasonClientShutdown = 0x02;
constexpr quint32 kCloseDrainTimeoutMs = 500;

// How long the session may sit with no PC->dongle frame before the keepalive
// steps in. Comfortably under the negotiated 30s watchdog (and still 3x under
// the 15s an un-updated dongle would negotiate), so a single lost keepalive
// is never fatal. See BtpBackend::sendSessionKeepalive().
constexpr int kKeepaliveIntervalMs = 5000;

// The keepalive is a MANIFEST_REQUEST for a source_id that cannot exist: the
// dongle answers a small NOT_FOUND that ManifestClient already discards
// without touching the catalog, so the exchange renews the watchdog and
// nothing else. 0 is not usable here -- it is the enumeration wildcard.
constexpr quint32 kKeepaliveSentinelSource = 0xFFFFFFFFu;

quint32 randomNonZero() {
    quint32 value = 0;
    while (value == 0) {
        value = QRandomGenerator::global()->generate();
    }
    return value;
}

QString formatRateMillihz(quint32 millihz) {
    return QString::number(millihz / 1000.0, 'g', 4) + " Hz";
}

// Human-readable mirrors of TELEMETRY.md's wire enums, for
// BtpBackend::catalogTopics() (CatalogTopicInfo::encoding/CatalogTopicField::
// type are display strings, not the wire codes -- see telemetry/
// catalogtopicinfo.h).
QString encodingLabel(TelemetryEncoding encoding) {
    switch (encoding) {
        case TelemetryEncoding::Invalid:
            return QStringLiteral("invalid");
        case TelemetryEncoding::OpaqueBytes:
            return QStringLiteral("OPAQUE_BYTES");
        case TelemetryEncoding::Utf8:
            return QStringLiteral("UTF8");
        case TelemetryEncoding::JsonUtf8:
            return QStringLiteral("JSON_UTF8");
        case TelemetryEncoding::CsvUtf8:
            return QStringLiteral("CSV_UTF8");
        case TelemetryEncoding::PackedLe:
            return QStringLiteral("PACKED_LE");
        case TelemetryEncoding::TlvLe:
            return QStringLiteral("TLV_LE");
    }
    return QStringLiteral("unknown");
}

QString fieldTypeLabel(TelemetryFieldType type) {
    switch (type) {
        case TelemetryFieldType::UInt8:
            return QStringLiteral("uint8");
        case TelemetryFieldType::UInt16:
            return QStringLiteral("uint16");
        case TelemetryFieldType::UInt32:
            return QStringLiteral("uint32");
        case TelemetryFieldType::UInt64:
            return QStringLiteral("uint64");
        case TelemetryFieldType::Int8:
            return QStringLiteral("int8");
        case TelemetryFieldType::Int16:
            return QStringLiteral("int16");
        case TelemetryFieldType::Int32:
            return QStringLiteral("int32");
        case TelemetryFieldType::Int64:
            return QStringLiteral("int64");
        case TelemetryFieldType::Float32:
            return QStringLiteral("float32");
        case TelemetryFieldType::Float64:
            return QStringLiteral("float64");
        case TelemetryFieldType::Bool:
            return QStringLiteral("bool");
        case TelemetryFieldType::Enum8:
            return QStringLiteral("enum8");
        case TelemetryFieldType::Enum16:
            return QStringLiteral("enum16");
    }
    return QStringLiteral("unknown");
}

CatalogTopicInfo toCatalogTopicInfo(const TelemetryTopicSchema& schema) {
    CatalogTopicInfo info;
    info.sourceId = schema.sourceId;
    info.topicId = schema.topicId;
    info.schemaVersion = schema.schemaVersion;
    info.name = schema.name;
    info.encoding = encodingLabel(schema.encoding);
    info.fields.reserve(schema.fields.size());
    for (const TelemetryFieldSchema& field : schema.fields) {
        CatalogTopicField out;
        out.fieldId = field.fieldId;
        out.name = field.name;
        out.type = fieldTypeLabel(field.type);
        if (field.elementCount != 1) {
            out.type += field.isVariableLength()
                            ? QStringLiteral("[..%1]").arg(field.maxElementCount)
                            : QStringLiteral("[%1]").arg(field.elementCount);
        }
        out.unit = field.unit;
        info.fields.append(out);
    }
    return info;
}

}  // namespace

BtpBackend::BtpBackend(btp::TransportProfile transport, QObject* parent)
    : BtpBackend(BtpSession::framingFor(transport), transport, parent) {}

BtpBackend::BtpBackend(BtpSession::Framing framing, btp::TransportProfile encodeProfile,
                       QObject* parent)
    : Backend(parent), m_terminalSourceId(randomNonZero()), m_terminalBootId(randomNonZero()) {
    // BTP v1 client stack (topico 14): raw bytes -> BtpSession (COBS decode,
    // or direct btp::decode() when the link is already pre-framed -- see
    // btpsession.h -- plus envelope/CRC validation and reassembly either
    // way) -> ProtocolRouter (dispatch by MessageType) ->
    // TelemetryFieldRouter (schema decode, fan out by field).
    // m_telemetryCatalog starts empty and is populated dynamically by
    // m_manifestClient's MANIFEST_REQUEST/MANIFEST_DATA exchange (topico 16)
    // -- nothing here assumes which source_id/schema is on the other end in
    // advance.
    m_btpSession = new BtpSession(framing, encodeProfile, this);
    m_protocolRouter = new ProtocolRouter(this);
    m_telemetryCatalog = new TelemetryCatalog();
    m_telemetryFieldRouter = new TelemetryFieldRouter(m_telemetryCatalog, this);
    m_btpHandshake = new BtpHandshake(m_btpSession, m_protocolRouter, this);
    m_manifestClient = new ManifestClient(m_btpSession, m_protocolRouter, m_telemetryCatalog, this);
    // No more boot-time "informe data/hora" prompt to wait on (StartupConfig,
    // removed): once a session is up, this asks the dongle's own clock and
    // corrects it over the same COMMAND_REQUEST channel a human's "dongle
    // set_clock" shell command already used.
    m_clockSync = new ClockSync(m_btpSession, m_protocolRouter, this);
    // topico 17: every open chart/gauge is a *consumer* of a topic; this is
    // what turns all of them into a single SUBSCRIBE per (source, topic) at
    // the highest rate any of them asked for, and into an UNSUBSCRIBE only
    // when the last one closes.
    m_subscriptionManager =
        new SubscriptionManager(m_btpSession, m_protocolRouter, m_telemetryCatalog, this);
    // Hub-channel control widgets (topico 28's missing half): a real
    // COMMAND_REQUEST/COMMAND_RESULT client, inert until setHubEndpoint()
    // configures a target -- see CommandClient's own header comment.
    m_commandClient = new CommandClient(m_btpSession, m_protocolRouter, this);
    connect(m_commandClient, &CommandClient::statusMessage, this, &Backend::statusMessage);
    // The parent half of the hub: tells the dongle which robot each child
    // device is for. Inert on a child backend, which never calls into it.
    m_hubBinder = new HubBinder(m_btpSession, m_protocolRouter, this);
    connect(m_hubBinder, &HubBinder::statusMessage, this, &Backend::statusMessage);

    connect(m_btpSession, &BtpSession::bytesToWrite, this, &Backend::bytesToWrite);
    // Hub role (topico 26): every frame this session decodes is also offered,
    // as raw octets, to whatever child devices are attached. Forwarded
    // unconditionally and with no filtering here -- which frames belong to
    // which child is decided by source_id, by each HubTransport, so this
    // backend needs to know nothing about how many children exist or whether
    // any does. With no child attached the signal simply has no receiver.
    connect(m_btpSession, &BtpSession::frameBytesReceived, this,
            &BtpBackend::hubFrameBytesReceived);
    // BtpHandshake's own outbound text (the ENTER line) goes out the same
    // way; the HELLO frame itself goes through BtpSession::sendFrame(),
    // whose bytesToWrite() is already connected above.
    connect(m_btpHandshake, &BtpHandshake::bytesToWrite, this, &Backend::bytesToWrite);

    // Session keepalive: a repeating timer restarted by every outbound BTP
    // frame (below), so it only fires when the link has actually been idle for
    // kKeepaliveIntervalMs. Started on sessionEstablished, stopped on failure
    // and on transport loss.
    m_keepaliveTimer = new QTimer(this);
    m_keepaliveTimer->setSingleShot(false);
    m_keepaliveTimer->setInterval(kKeepaliveIntervalMs);
    connect(m_keepaliveTimer, &QTimer::timeout, this, &BtpBackend::sendSessionKeepalive);
    connect(m_btpSession, &BtpSession::bytesToWrite, this, [this](const QByteArray&) {
        if (m_keepaliveTimer != nullptr && m_keepaliveTimer->isActive()) {
            m_keepaliveTimer->start();  // real traffic just went out; reset the idle clock
        }
    });

    connect(m_btpHandshake, &BtpHandshake::sessionEstablished, this,
            [this](quint32 peerSourceId, quint32 peerBootId, quint32 peerConfigRevision,
                   quint8 selectedVersion) {
                emit statusMessage(tr("BTP session established (HELLO_RESULT=SUCCESS)"), 5000);
                m_sessionClosing = false;
                m_sessionEstablished = true;
                m_keepaliveTimer->start();
                m_manifestClient->onSessionEstablished(peerConfigRevision);
                m_subscriptionManager->onSessionEstablished();
                m_clockSync->onSessionEstablished(peerSourceId, peerBootId);
                // Re-issued on EVERY session: HubRegistry's table is RAM-only
                // on the dongle, so a dongle reboot silently empties it while
                // this desktop still believes its children are routed.
                m_hubBinder->onSessionEstablished(peerSourceId, peerBootId);
                // Devices tab display only -- peerSourceId is the dongle's own
                // BTP identity (its HELLO_RESULT envelope's source_id), the
                // closest thing to a "device ID" this protocol reports today.
                emit deviceIdentified(
                    tr("BTP/%1").arg(selectedVersion),
                    QString("0x%1").arg(peerSourceId, 8, 16, QChar('0')).toUpper());
            });
    connect(m_btpHandshake, &BtpHandshake::sessionFailed, this, [this](const QString& reason) {
        m_keepaliveTimer->stop();
        m_sessionEstablished = false;
        if (m_sessionClosing) {
            // A successful SESSION_CLOSE ends with the dongle's plain-text
            // BTP/1 CONSOLE line. BtpHandshake deliberately reports that as
            // sessionFailed for unexpected drops; during an intentional
            // close it is confirmation, not a reason to recycle the port.
            m_sessionClosing = false;
            return;
        }
        emit statusMessage(tr("BTP handshake failed: %1").arg(reason), 8000);
        // BtpHandshake has already exhausted its own retries by the time it
        // says this, so the ENTER line is not what is missing any more --
        // recycling the port is the only escalation left. Without this the
        // status message above was the entire outcome: an 8-second toast,
        // then a device that stays "connected" and silent forever.
        emit sessionRecoveryNeeded();
    });
    connect(m_clockSync, &ClockSync::statusMessage, this, &Backend::statusMessage);
    // Not a direct connection to ProtocolRouter::onFrameReceived any more:
    // a sealed (ENCRYPTED) frame has to be opened first, and only this class
    // knows the endpoint key setHubEndpoint() configured -- see
    // onSessionFrameReceived()'s own comment.
    connect(m_btpSession, &BtpSession::frameReceived, this,
            &BtpBackend::onSessionFrameReceived);
    connect(m_protocolRouter, &ProtocolRouter::telemetrySampleReceived, m_telemetryFieldRouter,
            &TelemetryFieldRouter::onTelemetrySample);
    connect(m_telemetryFieldRouter, &TelemetryFieldRouter::fieldSample, this,
            &Backend::fieldSample);
    // topico 16 PASSO 9: a sample whose schema isn't in the catalog yet (or
    // no longer matches, after a schema change) triggers a targeted
    // manifest re-request instead of silently dropping forever.
    connect(m_telemetryFieldRouter, &TelemetryFieldRouter::unknownSchema, m_manifestClient,
            &ManifestClient::onUnknownSchema);
    // A SUBSCRIBE needs its target's boot_id, which only MANIFEST_DATA
    // supplies -- a subscription requested before the manifest arrived is
    // held back and released here.
    connect(m_manifestClient, &ManifestClient::catalogUpdated, m_subscriptionManager,
            &SubscriptionManager::onCatalogUpdated);
    connect(m_manifestClient, &ManifestClient::catalogUpdated, this, &Backend::catalogChanged);
    // CRITERIO DE ACEITE "pedido acima do maximo e limitado e informado ao
    // cliente": the granted rate is surfaced, never silently assumed equal to
    // what was asked for.
    connect(m_subscriptionManager, &SubscriptionManager::subscriptionRateLimited, this,
            [this](quint32 sourceId, quint16 topicId, quint32 requested, quint32 effective) {
                emit statusMessage(
                    tr("Topic 0x%1 of source 0x%2 limited to %3 (requested %4)")
                        .arg(topicId, 4, 16, QChar('0'))
                        .arg(sourceId, 8, 16, QChar('0'))
                        .arg(formatRateMillihz(effective), formatRateMillihz(requested)),
                    8000);
            });
    connect(m_subscriptionManager, &SubscriptionManager::subscriptionRejected, this,
            [this](quint32 sourceId, quint16 topicId, quint8 status, quint16 errorCode) {
                emit statusMessage(tr("SUBSCRIBE rejected for topic 0x%1 of source 0x%2 "
                                      "(status 0x%3, error 0x%4)")
                                       .arg(topicId, 4, 16, QChar('0'))
                                       .arg(sourceId, 8, 16, QChar('0'))
                                       .arg(status, 2, 16, QChar('0'))
                                       .arg(errorCode, 4, 16, QChar('0')),
                                   8000);
            });
    connect(m_subscriptionManager, &SubscriptionManager::subscriptionsChanged, this,
            &Backend::subscriptionsChanged);
    connect(m_subscriptionManager, &SubscriptionManager::statusReceived, this,
            &Backend::statusReceived);

    // TERMINAL_IN/OUT wiring moved here from the old SerialWidgetBridge
    // (topico 19): sendTerminalIn() below builds the outbound frame,
    // onTerminalFrameReceived() below filters the inbound stream down to
    // just TERMINAL_OUT.
    connect(m_protocolRouter, &ProtocolRouter::terminalFrameReceived, this,
            &BtpBackend::onTerminalFrameReceived);
}

BtpBackend::~BtpBackend() {
    delete m_telemetryCatalog;
}

void BtpBackend::feedBytes(const QByteArray& data) {
    m_btpSession->feedBytes(data);
    // BtpHandshake needs the same raw bytes BtpSession sees (it only looks
    // for the plain-text READY line, see protocol/btphandshake.h); bytes
    // that are actually COBS framing are harmless noise to it.
    m_btpHandshake->feedRawBytes(data);
}

void BtpBackend::setHubEndpoint(quint32 selfSourceId, quint32 peerSourceId,
                                const QByteArray& endpointKey) {
    m_selfSourceId = selfSourceId;
    m_peerSourceId = peerSourceId;
    m_endpointKey = endpointKey;
    // The child speaks as itself, with an identity that survives a restart.
    // Overwrites the random one the console channel uses, and must: the hub
    // keys its bind table on this number, so a per-run value would invalidate
    // the operator's binding on every launch (see hubChannelSourceId()).
    if (selfSourceId != 0) {
        m_terminalSourceId = selfSourceId;
    }
    // Every sealed message this backend originates -- SUBSCRIBE/UNSUBSCRIBE
    // and COMMAND_REQUEST -- speaks as this same (source_id, boot_id) and
    // draws from this same sequence counter (nextEndpointSequence()), never
    // each their own: two different messages reusing one AEAD nonce is
    // exactly what encryption.md section 5.1 forbids.
    m_subscriptionManager->setEndpointIdentity(
        m_terminalSourceId, m_terminalBootId, endpointKey,
        [this] { return nextEndpointSequence(); });
    m_commandClient->configure(
        m_terminalSourceId, m_terminalBootId, peerSourceId, m_telemetryCatalog, endpointKey,
        [this] { return nextEndpointSequence(); });
}

quint32 BtpBackend::nextEndpointSequence() {
    return ++m_endpointSequence;
}

void BtpBackend::sendSessionKeepalive() {
    if (!m_sessionEstablished) {
        return;  // session dropped between the timer firing and here
    }

    // MANIFEST_REQUEST payload (commands.md section 3.1): target source_id,
    // target boot_id, known_config_revision -- all uint32_le. Aimed at a
    // source that cannot exist so the reply is a small NOT_FOUND the
    // ManifestClient discards untouched; the point is only that the dongle
    // sees a valid BTP frame and refreshes its watchdog.
    QByteArray payload;
    payload.reserve(12);
    for (quint32 v : {kKeepaliveSentinelSource, quint32(0), quint32(0)}) {
        payload.append(static_cast<char>(v & 0xFF));
        payload.append(static_cast<char>((v >> 8) & 0xFF));
        payload.append(static_cast<char>((v >> 16) & 0xFF));
        payload.append(static_cast<char>((v >> 24) & 0xFF));
    }

    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = m_terminalSourceId;  // this backend's own stable-per-run id
    header.boot_id = m_terminalBootId;
    header.sequence = ++m_sessionSequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kControlManifestRequest;
    header.fragment_index = 0;
    header.fragment_count = 1;

    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(payload.constData()),
                            static_cast<std::size_t>(payload.size())}};
    // Goes out through BtpSession like any other frame -- which also trips the
    // bytesToWrite hook and restarts m_keepaliveTimer for the next interval.
    m_btpSession->sendFrame(frame);
}

bool BtpBackend::requestSessionClose() {
    if (!m_sessionEstablished || m_sessionClosing || m_peerSourceId != 0) {
        return false;
    }

    QByteArray payload;
    payload.reserve(8);
    payload.append(static_cast<char>(kCloseReasonClientShutdown));
    payload.append(3, char(0));  // reserved
    for (int shift = 0; shift < 32; shift += 8) {
        payload.append(static_cast<char>((kCloseDrainTimeoutMs >> shift) & 0xFF));
    }

    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = m_terminalSourceId;
    header.boot_id = m_terminalBootId;
    header.sequence = ++m_sessionSequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kControlSessionClose;
    header.fragment_index = 0;
    header.fragment_count = 1;

    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(payload.constData()),
                            static_cast<std::size_t>(payload.size())}};
    if (!m_btpSession->sendFrame(frame)) {
        return false;
    }

    m_keepaliveTimer->stop();
    m_sessionClosing = true;
    m_sessionEstablished = false;
    return true;
}

void BtpBackend::bindHubChild(quint32 childSourceId, quint32 peerSourceId) {
    m_hubBinder->bindChild(childSourceId, peerSourceId);
}

void BtpBackend::unbindHubChild(quint32 childSourceId) {
    m_hubBinder->unbindChild(childSourceId);
}

void BtpBackend::sendCommand(const QByteArray& text) {
    if (m_peerSourceId == 0) {
        return;  // not a hub-channel device: no robot to address a COMMAND_REQUEST to
    }
    m_commandClient->send(QString::fromUtf8(text));
}

void BtpBackend::onSessionFrameReceived(const BtpFrame& frame) {
    if ((frame.flags & btp::kFlagEncrypted) == 0U) {
        if (m_peerSourceId != 0 && !m_endpointKey.isEmpty()) {
            // Keyed hub channel: the robot seals everything it sends here.
            // An unsealed frame is a downgrade or a spoof -- drop it, and
            // say so once per session so a silently missing plot has an
            // explanation.
            if (!m_unsealedDowngradeReported) {
                m_unsealedDowngradeReported = true;
                emit statusMessage(
                    tr("Dropped an unsealed frame on a sealed hub channel "
                       "(check the robot's channel-B password)"),
                    6000);
            }
            return;
        }
        m_protocolRouter->onFrameReceived(frame);
        return;
    }
    if (m_endpointKey.isEmpty()) {
        return;  // sealed frame, no key configured -- fail closed, never forward ciphertext
    }

    btp::Header header{};
    header.type = frame.type;
    header.flags = frame.flags;
    header.source_id = frame.sourceId;
    header.boot_id = frame.bootId;
    header.sequence = frame.sequence;
    header.timestamp_us = frame.timestampUs;
    header.object_id = frame.objectId;
    header.fragment_index = frame.fragmentIndex;
    header.fragment_count = frame.fragmentCount;

    const std::optional<QByteArray> plaintext =
        ChannelSeal::open(m_endpointKey, header, frame.payload);
    if (!plaintext.has_value()) {
        return;  // tag mismatch / wrong cipher / wrong key -- never deliver unauthenticated bytes
    }

    BtpFrame opened = frame;
    opened.flags &= ~static_cast<quint16>(btp::kFlagEncrypted);
    opened.payload = *plaintext;
    m_protocolRouter->onFrameReceived(opened);
}

void BtpBackend::onTransportConnectionChanged(bool connected) {
    if (connected) {
        m_sessionClosing = false;
        m_unsealedDowngradeReported = false;
        m_btpSession->reset();
        if (m_peerSourceId != 0) {
            // A child does NOT hand shake. HELLO and ENTER negotiate a console
            // session, and a robot offers none -- it accepts exactly three
            // CONTROL object_ids over the radio and HELLO is not one of them,
            // so a handshake here would get silence and the device would never
            // come up.
            //
            // So what the handshake used to trigger is triggered by the link
            // itself. There is no session to establish, hence nothing to wait
            // for: ask the robot for its own catalog (not an enumeration --
            // only a hub can answer that), and let subscriptions re-assert.
            // ClockSync is deliberately absent: it is a "dongle set_clock"
            // shell command addressed to a hub, and a robot has no reason to
            // accept it.
            m_manifestClient->requestCatalogFor(m_peerSourceId);
            m_subscriptionManager->onSessionEstablished();
            return;
        }
        m_btpHandshake->start();
    } else {
        // No session, nothing to keep alive. The next sessionEstablished
        // re-arms it.
        if (m_keepaliveTimer != nullptr) {
            m_keepaliveTimer->stop();
        }
        m_sessionEstablished = false;
        m_sessionClosing = false;
        // Subscriptions are scoped to the session that granted them
        // (topico 17 PASSO 6): forget the grants, keep the widgets that
        // wanted them, so reconnecting re-subscribes everything.
        m_subscriptionManager->onSessionLost();
        // The bindings themselves are kept -- only the dongle's copy of them
        // died with the session, and resendAll() restores it on the next one.
        m_hubBinder->onSessionLost();
    }
}

bool BtpBackend::sendChildFrame(const QByteArray& alreadyEncoded) {
    // A pure forward, on purpose. Everything this backend originates itself
    // goes through sendFrame() and is encoded here; a child's frame is the one
    // thing that must arrive on the wire exactly as the child built it, so it
    // deliberately bypasses every encoding path this class owns.
    return m_btpSession->sendRawFrame(alreadyEncoded);
}

void BtpBackend::sendTerminalIn(const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        return;
    }

    // Split into chunks the dongle accepts as one logical message (topico 35
    // D.1). Its serial session caps a payload at what HELLO_RESULT negotiated
    // -- 2048 today, and it does NOT reassemble serial fragments -- so a
    // single oversized TERMINAL_IN frame (a big paste) is silently truncated
    // into the server-side pty. 1 KB stays well under any plausible negotiated
    // limit; keystrokes and normal pastes are one chunk.
    constexpr int kMaxTerminalChunk = 1024;
    for (int offset = 0; offset < bytes.size(); offset += kMaxTerminalChunk) {
        const QByteArray chunk = bytes.mid(offset, kMaxTerminalChunk);

        btp::Header header{};
        header.type = btp::MessageType::Terminal;
        header.flags = 0;
        header.source_id = m_terminalSourceId;
        header.boot_id = m_terminalBootId;
        header.sequence = ++m_sessionSequence;
        header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
        header.object_id = kTerminalInObjectId;
        header.fragment_index = 0;
        header.fragment_count = 1;

        const btp::Frame frame{header,
                               {reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                                static_cast<std::size_t>(chunk.size())}};
        m_btpSession->sendFrame(frame);
    }
}

void BtpBackend::onTerminalFrameReceived(const traceview::BtpFrame& frame) {
    // TERMINAL_IN frames from other TraceView instances/tools sharing this
    // connection are not this backend's own reply; only TERMINAL_OUT is
    // ever surfaced to a terminal widget.
    if (frame.objectId == kTerminalOutObjectId) {
        emit terminalDataReceived(frame.payload);
    }
}

quint64 BtpBackend::addSubscriber(quint32 sourceId, quint16 topicId, quint32 requestedRateMillihz) {
    return m_subscriptionManager->addSubscriber(sourceId, topicId, requestedRateMillihz);
}

quint64 BtpBackend::updateSubscriber(quint64 handle, quint32 sourceId, quint16 topicId,
                                     quint32 requestedRateMillihz) {
    return m_subscriptionManager->updateSubscriber(handle, sourceId, topicId, requestedRateMillihz);
}

void BtpBackend::removeSubscriber(quint64 handle) {
    m_subscriptionManager->removeSubscriber(handle);
}

QVector<TopicSubscriptionState> BtpBackend::subscriptions() const {
    return m_subscriptionManager->subscriptions();
}

QVector<StatusTopicRecord> BtpBackend::topicStatuses() const {
    return m_subscriptionManager->topicStatuses();
}

QVector<CatalogTopicInfo> BtpBackend::catalogTopics() const {
    QVector<CatalogTopicInfo> result;
    const QVector<TelemetryTopicSchema> schemas = m_telemetryCatalog->allSchemas();
    result.reserve(schemas.size());
    for (const TelemetryTopicSchema& schema : schemas) {
        result.append(toCatalogTopicInfo(schema));
    }
    return result;
}

}  // namespace traceview
