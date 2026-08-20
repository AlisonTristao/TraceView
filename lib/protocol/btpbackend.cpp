#include "protocol/btpbackend.h"

#include <QDateTime>
#include <QRandomGenerator>

#include <btp/codec.hpp>

#include "protocol/btpframe.h"
#include "protocol/btphandshake.h"
#include "protocol/btpsession.h"
#include "protocol/clocksync.h"
#include "protocol/manifestclient.h"
#include "protocol/protocolrouter.h"
#include "protocol/subscriptionmanager.h"
#include "protocol/telemetrycatalog.h"
#include "protocol/telemetryfieldrouter.h"

namespace traceview {

namespace {

// bally_protocol/docs/COMMANDS_AND_ACTIONS.md section 3.3 -- object_id
// values within MessageType::Terminal. Mirrors t_dongle_develop's
// SerialSession::kTerminalInObjectId/kTerminalOutObjectId (lib/SerialSession/
// SerialSession.h); each side of the wire defines its own copy of these
// normatively-fixed constants, same as topico 13 did on the dongle.
constexpr quint16 kTerminalInObjectId = 0x0001;
constexpr quint16 kTerminalOutObjectId = 0x0002;

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

}  // namespace

BtpBackend::BtpBackend(QObject* parent)
    : Backend(parent), m_terminalSourceId(randomNonZero()), m_terminalBootId(randomNonZero()) {
    // BTP v1 client stack (topico 14): raw bytes -> BtpSession (COBS decode
    // + envelope/CRC validation + reassembly) -> ProtocolRouter (dispatch by
    // MessageType) -> TelemetryFieldRouter (schema decode, fan out by
    // field). m_telemetryCatalog starts empty and is populated dynamically
    // by m_manifestClient's MANIFEST_REQUEST/MANIFEST_DATA exchange (topico
    // 16) -- nothing here assumes which source_id/schema is on the other
    // end in advance.
    m_btpSession = new BtpSession(this);
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
    m_subscriptionManager = new SubscriptionManager(m_btpSession, m_protocolRouter, m_telemetryCatalog, this);

    connect(m_btpSession, &BtpSession::bytesToWrite, this, &Backend::bytesToWrite);
    // BtpHandshake's own outbound text (the ENTER line) goes out the same
    // way; the HELLO frame itself goes through BtpSession::sendFrame(),
    // whose bytesToWrite() is already connected above.
    connect(m_btpHandshake, &BtpHandshake::bytesToWrite, this, &Backend::bytesToWrite);

    connect(m_btpHandshake, &BtpHandshake::sessionEstablished, this,
            [this](quint32 peerSourceId, quint32 peerBootId, quint32 peerConfigRevision, quint8 selectedVersion) {
                emit statusMessage(tr("BTP session established (HELLO_RESULT=SUCCESS)"), 5000);
                m_manifestClient->onSessionEstablished(peerConfigRevision);
                m_subscriptionManager->onSessionEstablished();
                m_clockSync->onSessionEstablished(peerSourceId, peerBootId);
                // Devices tab display only -- peerSourceId is the dongle's own
                // BTP identity (its HELLO_RESULT envelope's source_id), the
                // closest thing to a "device ID" this protocol reports today.
                emit deviceIdentified(tr("BTP/%1").arg(selectedVersion),
                                      QString("0x%1").arg(peerSourceId, 8, 16, QChar('0')).toUpper());
            });
    connect(m_btpHandshake, &BtpHandshake::sessionFailed, this, [this](const QString& reason) {
        emit statusMessage(tr("BTP handshake failed: %1").arg(reason), 8000);
    });
    connect(m_clockSync, &ClockSync::statusMessage, this, &Backend::statusMessage);
    connect(m_btpSession, &BtpSession::frameReceived, m_protocolRouter, &ProtocolRouter::onFrameReceived);
    connect(m_protocolRouter, &ProtocolRouter::telemetrySampleReceived, m_telemetryFieldRouter,
            &TelemetryFieldRouter::onTelemetrySample);
    connect(m_telemetryFieldRouter, &TelemetryFieldRouter::fieldSample, this, &Backend::fieldSample);
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
    // CRITERIO DE ACEITE "pedido acima do maximo e limitado e informado ao
    // cliente": the granted rate is surfaced, never silently assumed equal to
    // what was asked for.
    connect(m_subscriptionManager, &SubscriptionManager::subscriptionRateLimited, this,
            [this](quint32 sourceId, quint16 topicId, quint32 requested, quint32 effective) {
                emit statusMessage(tr("Topic 0x%1 of source 0x%2 limited to %3 (requested %4)")
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
    connect(m_subscriptionManager, &SubscriptionManager::subscriptionsChanged, this, &Backend::subscriptionsChanged);
    connect(m_subscriptionManager, &SubscriptionManager::statusReceived, this, &Backend::statusReceived);

    // TERMINAL_IN/OUT wiring moved here from the old SerialWidgetBridge
    // (topico 19): sendTerminalIn() below builds the outbound frame,
    // onTerminalFrameReceived() below filters the inbound stream down to
    // just TERMINAL_OUT.
    connect(m_protocolRouter, &ProtocolRouter::terminalFrameReceived, this, &BtpBackend::onTerminalFrameReceived);
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

void BtpBackend::onTransportConnectionChanged(bool connected) {
    if (connected) {
        m_btpSession->reset();
        m_btpHandshake->start();
    } else {
        // Subscriptions are scoped to the session that granted them
        // (topico 17 PASSO 6): forget the grants, keep the widgets that
        // wanted them, so reconnecting re-subscribes everything.
        m_subscriptionManager->onSessionLost();
    }
}

void BtpBackend::sendTerminalIn(const QByteArray& bytes) {
    if (bytes.isEmpty()) {
        return;
    }

    btp::Header header{};
    header.type = btp::MessageType::Terminal;
    header.flags = 0;
    header.source_id = m_terminalSourceId;
    header.boot_id = m_terminalBootId;
    header.sequence = ++m_terminalSequence;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kTerminalInObjectId;
    header.fragment_index = 0;
    header.fragment_count = 1;

    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                            static_cast<std::size_t>(bytes.size())}};
    m_btpSession->sendFrame(frame);
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

}  // namespace traceview
