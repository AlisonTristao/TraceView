#include "syntheticdevicesession.h"

#include <QRandomGenerator>
#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QtMath>

#include <btp/codec.hpp>

#include <utility>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"
#include "wireutil.h"

using traceview::BtpFrame;
using traceview::BtpSession;
using traceview::ProtocolRouter;
using wireutil::appendF64;
using wireutil::appendLe;
using wireutil::appendUtf8U16;
using wireutil::readLe16;
using wireutil::readLe32;

namespace {

// COMMANDS_AND_ACTIONS.md section 3.2.
constexpr quint16 kControlHello = 0x0001;
constexpr quint16 kControlHelloResult = 0x0002;
constexpr quint16 kControlManifestRequest = 0x0003;
constexpr quint16 kControlManifestData = 0x0004;
constexpr quint16 kControlSubscribe = 0x0005;
constexpr quint16 kControlSubscribeResult = 0x0006;
constexpr quint16 kControlUnsubscribe = 0x0007;
constexpr quint16 kControlUnsubscribeResult = 0x0008;
constexpr quint16 kControlSessionClose = 0x000A;
constexpr quint16 kControlSessionCloseResult = 0x000B;

// Common result/error codes actually used below (COMMANDS_AND_ACTIONS.md
// section 2) -- only the subset this device ever emits.
constexpr quint8 kStatusSuccess = 0x00;
constexpr quint8 kStatusRejected = 0x01;
constexpr quint8 kStatusUnsupported = 0x05;
constexpr quint16 kErrorNone = 0x0000;
constexpr quint16 kErrorMalformedPayload = 0x0001;
constexpr quint16 kErrorUnknownObject = 0x0002;
constexpr quint16 kErrorUnsupportedVersion = 0x0008;
constexpr quint16 kErrorStaleTargetBoot = 0x0009;
constexpr quint16 kErrorNotFound = 0x000B;

constexpr quint8 kManifestFlagNotModified = 0x01;
constexpr quint8 kManifestFlagCatalogComplete = 0x02;
constexpr quint8 kSourceRoleRobot = 0x01;
constexpr quint8 kSourceFlagOnline = 0x01;
constexpr quint8 kEncodingPackedLe = 0x05;
constexpr quint8 kTopicFlagSubscribable = 0x01;

constexpr int kMaxLineBufferBytes = 512;
constexpr int kHelloTimeoutMs = 2000;  // TRANSPORT_SERIAL.md section 5
constexpr int kHeartbeatIntervalMs = 1000;

// The 12-byte request-reference every *_RESULT/MANIFEST_DATA starts with
// (COMMANDS_AND_ACTIONS.md section 2): copied from the *triggering frame's
// BTP envelope*, never from inside that frame's payload.
void appendRequestRef(QByteArray& out, const BtpFrame& requestFrame) {
    appendLe(out, requestFrame.sourceId, 4);
    appendLe(out, requestFrame.bootId, 4);
    appendLe(out, requestFrame.sequence, 4);
}

struct ParsedHello {
    QVector<quint8> versions;
    quint32 maxLogicalPayload = 0;
    quint16 maxInflightReassemblies = 0;
    quint16 maxSubscriptions = 0;
    quint32 maxDedupEntries = 0;
    quint32 sessionTimeoutMs = 0;
};

// HELLO payload layout (COMMANDS_AND_ACTIONS.md section 5): role(1) +
// version_count(1) + flags(2) + max_logical_payload(4) +
// max_inflight_reassemblies(2) + max_subscriptions(2) + max_dedup_entries(4)
// + session_timeout_ms(4) + peer_uuid(16) + config_revision(4) +
// versions(version_count). role/flags/peer_uuid/config_revision aren't
// needed to answer HELLO_RESULT so they're skipped rather than stored.
bool parseHello(const QByteArray& payload, ParsedHello* out) {
    if (payload.size() < 40) {
        return false;
    }
    const quint8 versionCount = quint8(payload.at(1));
    if (payload.size() != 40 + versionCount) {
        return false;
    }
    out->maxLogicalPayload = readLe32(payload, 4);
    out->maxInflightReassemblies = readLe16(payload, 8);
    out->maxSubscriptions = readLe16(payload, 10);
    out->maxDedupEntries = readLe32(payload, 12);
    out->sessionTimeoutMs = readLe32(payload, 16);
    out->versions.reserve(versionCount);
    for (int i = 0; i < versionCount; ++i) {
        out->versions.append(quint8(payload.at(40 + i)));
    }
    return true;
}

// One field record (COMMANDS_AND_ACTIONS.md section 6.2): self-prefixed with
// record_size (bytes following the size field itself), so building it as a
// standalone buffer first and prepending the length keeps this a direct
// mirror of ManifestClient::parseFieldRecord()'s read order.
QByteArray buildFieldRecord(const SyntheticDeviceSession::FieldSpec& field) {
    QByteArray body;
    appendLe(body, field.fieldId, 2);
    appendLe(body, field.order, 2);
    body.append(static_cast<char>(field.type));
    body.append(static_cast<char>(0));  // flags: not nullable, not variable-count
    appendLe(body, 1, 2);               // element_count: scalar
    appendLe(body, 0, 2);               // max_element_count: unused for a fixed field
    appendF64(body, 1.0);               // scale
    appendF64(body, 0.0);               // offset
    appendLe(body, quint32(field.enumEntries.size()), 2);
    appendUtf8U16(body, field.name);
    appendUtf8U16(body, field.unit);
    appendUtf8U16(body, QString());  // description
    for (const SyntheticDeviceSession::EnumEntry& entry : field.enumEntries) {
        appendLe(body, entry.value, 2);
        appendUtf8U16(body, entry.label);
    }

    QByteArray record;
    appendLe(record, quint32(body.size()), 4);
    record.append(body);
    return record;
}

QByteArray buildTopicRecord(const SyntheticDeviceSession::TopicSpec& topic) {
    QByteArray body;
    appendLe(body, topic.topicId, 2);
    appendLe(body, 1, 2);  // schema_version: always 1, these schemas never change
    body.append(static_cast<char>(kEncodingPackedLe));
    body.append(static_cast<char>(kTopicFlagSubscribable));
    appendLe(body, quint16(topic.fields.size()), 2);
    appendLe(body, topic.maxRateMillihz, 4);
    appendUtf8U16(body, topic.name);
    appendUtf8U16(body, QString());  // description
    for (const SyntheticDeviceSession::FieldSpec& field : topic.fields) {
        body.append(buildFieldRecord(field));
    }

    QByteArray record;
    appendLe(record, quint32(body.size()), 4);
    record.append(body);
    return record;
}

}  // namespace

SyntheticDeviceSession::SyntheticDeviceSession(quint32 sourceId, QString sourceName, QVector<TopicSpec> topics,
                                               QObject* parent)
    : QObject(parent), m_sourceId(sourceId), m_sourceName(std::move(sourceName)), m_topics(std::move(topics)) {
    m_bootId = QRandomGenerator::global()->generate() | 1u;  // non-zero, fixed for this process's whole lifetime
    m_uuid.reserve(16);
    for (int i = 0; i < 16; ++i) {
        m_uuid.append(static_cast<char>(QRandomGenerator::global()->bounded(1, 256)));
    }
    m_clock.start();

    m_session = new BtpSession(this);
    m_router = new ProtocolRouter(this);
    connect(m_session, &BtpSession::frameReceived, m_router, &ProtocolRouter::onFrameReceived);
    connect(m_session, &BtpSession::bytesToWrite, this, &SyntheticDeviceSession::bytesToWrite);
    connect(m_router, &ProtocolRouter::controlFrameReceived, this, &SyntheticDeviceSession::onControlFrameReceived);

    m_helloTimer.setSingleShot(true);
    connect(&m_helloTimer, &QTimer::timeout, this, &SyntheticDeviceSession::onHelloTimeout);
    m_watchdogTimer.setSingleShot(true);
    connect(&m_watchdogTimer, &QTimer::timeout, this, &SyntheticDeviceSession::onWatchdogTimeout);

    for (const TopicSpec& spec : m_topics) {
        TopicRuntime runtime;
        runtime.maxRateMillihz = spec.maxRateMillihz;
        runtime.sendTimer = new QTimer(this);
        runtime.leaseTimer = new QTimer(this);
        runtime.leaseTimer->setSingleShot(true);
        const quint16 topicId = spec.topicId;
        connect(runtime.sendTimer, &QTimer::timeout, this, [this, topicId] { sendSample(topicId); });
        connect(runtime.leaseTimer, &QTimer::timeout, this, [this, topicId] { expireLease(topicId); });
        m_runtime.insert(topicId, runtime);
    }

    m_heartbeatTimer.setInterval(kHeartbeatIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, [this] {
        for (const TopicSpec& spec : m_topics) {
            TopicRuntime& runtime = m_runtime[spec.topicId];
            if (runtime.samplesSinceHeartbeat == 0) {
                continue;
            }
            const QString detail = heartbeatDetail(spec.topicId);
            emit logMessage(QStringLiteral("telemetry: %1  %2 samples/1.0s  rate=%3Hz%4")
                                 .arg(spec.name)
                                 .arg(runtime.samplesSinceHeartbeat)
                                 .arg(runtime.effectiveRateMillihz / 1000.0, 0, 'f', 2)
                                 .arg(detail.isEmpty() ? QString() : QStringLiteral("  ") + detail));
            runtime.samplesSinceHeartbeat = 0;
        }
    });
    m_heartbeatTimer.start();

    emit logMessage(QStringLiteral("%1: source_id=0x%2, boot_id=0x%3")
                         .arg(m_sourceName)
                         .arg(m_sourceId, 8, 16, QChar('0'))
                         .arg(m_bootId, 8, 16, QChar('0')));
}

double SyntheticDeviceSession::elapsedSeconds() const { return m_clock.elapsed() / 1000.0; }

quint64 SyntheticDeviceSession::nowUs() const { return quint64(m_clock.nsecsElapsed() / 1000); }

void SyntheticDeviceSession::feedBytes(const QByteArray& data) {
    if (m_phase == Phase::Console) {
        scanForEnter(data);
    }
    // Mirrors BtpBackend::feedBytes() feeding BtpSession and BtpHandshake the
    // same bytes unconditionally: plain ASCII console text never contains a
    // 0x00 COBS delimiter, so it's harmless noise to the decoder while still
    // in console mode.
    m_session->feedBytes(data);
}

void SyntheticDeviceSession::scanForEnter(const QByteArray& data) {
    m_lineBuffer.append(data);
    if (m_lineBuffer.size() > kMaxLineBufferBytes) {
        m_lineBuffer.remove(0, m_lineBuffer.size() - kMaxLineBufferBytes);
    }

    static const QRegularExpression re(QStringLiteral("BTP/1 ENTER ([0-9A-Fa-f]{16})\r\n"));
    const QRegularExpressionMatch match = re.match(QString::fromLatin1(m_lineBuffer));
    if (!match.hasMatch()) {
        return;
    }
    enterAwaitingHello(match.captured(1).toLower().toLatin1());
}

void SyntheticDeviceSession::enterAwaitingHello(const QByteArray& nonceLower) {
    m_lineBuffer.clear();
    emit logMessage(QStringLiteral("console: ENTER nonce=%1 -> READY sent, entering protocolled mode")
                         .arg(QString::fromLatin1(nonceLower)));
    // TRANSPORT_SERIAL.md section 5: the device clears its COBS decoder state
    // right before protocolled mode starts. This MUST happen, and m_phase
    // MUST already read AwaitingHello, before bytesToWrite() below: a
    // direct-connected loopback (or just a very fast real client) can have
    // the client's HELLO arrive back in feedBytes() synchronously, nested
    // inside this emit, before it returns -- if phase/decoder state were
    // updated afterward, that nested HELLO would see stale Console-phase
    // state and be silently dropped instead of dispatched.
    m_session->reset();
    m_phase = Phase::AwaitingHello;
    m_helloTimer.start(kHelloTimeoutMs);
    emit bytesToWrite("BTP/1 READY " + nonceLower + "\r\n");
}

void SyntheticDeviceSession::onControlFrameReceived(const BtpFrame& frame) {
    if (m_phase == Phase::AwaitingHello) {
        if (frame.objectId == kControlHello) {
            handleHello(frame);
        }
        // Spec: the client's first frame MUST be HELLO. Anything else here is
        // simply ignored; the 2000ms HELLO timeout is what actually enforces
        // this.
        return;
    }
    if (m_phase != Phase::Established) {
        return;
    }

    restartWatchdog();
    switch (frame.objectId) {
        case kControlManifestRequest:
            handleManifestRequest(frame);
            break;
        case kControlSubscribe:
            handleSubscribe(frame);
            break;
        case kControlUnsubscribe:
            handleUnsubscribe(frame);
            break;
        case kControlSessionClose:
            handleSessionClose(frame);
            break;
        default:
            break;  // HELLO resent, STATUS, or anything else: nothing to do
    }
}

void SyntheticDeviceSession::handleHello(const BtpFrame& frame) {
    ParsedHello parsed;
    const bool ok = parseHello(frame.payload, &parsed);

    quint8 status = kStatusUnsupported;
    quint8 selectedVersion = 0;
    quint16 errorCode = ok ? kErrorUnsupportedVersion : kErrorMalformedPayload;
    if (ok) {
        // The respondent picks the highest version common to both sides
        // (COMMANDS_AND_ACTIONS.md section 5) -- search our own supported
        // range from the top down against the client's advertised list.
        for (quint8 version = btp::kMaximumProtocolVersion; version >= btp::kMinimumProtocolVersion; --version) {
            if (parsed.versions.contains(version)) {
                selectedVersion = version;
                status = kStatusSuccess;
                errorCode = kErrorNone;
                break;
            }
            if (version == btp::kMinimumProtocolVersion) {
                break;  // avoid underflow when kMinimumProtocolVersion == 0 is ever introduced
            }
        }
    }

    QByteArray reply;
    appendRequestRef(reply, frame);
    reply.append(static_cast<char>(status));
    reply.append(static_cast<char>(selectedVersion));
    appendLe(reply, errorCode, 2);

    if (status == kStatusSuccess) {
        constexpr quint32 kOurMaxLogicalPayload = quint32(btp::kSerialMaxPayloadSize);
        constexpr quint16 kOurMaxInflightReassemblies = 1;
        constexpr quint16 kOurMaxSubscriptions = 8;
        constexpr quint32 kOurMaxDedupEntries = 16;
        constexpr quint32 kOurSessionTimeoutMs = 30000;

        const quint32 effectiveMaxPayload = qMin(parsed.maxLogicalPayload, kOurMaxLogicalPayload);
        const quint16 effectiveMaxInflight = qMin(parsed.maxInflightReassemblies, kOurMaxInflightReassemblies);
        const quint16 effectiveMaxSubs = qMin(parsed.maxSubscriptions, kOurMaxSubscriptions);
        const quint32 effectiveMaxDedup = qMin(parsed.maxDedupEntries, kOurMaxDedupEntries);
        m_sessionTimeoutMs = qMin(parsed.sessionTimeoutMs, kOurSessionTimeoutMs);

        appendLe(reply, effectiveMaxPayload, 4);
        appendLe(reply, effectiveMaxInflight, 2);
        appendLe(reply, effectiveMaxSubs, 2);
        appendLe(reply, effectiveMaxDedup, 4);
        appendLe(reply, m_sessionTimeoutMs, 4);
        reply.append(m_uuid);
        appendLe(reply, m_configRevision, 4);
    } else {
        appendLe(reply, 0, 4);
        appendLe(reply, 0, 2);
        appendLe(reply, 0, 2);
        appendLe(reply, 0, 4);
        appendLe(reply, 0, 4);
        reply.append(QByteArray(16, '\0'));
        appendLe(reply, 0, 4);
    }

    m_helloTimer.stop();

    // Flip m_phase (and arm the watchdog) BEFORE sendControl() below, not
    // after -- see enterAwaitingHello()'s comment: sendControl() emits
    // bytesToWrite(), whose downstream (a loopback, or just a fast real
    // client) can call back into feedBytes() with the client's next frame --
    // e.g. MANIFEST_REQUEST -- synchronously, nested inside this very call.
    if (status == kStatusSuccess) {
        m_phase = Phase::Established;
        restartWatchdog();
    }

    sendControl(kControlHelloResult, reply);

    if (status == kStatusSuccess) {
        emit logMessage(QStringLiteral("control: HELLO -> HELLO_RESULT SUCCESS (version=%1, session_timeout=%2ms)")
                             .arg(selectedVersion)
                             .arg(m_sessionTimeoutMs));
    } else {
        emit logMessage(QStringLiteral("control: HELLO -> HELLO_RESULT %1, closing session")
                             .arg(ok ? QStringLiteral("UNSUPPORTED") : QStringLiteral("UNSUPPORTED (malformed payload)")));
        // COMMANDS_AND_ACTIONS.md section 5: "sem intersecao... fecha a
        // sessao depois de transmitir a resposta."
        teardown(QStringLiteral("HELLO negotiation failed"));
    }
}

void SyntheticDeviceSession::handleManifestRequest(const BtpFrame& frame) {
    if (frame.payload.size() < 12) {
        return;
    }
    const quint32 targetSourceId = readLe32(frame.payload, 0);
    const quint32 targetBootId = readLe32(frame.payload, 4);
    const quint32 knownRevision = readLe32(frame.payload, 8);

    QByteArray reply;
    appendRequestRef(reply, frame);

    const bool addressedToUs = targetSourceId == 0 || targetSourceId == m_sourceId;
    if (!addressedToUs) {
        reply.append(static_cast<char>(kStatusRejected));
        reply.append(static_cast<char>(kManifestFlagCatalogComplete));
        appendLe(reply, kErrorNotFound, 2);
        appendLe(reply, 1, 2);  // manifest_format_version
        appendLe(reply, 0, 2);  // reserved
        appendLe(reply, 0, 4);  // config_revision
        reply.append(QByteArray(16, '\0'));
        appendLe(reply, 0, 4);
        appendLe(reply, 0, 4);
        reply.append(static_cast<char>(0));
        reply.append(static_cast<char>(0));
        appendLe(reply, 0, 2);
        appendLe(reply, 1, 2);
        appendLe(reply, 0, 2);
        appendLe(reply, 0, 2);
        appendUtf8U16(reply, QString());
        sendControl(kControlManifestData, reply);
        emit logMessage(QStringLiteral("control: MANIFEST_REQUEST(target=0x%1) -> MANIFEST_DATA NOT_FOUND")
                             .arg(targetSourceId, 8, 16, QChar('0')));
        return;
    }
    if (targetBootId != 0 && targetBootId != m_bootId) {
        reply.append(static_cast<char>(kStatusRejected));
        reply.append(static_cast<char>(kManifestFlagCatalogComplete));
        appendLe(reply, kErrorStaleTargetBoot, 2);
        appendLe(reply, 1, 2);
        appendLe(reply, 0, 2);
        appendLe(reply, 0, 4);
        reply.append(QByteArray(16, '\0'));
        appendLe(reply, 0, 4);
        appendLe(reply, 0, 4);
        reply.append(static_cast<char>(0));
        reply.append(static_cast<char>(0));
        appendLe(reply, 0, 2);
        appendLe(reply, 1, 2);
        appendLe(reply, 0, 2);
        appendLe(reply, 0, 2);
        appendUtf8U16(reply, QString());
        sendControl(kControlManifestData, reply);
        emit logMessage(QStringLiteral("control: MANIFEST_REQUEST -> MANIFEST_DATA STALE_TARGET_BOOT"));
        return;
    }

    // NOT_MODIFIED only applies to a *targeted* request (target_source_id !=
    // 0) -- a catalog-wide request (target=0) always gets the full body
    // regardless of known_config_revision (COMMANDS_AND_ACTIONS.md section
    // 6.1). TraceView's ManifestClient always requests target=0 first, so
    // this path realistically always takes the full-catalog branch below.
    const bool targeted = targetSourceId != 0;
    const bool notModified = targeted && knownRevision != 0 && knownRevision == m_configRevision;

    quint8 flags = kManifestFlagCatalogComplete;
    quint16 topicCount = 0;
    if (notModified) {
        flags |= kManifestFlagNotModified;
    } else {
        topicCount = quint16(m_topics.size());
    }

    reply.append(static_cast<char>(kStatusSuccess));
    reply.append(static_cast<char>(flags));
    appendLe(reply, kErrorNone, 2);
    appendLe(reply, 1, 2);  // manifest_format_version
    appendLe(reply, 0, 2);  // reserved
    appendLe(reply, m_configRevision, 4);
    reply.append(m_uuid);
    appendLe(reply, m_sourceId, 4);
    appendLe(reply, m_bootId, 4);
    reply.append(static_cast<char>(kSourceRoleRobot));
    reply.append(static_cast<char>(kSourceFlagOnline));
    appendLe(reply, 0, 2);  // catalog_index
    appendLe(reply, 1, 2);  // catalog_count
    appendLe(reply, topicCount, 2);
    appendLe(reply, 0, 2);  // action_count
    appendUtf8U16(reply, m_sourceName);

    if (!notModified) {
        for (const TopicSpec& spec : m_topics) {
            reply.append(buildTopicRecord(spec));
        }
    }

    sendControl(kControlManifestData, reply);
    emit logMessage(QStringLiteral("control: MANIFEST_REQUEST(target=0x%1) -> MANIFEST_DATA %2 (%3 topics)")
                         .arg(targetSourceId, 8, 16, QChar('0'))
                         .arg(notModified ? QStringLiteral("NOT_MODIFIED") : QStringLiteral("SUCCESS"))
                         .arg(topicCount));
}

void SyntheticDeviceSession::handleSubscribe(const BtpFrame& frame) {
    if (frame.payload.size() < 20) {
        return;
    }
    const quint32 targetSourceId = readLe32(frame.payload, 0);
    const quint32 targetBootId = readLe32(frame.payload, 4);
    const quint16 topicId = readLe16(frame.payload, 8);
    const quint32 requestedRate = readLe32(frame.payload, 12);
    const quint32 requestedLease = readLe32(frame.payload, 16);

    const auto it = m_runtime.find(topicId);
    const bool known = it != m_runtime.end();

    quint8 status = kStatusSuccess;
    quint16 errorCode = kErrorNone;
    quint32 subscriptionId = 0;
    quint32 effectiveRate = 0;
    quint32 grantedLease = 0;

    if (targetSourceId != m_sourceId || !known) {
        status = kStatusRejected;
        errorCode = kErrorUnknownObject;
    } else if (targetBootId != m_bootId) {
        status = kStatusRejected;
        errorCode = kErrorStaleTargetBoot;
    } else {
        TopicRuntime& runtime = it.value();
        if (runtime.subscriptionId == 0) {
            runtime.subscriptionId = m_nextSubscriptionId++;
        }
        subscriptionId = runtime.subscriptionId;
        effectiveRate = qMin(requestedRate, runtime.maxRateMillihz);
        grantedLease = requestedLease;
        runtime.effectiveRateMillihz = effectiveRate;

        const int intervalMs = qMax(1, int(1000000.0 / double(effectiveRate)));
        runtime.sendTimer->start(intervalMs);
        runtime.leaseTimer->start(int(grantedLease));
    }

    QByteArray reply;
    appendRequestRef(reply, frame);
    reply.append(static_cast<char>(status));
    reply.append(static_cast<char>(0));  // reserved
    appendLe(reply, errorCode, 2);
    appendLe(reply, subscriptionId, 4);
    appendLe(reply, effectiveRate, 4);
    appendLe(reply, grantedLease, 4);
    sendControl(kControlSubscribeResult, reply);

    if (status == kStatusSuccess) {
        emit logMessage(QStringLiteral("control: SUBSCRIBE topic=0x%1 requested=%2Hz lease=%3ms -> "
                                        "SUBSCRIBE_RESULT SUCCESS subscription_id=%4 effective=%5Hz")
                             .arg(topicId, 4, 16, QChar('0'))
                             .arg(requestedRate / 1000.0, 0, 'f', 2)
                             .arg(requestedLease)
                             .arg(subscriptionId)
                             .arg(effectiveRate / 1000.0, 0, 'f', 2));
    } else {
        emit logMessage(QStringLiteral("control: SUBSCRIBE topic=0x%1 -> SUBSCRIBE_RESULT REJECTED (error 0x%2)")
                             .arg(topicId, 4, 16, QChar('0'))
                             .arg(errorCode, 4, 16, QChar('0')));
    }
}

void SyntheticDeviceSession::handleUnsubscribe(const BtpFrame& frame) {
    if (frame.payload.size() < 12) {
        return;
    }
    const quint32 subscriptionId = readLe32(frame.payload, 8);

    for (auto it = m_runtime.begin(); it != m_runtime.end(); ++it) {
        TopicRuntime& runtime = it.value();
        if (runtime.subscriptionId != 0 && runtime.subscriptionId == subscriptionId) {
            runtime.sendTimer->stop();
            runtime.leaseTimer->stop();
            runtime.subscriptionId = 0;
            runtime.effectiveRateMillihz = 0;
            break;
        }
    }

    // Spec: removing an already-absent subscription still returns
    // SUCCESS/NONE, making retries idempotent -- there is no error path here.
    QByteArray reply;
    appendRequestRef(reply, frame);
    reply.append(static_cast<char>(kStatusSuccess));
    reply.append(static_cast<char>(0));
    appendLe(reply, kErrorNone, 2);
    sendControl(kControlUnsubscribeResult, reply);
    emit logMessage(QStringLiteral("control: UNSUBSCRIBE subscription_id=%1 -> UNSUBSCRIBE_RESULT SUCCESS")
                         .arg(subscriptionId));
}

void SyntheticDeviceSession::handleSessionClose(const BtpFrame& frame) {
    QByteArray reply;
    appendRequestRef(reply, frame);
    reply.append(static_cast<char>(kStatusSuccess));
    reply.append(static_cast<char>(0));
    appendLe(reply, kErrorNone, 2);
    sendControl(kControlSessionCloseResult, reply);
    teardown(QStringLiteral("SESSION_CLOSE received"));
}

void SyntheticDeviceSession::sendControl(quint16 objectId, const QByteArray& payload) {
    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = m_sourceId;
    header.boot_id = m_bootId;
    header.sequence = m_sequence++;
    header.timestamp_us = nowUs();
    header.object_id = objectId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    frame.payload.size = static_cast<std::size_t>(payload.size());
    m_session->sendFrame(frame);
}

void SyntheticDeviceSession::sendSample(quint16 topicId) {
    const auto it = m_runtime.find(topicId);
    if (it == m_runtime.end()) {
        return;
    }
    it.value().samplesSinceHeartbeat++;

    QByteArray payload;
    appendLe(payload, 1, 2);  // schema_version
    payload.append(sampleBody(topicId));

    btp::Header header{};
    header.type = btp::MessageType::Telemetry;
    header.flags = 0;
    header.source_id = m_sourceId;
    header.boot_id = m_bootId;
    header.sequence = m_sequence++;
    header.timestamp_us = nowUs();
    header.object_id = topicId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    frame.payload.size = static_cast<std::size_t>(payload.size());
    m_session->sendFrame(frame);
}

void SyntheticDeviceSession::expireLease(quint16 topicId) {
    const auto it = m_runtime.find(topicId);
    if (it == m_runtime.end() || it.value().subscriptionId == 0) {
        return;
    }
    emit logMessage(QStringLiteral("session: lease expired for topic=0x%1 subscription_id=%2 (no renewal)")
                         .arg(topicId, 4, 16, QChar('0'))
                         .arg(it.value().subscriptionId));
    it.value().sendTimer->stop();
    it.value().subscriptionId = 0;
    it.value().effectiveRateMillihz = 0;
}

void SyntheticDeviceSession::onHelloTimeout() {
    if (m_phase == Phase::AwaitingHello) {
        teardown(QStringLiteral("no HELLO within 2000ms of READY"));
    }
}

void SyntheticDeviceSession::onWatchdogTimeout() {
    if (m_phase == Phase::Established) {
        teardown(QStringLiteral("watchdog expired (no valid frame for session_timeout_ms)"));
    }
}

void SyntheticDeviceSession::restartWatchdog() { m_watchdogTimer.start(int(m_sessionTimeoutMs)); }

void SyntheticDeviceSession::teardown(const QString& reason) {
    m_helloTimer.stop();
    m_watchdogTimer.stop();
    for (auto it = m_runtime.begin(); it != m_runtime.end(); ++it) {
        TopicRuntime& runtime = it.value();
        runtime.sendTimer->stop();
        runtime.leaseTimer->stop();
        runtime.subscriptionId = 0;
        runtime.effectiveRateMillihz = 0;
        runtime.samplesSinceHeartbeat = 0;
    }
    m_session->reset();
    m_phase = Phase::Console;
    m_lineBuffer.clear();
    emit logMessage(QStringLiteral("session: %1 -> reverting to console mode").arg(reason));
    emit bytesToWrite(QByteArrayLiteral("BTP/1 CONSOLE\r\n"));
}
