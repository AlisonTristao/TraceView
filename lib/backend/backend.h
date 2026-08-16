#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "telemetry/statustopicrecord.h"
#include "telemetry/subscriptionstate.h"
#include "telemetry/telemetrybinding.h"

namespace traceview {

// Everything MainWindow/SerialWidgetBridge need from "whatever's on the
// other end of the wire" -- the extension point for plugging in a protocol
// other than BTP. SerialManager (core/serialmanager.h) stays a concrete,
// shared piece owned by MainWindow: it only moves raw bytes in and out of
// the open QSerialPort. Backend is the layer *above* it that gives those
// bytes meaning -- decoding them into telemetry samples, tracking topic
// subscriptions, and framing outbound terminal input -- so a Backend
// implementation depends only on this interface and traceview_telemetry,
// never on traceview_protocol/BTP. BtpBackend (protocol/btpbackend.h) is
// the reference implementation, wrapping BtpSession/ProtocolRouter/
// TelemetryFieldRouter/BtpHandshake/ManifestClient/SubscriptionManager.
//
// Outbound control-widget commands (push button/toggle/slider) are NOT part
// of this interface: they are raw text handed straight to
// SerialManager::writeCommand() (docs/PROTOCOL.md "Outbound: control
// commands"), with no protocol envelope involved, so they bypass Backend
// entirely -- see core/serialwidgetbridge.cpp.
class Backend : public QObject {
    Q_OBJECT

public:
    explicit Backend(QObject* parent = nullptr) : QObject(parent) {}

    // Registers one consumer of (sourceId, topicId) wanting at least
    // `requestedRateMillihz`, returning an opaque handle to pass back to
    // removeSubscriber()/updateSubscriber(). Returns 0 (no consumer
    // registered) when any argument is zero.
    virtual quint64 addSubscriber(quint32 sourceId, quint16 topicId, quint32 requestedRateMillihz) = 0;
    // Moves an existing consumer to another topic and/or rate, returning the
    // handle to keep using. Passing 0 as `handle` is the same as
    // addSubscriber().
    virtual quint64 updateSubscriber(quint64 handle, quint32 sourceId, quint16 topicId,
                                      quint32 requestedRateMillihz) = 0;
    // Drops one consumer. A handle of 0 or an unknown handle is ignored.
    virtual void removeSubscriber(quint64 handle) = 0;
    // Current per-topic view, for display. Only topics with at least one
    // live consumer (or an in-flight request for one) appear.
    virtual QVector<TopicSubscriptionState> subscriptions() const = 0;
    // Last per-topic metrics this backend has received, keyed by (sourceId,
    // topicId). Empty when the backend has none to report.
    virtual QVector<StatusTopicRecord> topicStatuses() const = 0;

public slots:
    // Raw bytes arriving off the transport (SerialManager::dataReceived).
    virtual void feedBytes(const QByteArray& data) = 0;
    // The transport's connection state changed (SerialManager::
    // connectionStateChanged) -- a fresh connection typically means
    // (re)negotiating a session; a lost one means forgetting whatever grants
    // depended on it.
    virtual void onTransportConnectionChanged(bool connected) = 0;
    // Raw bytes typed into a serial monitor/terminal widget, to be framed
    // and sent as this backend sees fit.
    virtual void sendTerminalIn(const QByteArray& bytes) = 0;

signals:
    // Bytes this backend needs written to the transport (connect to
    // SerialManager::write()).
    void bytesToWrite(const QByteArray& data);
    // A one-off, human-readable status update (session established/failed,
    // a subscription rate-limited or rejected, ...) meant for the status
    // bar. `timeoutMs` is how long it should stay visible.
    void statusMessage(const QString& text, int timeoutMs);
    // One decoded telemetry value for `binding`, with its origin timestamp.
    void fieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs, double value);
    // Bytes received back for a terminal/serial-monitor widget (the reply
    // side of sendTerminalIn()).
    void terminalDataReceived(const QByteArray& data);
    // Any change to a topic's state (grant, rejection, rate change,
    // subscriber count) -- a UI refresh hook, coarse on purpose.
    void subscriptionsChanged();
    // Per-topic metrics were updated (topicStatuses() has fresh data).
    void statusReceived();
};

}  // namespace traceview
