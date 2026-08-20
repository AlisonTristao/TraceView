#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>

namespace traceview {

class BtpSession;
class ProtocolRouter;
struct BtpFrame;

// Replaces StartupConfig's old boot-time "informe data/hora" prompt
// (bally_dongle/lib/StartupConfig/StartupConfig.cpp -- removed so the dongle
// no longer blocks its own boot waiting on a human) with a BTP
// COMMAND_REQUEST/COMMAND_RESULT round trip over the dongle's existing
// generic shell action (BtpTransport::btp_command::kShellActionId,
// bally_protocol/docs/COMMANDS_AND_ACTIONS.md section 4): once a session is
// established this runs "dongle clock" on the connected dongle and, if its
// answer has drifted past kDriftToleranceSecs from this desktop's own clock,
// corrects it with "dongle set_clock ...". No human interaction, no boot-time
// delay on either side.
class ClockSync : public QObject {
    Q_OBJECT

public:
    explicit ClockSync(BtpSession* session, ProtocolRouter* router, QObject* parent = nullptr);

public slots:
    // Wired to BtpHandshake::sessionEstablished(peerSourceId, peerBootId,
    // peerConfigRevision) -- the connected dongle's own identity is exactly
    // HELLO_RESULT's frame header (see BtpHandshake::onControlFrameReceived),
    // the same (source_id, boot_id) SerialMux::handleCommandRequest on the
    // firmware side requires a COMMAND_REQUEST's target_source_id/
    // target_boot_id to match.
    void onSessionEstablished(quint32 peerSourceId, quint32 peerBootId);

signals:
    // A one-off, human-readable status update, same contract as
    // Backend::statusMessage -- BtpBackend just forwards it.
    void statusMessage(const QString& text, int timeoutMs);

private slots:
    void onCommandFrameReceived(const traceview::BtpFrame& frame);
    void onReplyTimeout();

private:
    enum class Pending { None, Clock, SetClock };

    void sendShellCommand(const QString& commandLine, Pending expecting);
    void requestClock();

    BtpSession* m_session;

    // Private per-process identity, same construction ManifestClient/
    // SerialWidgetBridge use for their own request channels (topico 16/19) --
    // COMMAND_RESULT's correlation is just "the dongle echoes back whatever
    // (source_id, boot_id, sequence) this request's header carried," so this
    // does not need to match BtpHandshake's own HELLO identity.
    quint32 m_clientSourceId;
    quint32 m_clientBootId;
    quint32 m_nextSequence = 1;

    quint32 m_targetSourceId = 0;
    quint32 m_targetBootId = 0;

    // Guards against a late/stray COMMAND_RESULT (e.g. this dongle answering
    // a previous, already-timed-out attempt) being mistaken for the current
    // one.
    quint32 m_pendingSequence = 0;
    Pending m_pending = Pending::None;

    QTimer m_replyTimer;
};

}  // namespace traceview
