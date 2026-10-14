#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QLocalServer;
class QLocalSocket;

namespace traceview {

class McpSession;
class ToolRegistry;

// `TraceView --mcp` runs the stdio relay instead of the app (see
// mcpstdiorelay.h); `TraceView --agent-bridge` is the app with its bridge on
// for that run, whatever the Settings toggle says -- what start_traceview
// launches.
inline constexpr char kMcpRelayArgument[] = "--mcp";
inline constexpr char kAgentBridgeArgument[] = "--agent-bridge";

// The command an MCP client should run to reach this installation: the
// executable plus kMcpRelayArgument -- through `flatpak run <id>` inside a
// Flatpak, and the .AppImage file itself (not its temporary mount) inside an
// AppImage. First element is the program.
QStringList mcpServerCommand();
// Starts this installation's TraceView with its bridge on for the run
// (kAgentBridgeArgument), detached from the caller. Empty on success, else
// why not -- a Flatpak sandbox can't start an instance that outlives it.
QString launchAgentBridgeApp();

// The local socket `TraceView --mcp` (McpStdioRelay) connects to: a named
// pipe on Windows, a Unix socket elsewhere, both created owner-only so other
// users on the machine cannot reach it. One McpSession per connection,
// newline-delimited JSON framing (MCP's stdio framing, which the relay passes
// through unchanged).
class AgentBridgeServer : public QObject {
    Q_OBJECT

public:
    explicit AgentBridgeServer(ToolRegistry* registry, QObject* parent = nullptr);
    ~AgentBridgeServer() override;

    // Per-user, so two accounts on one machine each get their own.
    static QString socketName();
    // Defaults to socketName(); tests use their own so a running TraceView
    // doesn't answer them. Takes effect on the next start().
    void setSocketName(const QString& name) {
        m_socketName = name;
    }

    // False with `error` set when another TraceView instance already serves
    // the socket or the OS refuses it. Idempotent while listening.
    bool start(QString* error = nullptr);
    // Drops every connected client.
    void stop();
    bool isListening() const;
    int clientCount() const {
        return m_sessions.size();
    }

signals:
    void clientConnected(const QString& clientName);
    void clientDisconnected(const QString& clientName);
    void toolCalled(const QString& clientName, const QString& toolName);

private:
    void onNewConnection();
    void onReadyRead(QLocalSocket* socket);

    ToolRegistry* m_registry;
    QString m_socketName;
    QLocalServer* m_server = nullptr;
    QHash<QLocalSocket*, McpSession*> m_sessions;
    QHash<QLocalSocket*, QByteArray> m_buffers;
};

}  // namespace traceview
