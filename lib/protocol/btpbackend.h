#pragma once

#include <btp/codec.hpp>

#include "backend/backend.h"

namespace traceview {

class BtpSession;
class ProtocolRouter;
class TelemetryCatalog;
class TelemetryFieldRouter;
class BtpHandshake;
class ManifestClient;
class SubscriptionManager;
class ClockSync;
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
    // `transport` picks which BTP transport profile the underlying
    // BtpSession speaks (Serial/COBS or UsbHid, see btpsession.h) --
    // DeviceConnection supplies it from the Device's own TransportType
    // (devices/device.h), converted to this BTP-library type since
    // traceview_devices can't depend on btp::codec directly. Defaults to
    // Serial, preserving every existing call site's behavior.
    explicit BtpBackend(btp::TransportProfile transport = btp::TransportProfile::Serial, QObject* parent = nullptr);
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

public slots:
    void feedBytes(const QByteArray& data) override;
    void onTransportConnectionChanged(bool connected) override;
    void sendTerminalIn(const QByteArray& bytes) override;

private:
    void onTerminalFrameReceived(const traceview::BtpFrame& frame);

    BtpSession* m_btpSession;
    ProtocolRouter* m_protocolRouter;
    TelemetryCatalog* m_telemetryCatalog;
    TelemetryFieldRouter* m_telemetryFieldRouter;
    BtpHandshake* m_btpHandshake;
    ManifestClient* m_manifestClient;
    SubscriptionManager* m_subscriptionManager;
    ClockSync* m_clockSync;

    // Minimal, self-contained BTP identity for TERMINAL_IN frames only --
    // there is no HELLO/MANIFEST exchange for it to learn one from (moved
    // here from the old SerialWidgetBridge::sendTerminalIn(), topico 19).
    // Random, non-zero, generated once per BtpBackend lifetime.
    quint32 m_terminalSourceId;
    quint32 m_terminalBootId;
    quint32 m_terminalSequence = 0;
};

}  // namespace traceview
