#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <btp/node.hpp>
#include <functional>

#include "backend/statusseverity.h"

namespace traceview {

class TelemetryCatalog;

// Sends COMMAND_REQUESTs -- action_id=1/action_version=1, "run this line as a
// shell command", the convention bally_dongle and bally_OS both define
// (BTP/docs/commands.md section 2.1) -- to a fixed target, and reports each
// COMMAND_RESULT via statusMessage() and, line by line, via logLine() -- the
// device's log panel, not its terminal: text pushed into the terminal stream
// lands in the middle of the robot's line editor (its prompt redraws) and
// garbles whatever the user is typing there. This is
// what control widgets use to reach a robot (hub channel, TCP, BLE, or the
// robot's own serial port) instead of TERMINAL_IN: bally_OS's dispatch drops
// every TERMINAL frame it receives on channel B, and a COMMAND carries its own
// result back.
//
// Wire mechanics (encode COMMAND_REQUEST, seal it, correlate COMMAND_RESULT,
// time it out) are btp::Node's btp::CommandClient (library 2.14.0+) --
// node.command() / node.command_outcome(), reached through the btp::Node
// BtpBackend owns. This class keeps the policy Node has no notion of:
// refusing to send with no target configured, no endpoint key, or no free
// slot, and the user-facing text. Several requests can be outstanding at once
// (up to the node's MaxCommands): a robot runs its shell lines one after the
// other, so a stop pressed while a homing is still running must not be
// dropped here.
//
// notifyOutcome() is the one entry point BtpBackend calls, from two places:
// right after node.receive() delivers NodeRx::CommandHandled, and after
// every node.tick() (the only way a TimedOut outcome -- which tick() alone
// produces, with no NodeRx of its own -- is ever noticed). Either call is a
// no-op unless the outcome correlates to one of OUR outstanding local_ids.
class CommandClient : public QObject {
    Q_OBJECT

public:
    explicit CommandClient(btp::Node& node, QObject* parent = nullptr);

    // `targetSourceId` is the robot this backend addresses (0 = unconfigured,
    // console-facing). `hasEndpointKey` is asked at send() time -- true once
    // BtpBackend::setHubEndpoint() has a non-empty endpoint key, so a
    // COMMAND_REQUEST never goes out in the clear to a robot (node itself has
    // no "refuse without a key" mode -- has_seal()==false there just means
    // cleartext, which this class must not let happen for a hub target).
    void configure(quint32 targetSourceId, TelemetryCatalog* catalog,
                  std::function<bool()> hasEndpointKey);

public slots:
    // No-op, silently, when not configured. Otherwise a status message says
    // why nothing went out (no key, target's boot_id not known yet -- no
    // MANIFEST_DATA from it in this process -- or every slot busy).
    void send(const QString& commandLine);

signals:
    void statusMessage(const QString& text, int timeoutMs,
                       traceview::StatusSeverity severity = traceview::StatusSeverity::Info);
    // The command as sent ("> line") and then its result -- the robot's shell
    // output, ANSI colours stripped, or why it failed -- one line per
    // emission, for the device's log panel (Backend::logReceived).
    // `severity` is a traceview::LogSeverity.
    void logLine(quint8 severity, const QString& text);

public:
    // See the class comment. Reads node.command_outcome() itself; safe to
    // call unconditionally.
    void notifyOutcome();

private:
    btp::Node& m_node;
    TelemetryCatalog* m_catalog = nullptr;

    quint32 m_targetSourceId = 0;
    std::function<bool()> m_hasEndpointKey;

    QHash<std::uint32_t, QString> m_pending;  // local_id -> command line
};

}  // namespace traceview
