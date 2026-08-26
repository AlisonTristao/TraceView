#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include "telemetry/catalogtopicinfo.h"
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
    virtual quint64 addSubscriber(quint32 sourceId, quint16 topicId,
                                  quint32 requestedRateMillihz) = 0;
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
    // Every (source, topic) schema this backend's catalog currently holds,
    // as announced by the device's own manifest exchange (MANIFEST_DATA for
    // BtpBackend) -- each entry's `name` is the human-readable topic name
    // TELEMETRY.md section 3 requires alongside the numeric topic_id.
    // Display-only: addSubscriber()/updateSubscriber() still take raw
    // sourceId/topicId, never one of these. Empty until that exchange has
    // completed at least once.
    virtual QVector<CatalogTopicInfo> catalogTopics() const = 0;

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
    // A control widget's command line (push button/toggle/slider), to be
    // sent as this backend sees fit -- for BtpBackend this means a real
    // COMMAND_REQUEST when the device is a hub channel (a robot has no
    // TERMINAL handler; only COMMAND is accepted end to end through the
    // hub), and is a silent no-op otherwise, mirroring the "no console
    // channel" contract control-widget commands already have for a USB HID
    // device (see core/serialwidgetbridge.cpp).
    virtual void sendCommand(const QByteArray& text) = 0;

signals:
    // Bytes this backend needs written to the transport (connect to
    // SerialManager::write()).
    void bytesToWrite(const QByteArray& data);
    // A one-off, human-readable status update (session established/failed,
    // a subscription rate-limited or rejected, ...) meant for the status
    // bar. `timeoutMs` is how long it should stay visible.
    void statusMessage(const QString& text, int timeoutMs);
    // One decoded telemetry value for `binding`, with its origin timestamp.
    void fieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                     double value);
    // Bytes received back for a terminal/serial-monitor widget (the reply
    // side of sendTerminalIn()).
    void terminalDataReceived(const QByteArray& data);
    // Any change to a topic's state (grant, rejection, rate change,
    // subscriber count) -- a UI refresh hook, coarse on purpose.
    void subscriptionsChanged();
    // Per-topic metrics were updated (topicStatuses() has fresh data).
    void statusReceived();
    // catalogTopics() has fresh data (a manifest exchange completed or was
    // updated) -- lets a UI cache built from it (e.g. MainWindow's
    // DeviceOption list for the properties panel) know to re-fetch instead
    // of only refreshing on device add/remove/rename.
    void catalogChanged();
    // The connected device just identified itself (session established).
    // btpVersion/btpId are what the Devices tab's "Reported by device"
    // section shows -- live-mirrored the same way `Device::connected` is, not
    // user-editable and not persisted (see devices/deviceconfigdialog.h).
    void deviceIdentified(const QString& btpVersion, const QString& btpId);
    // The transport is fine but the protocol session on top of it is dead and
    // will not recover on its own -- the owner should recycle the connection
    // (close it, and let its retry timer reopen it).
    //
    // This exists because "transport open" and "session established" are two
    // states, and only the first one had a recovery path. A handshake that
    // failed left the port open, so DeviceConnection's retry timer -- which
    // only ever fires for a transport that is DOWN -- never had anything to
    // do, and the device sat there reading "connected" while speaking to
    // nobody, with no way back except unplugging it. A backend with no
    // session concept simply never emits this.
    void sessionRecoveryNeeded();
};

}  // namespace traceview
