#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <btp/node.hpp>
#include <functional>
#include <memory>

class QTimer;

namespace traceview {
class Transport;
}

namespace tracelab {

// One BTP producer endpoint on one link: what a robot's firmware is to the
// cable it sits on. A btp::Node serving `catalog` (topics AND actions, BTP
// 2.50) behind a responder session, its subscription table and its command
// handler, reading and writing whole frames through a traceview::Transport --
// normally one end of a LoopbackTransport pair (plan decision L4: BTP even
// inside one process).
//
// An endpoint with several peers (the simulated plant talks to TraceView and
// to the controller host) has one port per link, all serving the same
// catalogue under the same identity; each keeps its own session and
// subscriptions, so what one peer subscribes to never reaches the other.
//
// Publishing is the owner's: publish() sends a sample of a topic when someone
// on this link subscribed to it, and publishDue() only when the subscription's
// rate also allows it now (a viewer at its own rate, while the controller
// gets every tick).
class BtpProducerPort : public QObject, public btp::NodeConfig {
    Q_OBJECT

public:
    // Called for every COMMAND_REQUEST: STREAM actions (`stream` true, no
    // result is sent whatever `outcome` holds) and regular ones (fill
    // `outcome`; it starts as Success). `parameters` is the action's body.
    using CommandHandler = std::function<void(quint16 actionId, btp::ByteView parameters,
                                              btp::NodeActionOutcome* outcome, bool stream)>;
    using SampleFill = std::function<void(btp::SampleWriter& writer)>;

    // `catalog` and `name` must outlive the port. Identity: `sourceId` and
    // `bootId`, both non-zero.
    BtpProducerPort(btp::Catalog* catalog, quint32 sourceId, quint32 bootId,
                    const QByteArray& name, QObject* parent = nullptr);
    ~BtpProducerPort() override;

    void setCommandHandler(CommandHandler handler);

    // Where the frames go; not owned. Bringing the transport up starts a
    // fresh session (and the subscriptions of the last one are forgotten).
    void setTransport(traceview::Transport* transport);
    bool sessionActive() const;

    // Whether anyone on this link holds a subscription to `topicId`.
    bool subscribed(quint16 topicId) const;
    // publish() when subscribed, regardless of the granted rate; true when
    // the sample went out.
    bool publish(quint16 topicId, const SampleFill& fill, quint64 timestampUs);
    // publish() only when the granted rate makes the topic due now.
    bool publishDue(quint16 topicId, const SampleFill& fill, quint64 timestampUs);
    // A whole-body UTF8 document topic (schema_version + text).
    bool sendText(quint16 topicId, const QByteArray& utf8, quint64 timestampUs);

    static quint64 nowMs();

    // btp::NodeConfig
    bool send(const std::uint8_t* frame, std::size_t frame_size) override;
    bool has_clock() const noexcept override {
        return true;
    }
    std::uint64_t clock() override {
        return nowMs();
    }
    bool has_command() const noexcept override {
        return true;
    }
    void command(std::uint16_t action_id, std::uint16_t action_version, btp::ByteView parameters,
                 btp::NodeActionOutcome* outcome, const btp::NodeCommandTicket& ticket) override;

signals:
    // A SUBSCRIBE or UNSUBSCRIBE was served: subscribed() may have changed.
    void subscriptionsChanged();
    void sessionChanged(bool active);

private:
    using Node = btp::StaticNode</*Slots=*/4, /*SlotBytes=*/4096, /*SealBytes=*/64,
                                 /*ScratchBytes=*/16384, /*CatalogTopics=*/1,
                                 /*CatalogFields=*/1, /*CatalogStringBytes=*/16,
                                 /*MaxSubscriptions=*/16, /*MaxCommands=*/8,
                                 /*CommandBytes=*/256>;

    void onBytes(const QByteArray& data);
    void onTransportState(bool connected);
    void startSession();

    btp::Catalog* m_catalog;
    QByteArray m_name;
    std::unique_ptr<Node> m_node;
    QPointer<traceview::Transport> m_transport;
    CommandHandler m_handler;
    QTimer* m_tickTimer = nullptr;
    bool m_sessionActive = false;
};

}  // namespace tracelab
