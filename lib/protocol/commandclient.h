#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>
#include <functional>

#include "backend/statusseverity.h"

namespace traceview {

class BtpSession;
class ProtocolRouter;
class TelemetryCatalog;
struct BtpFrame;

// Sends one COMMAND_REQUEST at a time -- action_id=1/action_version=1, "run
// this line as a shell command", the convention bally_dongle and bally_OS
// both define (BTP/docs/commands.md section 2.1) -- to a fixed target, and
// reports the COMMAND_RESULT via statusMessage(). This is what a hub-channel
// device's control widgets need instead of TERMINAL_IN: bally_OS's dispatch
// drops every TERMINAL frame it receives (no handler exists), and only
// COMMAND has been widened to accept channel B (key E) so far.
//
// Inert until configure() is called with a non-zero target -- mirrors
// BtpBackend::setHubEndpoint()'s all-or-nothing contract, and is exactly why
// a plain console-facing BtpBackend (target never set) leaves send() a
// silent no-op.
class CommandClient : public QObject {
    Q_OBJECT

public:
    CommandClient(BtpSession* session, ProtocolRouter* router, QObject* parent = nullptr);

    // `selfSourceId`/`selfBootId` must be this backend's STABLE hub identity
    // (BtpBackend::m_terminalSourceId/m_terminalBootId post-setHubEndpoint) --
    // the same identity SUBSCRIBE is sent under, because `hub -bind` keys its
    // table on exactly one child source_id per device (bally_dongle's
    // HubRegistry). `endpointKey` empty means "not configured": send()
    // refuses rather than transmit a COMMAND_REQUEST in the clear to a robot.
    // `nextSequence` MUST be shared with every other sealed message this
    // backend originates under the same (selfSourceId, selfBootId, key) --
    // two different messages reusing one AEAD nonce is the one failure mode
    // this whole design exists to avoid (encryption.md section 5.1).
    void configure(quint32 selfSourceId, quint32 selfBootId, quint32 targetSourceId,
                  TelemetryCatalog* catalog, const QByteArray& endpointKey,
                  std::function<quint32()> nextSequence);

public slots:
    // No-op, silently, when not configured, when a previous command is still
    // awaiting its result, or when the target's boot_id is not known yet (no
    // MANIFEST_DATA from it in this process -- same reason SUBSCRIBE holds
    // back in SubscriptionManager::sendSubscribe). One outstanding request at
    // a time: a second press before the first result lands has no correlated
    // place to show a second answer anyway.
    void send(const QString& commandLine);

signals:
    void statusMessage(const QString& text, int timeoutMs,
                       traceview::StatusSeverity severity = traceview::StatusSeverity::Info);

private slots:
    void onCommandFrameReceived(const traceview::BtpFrame& frame);
    void onReplyTimeout();

private:
    static constexpr int kReplyTimeoutMs = 5000;

    BtpSession* m_session;
    TelemetryCatalog* m_catalog = nullptr;

    quint32 m_selfSourceId = 0;
    quint32 m_selfBootId = 0;
    quint32 m_targetSourceId = 0;
    QByteArray m_endpointKey;
    std::function<quint32()> m_nextSequence;

    bool m_pending = false;
    quint32 m_pendingSequence = 0;
    QTimer m_replyTimer;
};

}  // namespace traceview
