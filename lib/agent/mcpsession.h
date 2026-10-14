#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace traceview {

class ToolRegistry;

// MCP protocol versions this server speaks, newest first.
QStringList mcpSupportedProtocolVersions();
// The version to answer an initialize request that asked for `requested`:
// the same one when supported, else our newest (the client then decides
// whether it can continue, per the MCP lifecycle spec).
QString mcpNegotiateProtocolVersion(const QString& requested);
// The initialize result's `instructions`: what an assistant should know about
// TraceView before calling any tool. Shared by McpSession and the stdio relay.
QString mcpServerInstructions();
// initialize's result object, built the same way by McpSession and the relay.
QJsonObject mcpInitializeResult(const QString& requestedVersion);

// JSON-RPC builders shared by McpSession and the stdio relay.
QJsonObject jsonRpcResult(const QJsonValue& id, const QJsonValue& result);
QJsonObject jsonRpcError(const QJsonValue& id, int code, const QString& message);

// One MCP client connection: decodes JSON-RPC 2.0 messages, answers the
// lifecycle requests itself and dispatches tools/list and tools/call to a
// ToolRegistry. Transport-agnostic -- whoever owns it feeds whole messages
// to receive() and writes whatever send() emits (AgentBridgeServer frames
// them as newline-delimited JSON over a local socket).
class McpSession : public QObject {
    Q_OBJECT

public:
    explicit McpSession(ToolRegistry* registry, QObject* parent = nullptr);

    void receive(const QByteArray& message);

    // From the client's initialize request; empty until one arrived.
    QString clientName() const {
        return m_clientName;
    }

signals:
    // One complete JSON-RPC message, compact, without a trailing newline.
    void send(const QByteArray& message);
    void clientIdentified(const QString& name, const QString& version);
    void toolCalled(const QString& name);

private:
    void handleRequest(const QJsonValue& id, const QString& method, const QJsonObject& params);
    void callTool(const QJsonValue& id, const QJsonObject& params);
    void sendObject(const QJsonObject& object);

    QPointer<ToolRegistry> m_registry;
    QString m_clientName;
};

}  // namespace traceview
