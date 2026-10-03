#include "controllerhost.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <btp/profiles/loop.hpp>
#include <btp/telemetry.hpp>
#include <cstring>

#include "core/transport.h"
#include "lab/plant/btpproducerport.h"
#include "lab/plant/plantmanifest.h"

namespace tracelab {

namespace loopp = btp::profiles::loop;

namespace {

constexpr int kTickIntervalMs = 100;
constexpr int kRetryIntervalMs = 1000;
constexpr std::uint64_t kHelloTimeoutMs = 3000;
// Every tick, whatever the plant allows; renewed by Node::tick().
constexpr std::uint32_t kSubscribeRateMillihz = 1'000'000;
constexpr std::uint32_t kSubscribeLeaseMs = 10000;
constexpr int kMaxPrediction = 64;

constexpr quint16 kDiagTopic = 0x0001;
constexpr quint16 kInfoTopic = 0x0002;
const char kDiagTopicName[] = "ctl.diag";
const char kInfoTopicName[] = "ctl.info";

quint64 nowMs() {
    return quint64(QDateTime::currentMSecsSinceEpoch());
}

btp::FieldRecord arrayField(btp::FieldRecord field, int maxCount) {
    field.flags = std::uint8_t(field.flags | btp::kFieldVariableCount);
    field.element_count = 0;
    field.max_element_count = std::uint16_t(maxCount);
    return field;
}

}  // namespace

// The NodeConfig of the plant-side consumer node: its frames go to the plant
// transport.
class ControllerHost::Link final : public btp::NodeConfig {
public:
    explicit Link(ControllerHost* host) : m_host(host) {}
    bool send(const std::uint8_t* frame, std::size_t frame_size) override {
        if (!m_host->m_plantTransport) {
            return false;
        }
        return m_host->m_plantTransport->write(
            QByteArray(reinterpret_cast<const char*>(frame), qsizetype(frame_size)));
    }
    bool has_clock() const noexcept override {
        return true;
    }
    std::uint64_t clock() override {
        return nowMs();
    }

private:
    ControllerHost* m_host;
};

ControllerHost::ControllerHost(quint32 sourceId, QObject* parent)
    : QObject(parent), m_sourceId(sourceId), m_link(std::make_unique<Link>(this)) {
    m_link->source_id = sourceId;
    m_link->boot_id = quint32(QDateTime::currentMSecsSinceEpoch() & 0x7fffffff) | 1U;
    m_link->transport = btp::kSerialTransport;
    m_scratch.resize(4096);
    m_infoName = QByteArrayLiteral("TraceLab: Controller");

    buildDiagCatalog();
    m_viewer = new BtpProducerPort(&m_diagCatalog, sourceId, m_link->boot_id, m_infoName, this);
    connect(m_viewer, &BtpProducerPort::subscriptionsChanged, this, &ControllerHost::sendInfo);

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(kTickIntervalMs);
    connect(m_tickTimer, &QTimer::timeout, this, [this] {
        if (!m_node) {
            return;
        }
        m_node->tick(nowMs());
        const btp::InitiatorEvent event = m_node->initiator_event();
        if (event == btp::InitiatorEvent::TimedOut ||
            event == btp::InitiatorEvent::Disconnected) {
            if (m_ready) {
                m_ready = false;
                emit plantReadyChanged(false);
            }
            if (!m_retryTimer->isActive()) {
                m_retryTimer->start();
            }
        }
    });
    m_tickTimer->start();

    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(kRetryIntervalMs);
    connect(m_retryTimer, &QTimer::timeout, this, &ControllerHost::connectToPlant);
}

ControllerHost::~ControllerHost() = default;

void ControllerHost::setAdapter(std::unique_ptr<ControllerAdapter> adapter) {
    m_adapter = std::move(adapter);
    m_configured = false;
    if (m_adapter && !m_measurementNames.isEmpty()) {
        m_adapter->setSignalNames(m_measurementNames, m_inputNames);
        QString error;
        m_configured =
            m_adapter->configure(m_measurementNames.size(), m_inputNames.size(), &error);
    }
    sendInfo();
}

ControllerAdapter* ControllerHost::adapter() const {
    return m_adapter.get();
}

void ControllerHost::refreshInfo() {
    sendInfo();
}

void ControllerHost::setTransport(traceview::Transport* transport) {
    setPlantTransport(transport);
}

void ControllerHost::setPlantTransport(traceview::Transport* transport) {
    if (m_plantTransport) {
        disconnect(m_plantTransport, nullptr, this, nullptr);
    }
    m_plantTransport = transport;
    m_node.reset();
    if (m_ready) {
        m_ready = false;
        emit plantReadyChanged(false);
    }
    if (!transport) {
        return;
    }
    connect(transport, &traceview::Transport::dataReceived, this, &ControllerHost::onPlantBytes);
    connect(transport, &traceview::Transport::connectionStateChanged, this,
            &ControllerHost::onPlantState);
    if (transport->isConnected()) {
        connectToPlant();
    }
}

void ControllerHost::setViewerTransport(traceview::Transport* transport) {
    m_viewerTransport = transport;
    m_viewer->setTransport(transport);
}

void ControllerHost::onPlantState(bool connected) {
    if (connected) {
        connectToPlant();
        return;
    }
    m_node.reset();
    m_retryTimer->stop();
    if (m_ready) {
        m_ready = false;
        emit plantReadyChanged(false);
    }
}

void ControllerHost::connectToPlant() {
    if (!m_plantTransport || !m_plantTransport->isConnected()) {
        return;
    }
    // A fresh node per attempt: nothing of an older session (subscriptions,
    // a half-learned catalogue) survives into the next one.
    m_node = std::make_unique<ConsumerNode>(*m_link);
    m_learned.clear();
    m_node->learn_catalog(&m_learned);
    m_node->on_sample(&ControllerHost::sampleThunk, this);
    m_node->begin(false);
    std::uint8_t uuid[16];
    for (int i = 0; i < 16; ++i) {
        uuid[i] = std::uint8_t(0xC0 + i);
    }
    const btp::Hello hello = btp::HelloBuilder(btp::Role::Consumer, uuid)
                                 .max_logical_payload(65536)
                                 .max_subscriptions(4)
                                 .session_timeout_ms(60000)
                                 .build();
    m_stateTopic = m_previewTopic = m_uAction = m_modeAction = 0;
    m_previewTick = -1;
    if (!m_node->connect(hello, nowMs(), kHelloTimeoutMs)) {
        m_retryTimer->start();
    }
}

void ControllerHost::onPlantBytes(const QByteArray& data) {
    if (!m_node) {
        return;
    }
    btp::ReceivedMessage message{};
    const btp::NodeRx rx =
        m_node->receive(reinterpret_cast<const std::uint8_t*>(data.constData()),
                        std::size_t(data.size()), nowMs(), &message);
    if (!m_node) {
        return;  // torn down from inside a sample
    }
    switch (rx) {
        case btp::NodeRx::InitiatorHandled:
            if (m_node->initiator_event() == btp::InitiatorEvent::Connected) {
                m_peerSource = m_node->connected_peer_source_id();
                m_peerBoot = m_node->connected_peer_boot_id();
                m_node->request_manifest(m_peerSource, m_peerBoot, 0);
            } else {
                m_retryTimer->start();
            }
            break;
        case btp::NodeRx::CatalogUpdated:
            onCatalog();
            break;
        default:
            break;
    }
}

void ControllerHost::onCatalog() {
    m_stateTopic = m_previewTopic = 0;
    m_measurementNames.clear();
    for (std::size_t i = 0; i < m_learned.topic_count(); ++i) {
        const btp::CatalogTopic* topic = m_learned.topic_at(i);
        if (topic == nullptr || topic->name == nullptr) {
            continue;
        }
        if (std::strcmp(topic->name, kTopicLoopState) == 0) {
            m_stateTopic = topic->topic_id;
            for (std::size_t f = 0; f < topic->field_count; ++f) {
                const QString name = QString::fromUtf8(m_learned.field_name(*topic, f));
                if (name == QLatin1String(loopp::field::kTick) ||
                    name == QLatin1String(loopp::field::kMode)) {
                    continue;
                }
                const btp::FieldSpec& spec = topic->fields[f];
                const int count = (spec.flags & btp::kFieldVariableCount) != 0
                                      ? int(spec.max_element_count)
                                      : int(spec.element_count);
                if (count <= 1 && (spec.flags & btp::kFieldVariableCount) == 0) {
                    m_measurementNames.append(name);
                } else {
                    for (int e = 0; e < count; ++e) {
                        m_measurementNames.append(QStringLiteral("%1[%2]").arg(name).arg(e));
                    }
                }
            }
        } else if (std::strcmp(topic->name, kTopicSimPreview) == 0) {
            m_previewTopic = topic->topic_id;
        }
    }
    m_uAction = m_modeAction = 0;
    m_inputNames.clear();
    for (std::size_t i = 0; i < m_learned.action_count(); ++i) {
        const btp::CatalogAction* action = m_learned.action_at(i);
        if (action == nullptr || action->name == nullptr) {
            continue;
        }
        if (std::strcmp(action->name, kActionLoopU) == 0) {
            m_uAction = action->action_id;
            for (std::size_t p = 1; p < action->parameter_count; ++p) {
                m_inputNames.append(QString::fromUtf8(m_learned.field_name(*action, p)));
            }
        } else if (std::strcmp(action->name, kActionLoopMode) == 0) {
            m_modeAction = action->action_id;
        }
    }
    if (m_stateTopic == 0 || m_uAction == 0) {
        return;  // not a loop-profile plant
    }
    if (m_adapter) {
        m_adapter->setSignalNames(m_measurementNames, m_inputNames);
        QString error;
        m_configured =
            m_adapter->configure(m_measurementNames.size(), m_inputNames.size(), &error);
    }
    m_node->subscribe(m_peerSource, m_peerBoot, m_stateTopic, kSubscribeRateMillihz,
                      kSubscribeLeaseMs);
    if (m_previewTopic != 0) {
        m_node->subscribe(m_peerSource, m_peerBoot, m_previewTopic, kSubscribeRateMillihz,
                          kSubscribeLeaseMs);
    }
    if (m_modeAction != 0) {
        // loop.mode REMOTE, armed: actuation now comes from loop.u.
        const btp::CatalogAction* action = m_learned.action(m_modeAction, 1);
        btp::SampleWriter writer(reinterpret_cast<std::uint8_t*>(m_scratch.data()),
                                 std::size_t(m_scratch.size()), action->fields,
                                 action->parameter_count);
        writer.begin(0, btp::SampleLayout::BodyOnly);
        writer.put_u64(loopp::kModeRemote);
        writer.put_bool(true);
        std::size_t written = 0;
        if (writer.finish(&written) == btp::MessageError::Ok) {
            m_node->command(m_peerSource, m_peerBoot, m_modeAction, 1,
                            reinterpret_cast<const std::uint8_t*>(m_scratch.constData()),
                            written);
        }
    }
    m_lastTick = 0;
    if (!m_ready) {
        m_ready = true;
        emit plantReadyChanged(true);
    }
}

void ControllerHost::sampleThunk(void* ctx, const btp::CatalogTopic& topic,
                                 btp::SampleReader& reader) {
    static_cast<ControllerHost*>(ctx)->onSample(topic, reader);
}

void ControllerHost::onSample(const btp::CatalogTopic& topic, btp::SampleReader& reader) {
    btp::SampleValue value{};
    if (topic.topic_id == m_previewTopic && m_previewTopic != 0) {
        qint64 tick = -1;
        QVector<double> px;
        QVector<double> py;
        std::size_t index = 0;
        while (reader.next(&value) == btp::SampleStep::Item) {
            const char* name = m_learned.field_name(topic, index++);
            if (value.is_null) {
                continue;
            }
            if (std::strcmp(name, "tick") == 0) {
                tick = qint64(value.f64(0));
            } else if (std::strcmp(name, "px") == 0 || std::strcmp(name, "py") == 0) {
                QVector<double>& out = name[1] == 'x' ? px : py;
                for (std::size_t e = 0; e < value.count; ++e) {
                    out.append(value.f64(e));
                }
            }
        }
        m_previewTick = tick;
        m_previewX = px;
        m_previewY = py;
        return;
    }
    if (topic.topic_id != m_stateTopic || m_stateTopic == 0) {
        return;
    }
    quint64 tick = 0;
    QVector<double> y;
    y.reserve(m_measurementNames.size());
    std::size_t index = 0;
    while (reader.next(&value) == btp::SampleStep::Item) {
        const char* name = m_learned.field_name(topic, index++);
        if (std::strcmp(name, loopp::field::kTick) == 0) {
            tick = value.is_null ? 0 : quint64(value.f64(0));
            continue;
        }
        if (std::strcmp(name, loopp::field::kMode) == 0) {
            continue;
        }
        const std::size_t declared =
            (topic.fields[index - 1].flags & btp::kFieldVariableCount) != 0
                ? topic.fields[index - 1].max_element_count
                : topic.fields[index - 1].element_count;
        for (std::size_t e = 0; e < qMax<std::size_t>(declared, 1); ++e) {
            y.append(!value.is_null && e < value.count ? value.f64(e) : qQNaN());
        }
    }
    answer(tick, y);
}

void ControllerHost::answer(quint64 tick, const QVector<double>& y) {
    if (!m_adapter || !m_configured || !m_node) {
        return;
    }
    if (tick < m_lastTick) {
        m_adapter->reset();  // the plant went back: sim.reset
    }
    m_lastTick = tick;

    ControllerInput input;
    input.tick = tick;
    input.y = y;
    if (m_previewTick == qint64(tick)) {
        input.reference = m_previewX + m_previewY;
    }
    ControllerOutput output;
    QElapsedTimer timer;
    timer.start();
    const bool computed = m_adapter->compute(input, &output);
    const double solveMs = timer.nsecsElapsed() * 1e-6;
    // No answer (no preview of this tick yet, at the start): an empty plan
    // still answers the tick, so the plant holds its input and goes on instead
    // of waiting for a plan that will not come.
    const bool hold = !computed || output.u.isEmpty();
    if (hold) {
        output.u.clear();
    }

    // loop.u: ref_tick, then one array per input over the horizon.
    const btp::CatalogAction* action = m_learned.action(m_uAction, 1);
    if (action == nullptr) {
        return;
    }
    btp::SampleWriter writer(reinterpret_cast<std::uint8_t*>(m_scratch.data()),
                             std::size_t(m_scratch.size()), action->fields,
                             action->parameter_count);
    writer.begin(0, btp::SampleLayout::BodyOnly);
    writer.put_f64(double(tick));
    for (int i = 0; i < m_inputNames.size(); ++i) {
        std::vector<double> horizon;
        for (const QVector<double>& step : output.u) {
            horizon.push_back(i < step.size() ? step[i] : 0.0);
        }
        writer.put_array_f64(horizon.data(), horizon.size());
    }
    std::size_t written = 0;
    if (writer.finish(&written) == btp::MessageError::Ok) {
        m_node->send_stream(m_peerSource, m_peerBoot, m_uAction, 1,
                            reinterpret_cast<const std::uint8_t*>(m_scratch.constData()),
                            written);
    }
    if (hold) {
        return;
    }
    ++m_answered;

    // ctl.diag for TraceView.
    const quint64 now = nowMs() * 1000ULL;
    m_lastTimestampUs = qMax(now, m_lastTimestampUs + 1);
    const auto diag = [&](btp::SampleWriter& w) {
        const auto array = [&](const QVector<double>& values) {
            w.put_array_f64(values.constData(),
                            std::size_t(qMin<qsizetype>(values.size(), kMaxPrediction)));
        };
        w.put_f64(double(tick));
        w.put_f64(solveMs);
        w.put_f64(output.iterations);
        array(output.predictedX);
        array(output.predictedY);
        array(output.predictedTheta);
        const int diagCount = m_adapter->diagNames().size();
        for (int i = 0; i < diagCount; ++i) {
            w.put_f64(i < output.diag.size() ? output.diag[i] : qQNaN());
        }
    };
    m_viewer->publishDue(kDiagTopic, diag, m_lastTimestampUs);
    emit stepped(tick, solveMs);
}

void ControllerHost::buildDiagCatalog() {
    m_diagCatalog.clear();
    m_diagCatalog.set_config_revision_auto();
    const QStringList names = m_adapter ? m_adapter->diagNames() : QStringList();
    const QStringList units = m_adapter ? m_adapter->diagUnits() : QStringList();
    QVector<QByteArray> nameBytes;
    QVector<QByteArray> unitBytes;
    for (int i = 0; i < names.size(); ++i) {
        nameBytes.append(names[i].toUtf8());
        unitBytes.append(units.value(i).toUtf8());
    }
    QVector<btp::FieldRecord> fields = {
        btp::u32("ref_tick"),
        btp::f64("solve_ms", "ms"),
        btp::u32("iterations"),
        arrayField(btp::f64("pred_x", "m"), kMaxPrediction),
        arrayField(btp::f64("pred_y", "m"), kMaxPrediction),
        arrayField(btp::f64("pred_theta", "rad"), kMaxPrediction),
    };
    for (int i = 0; i < nameBytes.size(); ++i) {
        fields.append(btp::f64(nameBytes[i].constData(), unitBytes[i].constData()));
    }
    m_diagCatalog.add_topic(kDiagTopic, 1, btp::TelemetryEncoding::PackedLe, true, 0,
                            kDiagTopicName, fields.constData(), std::size_t(fields.size()));
    m_diagCatalog.add_topic(kInfoTopic, 1, btp::TelemetryEncoding::Utf8, true, 0, kInfoTopicName,
                            nullptr, 0);
}

void ControllerHost::sendInfo() {
    // ctl.diag's schema follows the adapter's diagnostics: rebuilt while no
    // viewer holds it (a viewer re-reads the manifest on its next session).
    if (m_adapter && !m_viewer->sessionActive()) {
        const btp::CatalogTopic* diag = m_diagCatalog.topic(kDiagTopic);
        if (diag != nullptr && int(diag->field_count) != 6 + m_adapter->diagNames().size()) {
            buildDiagCatalog();
        }
    }
    if (!m_adapter) {
        return;
    }
    m_lastTimestampUs = qMax(nowMs() * 1000ULL, m_lastTimestampUs + 1);
    m_viewer->sendText(kInfoTopic, m_adapter->name().toUtf8(), m_lastTimestampUs);
}

}  // namespace tracelab
