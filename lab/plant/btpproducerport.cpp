#include "btpproducerport.h"

#include <QDateTime>
#include <QTimer>
#include <cstring>

#include "core/transport.h"

namespace tracelab {

namespace {

// Session watchdog and subscription expiry cadence (Node::tick()).
constexpr int kTickIntervalMs = 200;
// What this end offers in HELLO_RESULT; the negotiated watchdog is the
// smaller of both sides'. TraceView's keepalive and the controller host's
// subscription renewals keep a quiet session alive well inside it.
constexpr std::uint32_t kSessionTimeoutMs = 60000;

void fillThunk(void* ctx, btp::SampleWriter& writer) {
    (*static_cast<const BtpProducerPort::SampleFill*>(ctx))(writer);
}

btp::Hello localHello(const QByteArray& name) {
    // A stable UUID per endpoint name: the same device answers with the same
    // identity across runs (TraceView's manifest cache keys on it).
    std::uint8_t uuid[16] = {};
    std::uint32_t hash = 2166136261u;
    for (int i = 0; i < 16; ++i) {
        for (char c : name) {
            hash ^= quint8(c) + std::uint32_t(i);
            hash *= 16777619u;
        }
        uuid[i] = std::uint8_t(hash >> 8) | 1U;
    }
    return btp::HelloBuilder(btp::Role::Producer, uuid)
        .max_logical_payload(65536)
        .max_subscriptions(16)
        .session_timeout_ms(kSessionTimeoutMs)
        .build();
}

}  // namespace

BtpProducerPort::BtpProducerPort(btp::Catalog* catalog, quint32 sourceId, quint32 bootId,
                                 const QByteArray& name, QObject* parent)
    : QObject(parent), m_catalog(catalog), m_name(name) {
    source_id = sourceId;
    boot_id = bootId;
    transport = btp::kSerialTransport;
    m_node = std::make_unique<Node>(*this);
    m_node->serve_catalog(m_catalog, static_cast<std::uint8_t>(btp::Role::Producer), nullptr,
                          m_name.constData());
    m_node->enable_session(localHello(m_name), /*hello_deadline_ms=*/0);
    m_node->begin(/*arm_and_announce=*/false);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(kTickIntervalMs);
    connect(m_tickTimer, &QTimer::timeout, this, [this] {
        const btp::SessionEvent event = m_node->tick(nowMs());
        if (event == btp::SessionEvent::TimedOut) {
            m_sessionActive = false;
            emit sessionChanged(false);
            m_node->arm_session(nowMs());
        }
    });
    m_tickTimer->start();
}

BtpProducerPort::~BtpProducerPort() = default;

void BtpProducerPort::setCommandHandler(CommandHandler handler) {
    m_handler = std::move(handler);
}

void BtpProducerPort::setTransport(traceview::Transport* transport) {
    if (m_transport) {
        disconnect(m_transport, nullptr, this, nullptr);
    }
    m_transport = transport;
    if (!transport) {
        return;
    }
    connect(transport, &traceview::Transport::dataReceived, this, &BtpProducerPort::onBytes);
    connect(transport, &traceview::Transport::connectionStateChanged, this,
            &BtpProducerPort::onTransportState);
    if (transport->isConnected()) {
        startSession();
    }
}

bool BtpProducerPort::sessionActive() const {
    return m_sessionActive;
}

quint64 BtpProducerPort::nowMs() {
    return quint64(QDateTime::currentMSecsSinceEpoch());
}

bool BtpProducerPort::subscribed(quint16 topicId) const {
    const btp::SubscriptionTable* table = m_node->subscriptions();
    return m_sessionActive && table != nullptr && table->subscriber_count(topicId) > 0;
}

bool BtpProducerPort::publish(quint16 topicId, const SampleFill& fill, quint64 timestampUs) {
    if (!subscribed(topicId)) {
        return false;
    }
    return m_node->publish(topicId, &fillThunk, const_cast<SampleFill*>(&fill), timestampUs);
}

bool BtpProducerPort::publishDue(quint16 topicId, const SampleFill& fill, quint64 timestampUs) {
    btp::SubscriptionTable* table = m_node->subscriptions();
    const quint64 now = nowMs();
    if (!subscribed(topicId) || table == nullptr || !table->due(topicId, now)) {
        return false;
    }
    if (!m_node->publish(topicId, &fillThunk, const_cast<SampleFill*>(&fill), timestampUs)) {
        return false;
    }
    table->note_published(topicId, now);
    return true;
}

bool BtpProducerPort::sendText(quint16 topicId, const QByteArray& utf8, quint64 timestampUs) {
    if (!subscribed(topicId)) {
        return false;
    }
    const btp::CatalogTopic* topic = m_catalog->topic(topicId);
    if (topic == nullptr) {
        return false;
    }
    QByteArray payload;
    payload.reserve(utf8.size() + 2);
    payload.append(char(topic->schema_version & 0xFF));
    payload.append(char(topic->schema_version >> 8));
    payload.append(utf8);
    return m_node->send(btp::MessageType::Telemetry, topicId,
                        reinterpret_cast<const std::uint8_t*>(payload.constData()),
                        std::size_t(payload.size()), timestampUs);
}

bool BtpProducerPort::send(const std::uint8_t* frame, std::size_t frame_size) {
    if (!m_transport) {
        return false;
    }
    return m_transport->write(
        QByteArray(reinterpret_cast<const char*>(frame), qsizetype(frame_size)));
}

void BtpProducerPort::command(std::uint16_t action_id, std::uint16_t action_version,
                              btp::ByteView parameters, btp::NodeActionOutcome* outcome,
                              const btp::NodeCommandTicket& ticket) {
    Q_UNUSED(action_version);
    if (!m_handler) {
        if (ticket.valid()) {
            outcome->status = static_cast<std::uint8_t>(btp::ResultStatus::Rejected);
            outcome->error_code = static_cast<std::uint16_t>(btp::ResultError::NotFound);
        }
        return;
    }
    m_handler(action_id, parameters, outcome, !ticket.valid());
}

void BtpProducerPort::onBytes(const QByteArray& data) {
    btp::ReceivedMessage message{};
    const btp::NodeRx rx =
        m_node->receive(reinterpret_cast<const std::uint8_t*>(data.constData()),
                        std::size_t(data.size()), nowMs(), &message);
    switch (rx) {
        case btp::NodeRx::SessionHandled:
            switch (m_node->session_event()) {
                case btp::SessionEvent::HelloAccepted:
                    m_sessionActive = true;
                    emit sessionChanged(true);
                    break;
                case btp::SessionEvent::SessionClosed:
                case btp::SessionEvent::TimedOut:
                case btp::SessionEvent::HelloRejected:
                case btp::SessionEvent::Abandoned:
                    if (m_sessionActive) {
                        m_sessionActive = false;
                        emit sessionChanged(false);
                    }
                    m_node->arm_session(nowMs());
                    break;
                default:
                    break;
            }
            break;
        case btp::NodeRx::SubscriptionServed:
            emit subscriptionsChanged();
            break;
        default:
            break;
    }
}

void BtpProducerPort::onTransportState(bool connected) {
    if (connected) {
        startSession();
        return;
    }
    m_node->reset_link(0);
    if (m_sessionActive) {
        m_sessionActive = false;
        emit sessionChanged(false);
    }
}

void BtpProducerPort::startSession() {
    m_node->reset_link(0);
    m_sessionActive = false;
    m_node->arm_session(nowMs());
}

}  // namespace tracelab
