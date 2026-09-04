#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <btp/node.hpp>
#include <functional>

#include "backend/statusseverity.h"

namespace traceview {

class TelemetryCatalog;

// Sends one COMMAND_REQUEST at a time -- action_id=1/action_version=1, "run
// this line as a shell command", the convention bally_dongle and bally_OS
// both define (BTP/docs/commands.md section 2.1) -- to a fixed target, and
// reports the COMMAND_RESULT via statusMessage(). This is what a hub-channel
// device's control widgets need instead of TERMINAL_IN: bally_OS's dispatch
// drops every TERMINAL frame it receives (no handler exists), and only
// COMMAND has been widened to accept channel B (key E) so far.
//
// Wire mechanics (encode COMMAND_REQUEST, seal it, hold one outstanding
// request, correlate COMMAND_RESULT, time it out) are btp::Node's
// btp::CommandClient (library 2.14.0+) now -- node.command() / node.
// command_outcome(), reached through the btp::Node BtpBackend owns. This
// class keeps the policy Node has no notion of: refusing to send with no
// target configured, no endpoint key, or a request already in flight
// (mirrors BtpBackend::setHubEndpoint()'s all-or-nothing contract -- a plain
// console-facing backend never configure()s a target, so send() stays a
// silent no-op), and the user-facing statusMessage() text.
//
// notifyOutcome() is the one entry point BtpBackend calls, from two places:
// right after node.receive() delivers NodeRx::CommandHandled, and after
// every node.tick() (the only way a TimedOut outcome -- which tick() alone
// produces, with no NodeRx of its own -- is ever noticed). Either call is a
// no-op unless the outcome correlates to OUR OWN outstanding local_id.
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
    // No-op, silently, when not configured, when a previous command is still
    // awaiting its result, or when the target's boot_id is not known yet (no
    // MANIFEST_DATA from it in this process). One outstanding request at a
    // time: a second press before the first result lands has no correlated
    // place to show a second answer anyway.
    void send(const QString& commandLine);

signals:
    void statusMessage(const QString& text, int timeoutMs,
                       traceview::StatusSeverity severity = traceview::StatusSeverity::Info);

public:
    // See the class comment. Reads node.command_outcome() itself; safe to
    // call unconditionally.
    void notifyOutcome();

private:
    static constexpr std::uint64_t kReplyTimeoutMs = 5000U;  // matches btp::kCommandTimeoutMs

    btp::Node& m_node;
    TelemetryCatalog* m_catalog = nullptr;

    quint32 m_targetSourceId = 0;
    std::function<bool()> m_hasEndpointKey;

    std::uint32_t m_pendingLocalId = 0;
};

}  // namespace traceview
