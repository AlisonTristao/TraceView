#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

#include <cstdint>

namespace traceview {
class BtpSession;
class ProtocolRouter;
struct BtpFrame;
}  // namespace traceview

// Generic device-role engine for BTP v1 (BTP/docs/session-and-terminal.md,
// BTP/docs/commands.md) over whatever bytes
// feedBytes()/bytesToWrite() are wired to -- deliberately transport-agnostic,
// the same contract traceview::BtpSession itself uses, so main.cpp is the
// only place that knows about QSerialPort. Reuses traceview::BtpSession/
// ProtocolRouter verbatim for COBS/CRC/envelope framing.
//
// This class owns everything that is the same for *any* simulated device --
// console ENTER/READY, HELLO/HELLO_RESULT, MANIFEST_REQUEST/DATA (built
// generically from the TopicSpec list passed to the constructor),
// SUBSCRIBE/UNSUBSCRIBE/SESSION_CLOSE, the session watchdog, and periodic
// TELEMETRY sending. A concrete subclass (solarpaneldevice.h,
// weatherstationdevice.h) only supplies *what* each topic looks like
// (topics(), passed to the constructor) and *how* to encode one sample
// (sampleBody()) -- it never touches handshake/manifest/subscribe mechanics
// directly. Factored out this way once a second device profile
// (weatherstationdevice.h) needed the exact same protocol machinery: keeping
// it in one place means a correctness fix (like the phase-ordering bug
// enterAwaitingHello()/handleHello() guard against, see their comments)
// only has to happen once.
//
// Deliberately out of scope for every profile: STATUS, TERMINAL_IN/OUT,
// ESP-NOW -- none of them blocks testing the serial telemetry path this tool
// exists for. COMMAND/COMMAND_REQUEST is a partial exception: it answers just
// the "dongle clock"/"dongle set_clock ..." shell one-liners (the generic
// shell action every real firmware executor also answers, BtpTransport::
// btp_command::kShellActionId in bally_dongle) so traceview::ClockSync's
// post-connect clock check/correction has something to talk to when pointed
// at a synthetic device instead of real hardware. No other shell command is
// simulated.
class SyntheticDeviceSession : public QObject {
    Q_OBJECT

public:
    // One enum label (commands.md section 3.3's field record enum entries)
    // -- only non-empty for an enum8/enum16 field.
    struct EnumEntry {
        quint16 value;
        QString label;
    };

    // One field of one topic. Every field here is a non-nullable scalar
    // (element_count=1, scale=1, offset=0) -- richer shapes (arrays,
    // nullable fields) aren't needed by any device profile this tool has
    // today.
    struct FieldSpec {
        quint16 fieldId;
        quint16 order;
        quint8 type;  // TELEMETRY.md section 5 type code, see wireutil.h
        QString name;
        QString unit;
        QVector<EnumEntry> enumEntries;
    };

    // One topic: identity, subscribe-rate ceiling, and its fields in order.
    struct TopicSpec {
        quint16 topicId;
        QString name;
        quint32 maxRateMillihz;
        QVector<FieldSpec> fields;
    };

    // sourceId/sourceName/topics describe one simulated device. source_role
    // (session-and-terminal.md section 1's Roles, commands.md section 3.2)
    // is always ROBOT (0x01) --
    // there's no better fit for "a standalone sensor-bearing thing" in BTP's
    // fixed 4-value role table, and every profile here is exactly that.
    SyntheticDeviceSession(quint32 sourceId, QString sourceName, QVector<TopicSpec> topics,
                           QObject* parent = nullptr);

public slots:
    // Raw bytes off the wire, in arrival order -- same contract as
    // traceview::BtpSession::feedBytes(). Fed to both the console-mode line
    // scanner and the BtpSession unconditionally (mirrors BtpBackend::
    // feedBytes() feeding both BtpSession and BtpHandshake the same bytes):
    // plain ASCII handshake text never contains a COBS delimiter, so it's
    // harmless noise to the decoder, and once binary framing starts the
    // scanner's own phase guard makes it a no-op.
    void feedBytes(const QByteArray& data);

signals:
    // Bytes to write to the transport (the READY/CONSOLE lines, and every
    // BTP frame BtpSession::sendFrame() produces).
    void bytesToWrite(const QByteArray& data);
    // Human-readable line for main.cpp to timestamp and print -- this is the
    // tool's whole "watch the handshake happen" visibility story.
    void logMessage(const QString& text);

protected:
    // Subclasses fill in the PACKED_LE-encoded field bytes (ascending
    // `order`, no presence bitmap -- every field declared above is
    // non-nullable) for topicId's current sample. Called once per that
    // topic's send-timer tick, at whatever rate SUBSCRIBE last granted.
    virtual QByteArray sampleBody(quint16 topicId) = 0;
    // Optional one-line "what does this topic look like right now" summary
    // appended to the 1-second heartbeat log; default is just the sample
    // count/rate the base class already prints.
    virtual QString heartbeatDetail(quint16 topicId) const {
        Q_UNUSED(topicId);
        return QString();
    }

    // Seconds since this process started -- every profile's simulation is a
    // (mostly) pure function of this.
    double elapsedSeconds() const;

private slots:
    void onControlFrameReceived(const traceview::BtpFrame& frame);
    void onCommandFrameReceived(const traceview::BtpFrame& frame);
    void onHelloTimeout();
    void onWatchdogTimeout();

private:
    enum class Phase { Console, AwaitingHello, Established };

    struct TopicRuntime {
        quint32 maxRateMillihz = 0;
        quint32 subscriptionId = 0;
        quint32 effectiveRateMillihz = 0;
        quint32 samplesSinceHeartbeat = 0;
        QTimer* sendTimer = nullptr;   // heap-allocated, parented to `this`
        QTimer* leaseTimer = nullptr;  // (QHash values must stay copyable)
    };

    void scanForEnter(const QByteArray& data);
    void enterAwaitingHello(const QByteArray& nonceLower);

    void handleHello(const traceview::BtpFrame& frame);
    void handleManifestRequest(const traceview::BtpFrame& frame);
    void handleSubscribe(const traceview::BtpFrame& frame);
    void handleUnsubscribe(const traceview::BtpFrame& frame);
    void handleSessionClose(const traceview::BtpFrame& frame);
    void handleCommandRequest(const traceview::BtpFrame& frame);

    void sendControl(quint16 objectId, const QByteArray& payload);
    void sendCommandResult(const traceview::BtpFrame& requestFrame, quint16 actionId, quint16 actionVersion,
                           quint8 status, quint16 errorCode, const QString& message);
    void sendSample(quint16 topicId);
    void expireLease(quint16 topicId);
    quint64 nowUs() const;

    void restartWatchdog();
    void teardown(const QString& reason);

    quint32 m_sourceId;
    QString m_sourceName;
    quint32 m_bootId;
    quint32 m_sequence = 1;
    quint32 m_configRevision = 1;
    QByteArray m_uuid;  // 16 random bytes, generated once at startup
    QElapsedTimer m_clock;

    traceview::BtpSession* m_session;
    traceview::ProtocolRouter* m_router;

    Phase m_phase = Phase::Console;
    QByteArray m_lineBuffer;
    quint32 m_sessionTimeoutMs = 15000;
    quint32 m_nextSubscriptionId = 1;

    QTimer m_helloTimer;
    QTimer m_watchdogTimer;
    QTimer m_heartbeatTimer;

    QVector<TopicSpec> m_topics;
    QHash<quint16, TopicRuntime> m_runtime;  // keyed by topicId, one entry per m_topics element

    // Simulates "dongle set_clock" actually moving this device's clock:
    // "dongle clock" reports QDateTime::currentSecsSinceEpoch() + this
    // offset, and "dongle set_clock <text>" recomputes it from the requested
    // epoch. Zero (real time) until the first set_clock.
    qint64 m_clockOffsetSecs = 0;
};
