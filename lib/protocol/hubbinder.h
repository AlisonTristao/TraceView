#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>

namespace traceview {

class BtpSession;
class ProtocolRouter;
struct BtpFrame;

// Tells the hub which robot each of this desktop's child devices is talking
// to, by issuing "hub -bind <child>, <peer>" on the dongle's own shell over
// the parent's channel-A session (the same COMMAND_REQUEST/COMMAND_RESULT
// round trip ClockSync already uses for "dongle -clock").
//
// WHY THIS HAS TO EXIST AT ALL. A BTP header has no destination field -- only
// source_id, which says where a frame came from. Upstream that is enough,
// because the hub has exactly one place to send anything: the cable.
// Downstream it is not, since several children share that cable and each
// means a different robot. COMMAND_REQUEST and SUBSCRIBE carry a target in
// their own payload, but TELEMETRY and TERMINAL carry nothing of the sort, so
// for those the dongle cannot derive a destination from the frame at all. It
// has to be told, out of band. bally_dongle's HubRegistry is where it is
// told, and this class is what tells it.
//
// WHY IT IS AUTOMATIC NOW. That binding used to be an operator typing
// "hub -bind" into the dongle's console, and every path that needed it failed
// SILENTLY without it -- not with an error, but by falling back to the
// dongle's own local handling: a child's TERMINAL_IN got typed into the
// dongle's shell, and its COMMAND_REQUEST got executed on the dongle
// (SerialMux::handleTerminalIn / handleCommandRequest). Adding a robot in the
// UI produced a device that looked connected, plotted nothing, and commanded
// the wrong machine. The desktop already knows both numbers -- the child's
// source_id is derived from its device id (devices/device.h's
// hubChannelSourceId(), stable across relaunches precisely so this table
// survives them) -- so there was never a reason for a human to retype them.
//
// RE-SENDING IS EXPECTED, NOT A RETRY. HubRegistry's table lives in RAM and
// is gone the moment the dongle reboots, and HubRegistry::bind() replaces an
// existing child's peer in place rather than consuming a second slot. So
// every binding is re-issued on each new session, unconditionally, and that
// is the normal path rather than an error path.
class HubBinder : public QObject {
    Q_OBJECT

public:
    explicit HubBinder(BtpSession* session, ProtocolRouter* router, QObject* parent = nullptr);

    // Declares intent: this child device speaks to this robot. Held until
    // unbindChild(), and (re)sent to the dongle whenever a session is up.
    // Sending it twice with the same pair is harmless and costs one command.
    void bindChild(quint32 childSourceId, quint32 peerSourceId);
    // Drops the intent and, if a session is up, tells the dongle to forget it.
    void unbindChild(quint32 childSourceId);

public slots:
    // Wired to BtpHandshake::sessionEstablished, exactly like ClockSync's own
    // slot: peerSourceId/peerBootId are the dongle's identity, which
    // COMMAND_REQUEST needs as target_source_id/target_boot_id.
    void onSessionEstablished(quint32 peerSourceId, quint32 peerBootId);
    void onSessionLost();

signals:
    // Same contract as Backend::statusMessage; BtpBackend forwards it.
    void statusMessage(const QString& text, int timeoutMs);

private slots:
    void onCommandFrameReceived(const traceview::BtpFrame& frame);

private:
    // Formats one source_id the way bally_dongle's parseSourceId() reads it.
    // 0x-prefixed rather than bare hex: parseSourceId tries strtoul base 0
    // first, where a bare "33445566" would be read as DECIMAL and silently
    // bind the wrong number. The prefix removes the ambiguity entirely.
    static QString formatSourceId(quint32 sourceId);
    void sendShellCommand(const QString& commandLine, quint32 aboutChild);
    // (Re)issues every held binding. Called on each new session.
    void resendAll();

    BtpSession* m_session;

    // Private per-process identity, same construction ClockSync uses and for
    // the same reason: COMMAND_RESULT correlation is just the dongle echoing
    // back this request's own (source_id, boot_id, sequence).
    quint32 m_clientSourceId;
    quint32 m_clientBootId;
    quint32 m_nextSequence = 1;

    quint32 m_targetSourceId = 0;
    quint32 m_targetBootId = 0;

    // child_source_id -> peer_source_id, the intent this desktop holds.
    QHash<quint32, quint32> m_bindings;
    // sequence -> child_source_id, so a failed COMMAND_RESULT can name which
    // device it was about. Without this a rejected bind would be as silent as
    // the missing bind it replaced.
    QHash<quint32, quint32> m_inFlight;
};

}  // namespace traceview
