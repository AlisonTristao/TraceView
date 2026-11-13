#include "agent/mcpsession.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>

#include "agent/toolregistry.h"

namespace traceview {

namespace {

// JSON-RPC 2.0 error codes.
constexpr int kParseError = -32700;
constexpr int kInvalidRequest = -32600;
constexpr int kMethodNotFound = -32601;
constexpr int kInvalidParams = -32602;

}  // namespace

QStringList mcpSupportedProtocolVersions() {
    return {QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"),
            QStringLiteral("2024-11-05")};
}

QString mcpNegotiateProtocolVersion(const QString& requested) {
    const QStringList supported = mcpSupportedProtocolVersions();
    return supported.contains(requested) ? requested : supported.first();
}

QString mcpServerInstructions() {
    return QStringLiteral(
        "TraceView is a telemetry dashboard for robots. A project holds devices and "
        "workspaces; each workspace is a dashboard of widgets (charts, gauges, controls) bound "
        "to device telemetry by deviceId + sourceId + topicId + fieldId. Start with "
        "get_app_state, then list_signals for the bindable fields and list_widget_types for "
        "each widget's config format. Dashboard edits act on the active workspace "
        "(switch_workspace first) and are undoable (undo). Take a screenshot to check a "
        "layout. Nothing is written to disk until save_project.");
}

QJsonObject mcpInitializeResult(const QString& requestedVersion) {
    return QJsonObject{
        {"protocolVersion", mcpNegotiateProtocolVersion(requestedVersion)},
        {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", true}}}}},
        {"serverInfo", QJsonObject{{"name", "traceview"},
                                   {"title", "TraceView"},
                                   {"version", QCoreApplication::applicationVersion()}}},
        {"instructions", mcpServerInstructions()},
    };
}

QJsonObject jsonRpcResult(const QJsonValue& id, const QJsonValue& result) {
    return QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

QJsonObject jsonRpcError(const QJsonValue& id, int code, const QString& message) {
    return QJsonObject{{"jsonrpc", "2.0"},
                       {"id", id},
                       {"error", QJsonObject{{"code", code}, {"message", message}}}};
}

McpSession::McpSession(ToolRegistry* registry, QObject* parent)
    : QObject(parent), m_registry(registry) {
    if (registry) {
        connect(registry, &ToolRegistry::toolsChanged, this, [this] {
            sendObject(
                QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/tools/list_changed"}});
        });
    }
}

void McpSession::receive(const QByteArray& message) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        sendObject(jsonRpcError(QJsonValue::Null, kParseError, parseError.errorString()));
        return;
    }
    if (!document.isObject()) {
        sendObject(jsonRpcError(QJsonValue::Null, kInvalidRequest,
                                QStringLiteral("Expected a JSON-RPC object.")));
        return;
    }
    const QJsonObject object = document.object();
    const QString method = object.value("method").toString();
    if (method.isEmpty()) {
        // A response to something we sent. We send no requests, so there is
        // nothing to match it to.
        return;
    }
    if (!object.contains("id")) {
        // Notifications (initialized, cancelled) need no answer.
        return;
    }
    handleRequest(object.value("id"), method, object.value("params").toObject());
}

void McpSession::handleRequest(const QJsonValue& id, const QString& method,
                               const QJsonObject& params) {
    if (method == QLatin1String("initialize")) {
        const QJsonObject clientInfo = params.value("clientInfo").toObject();
        m_clientName = clientInfo.value("name").toString();
        emit clientIdentified(m_clientName, clientInfo.value("version").toString());
        sendObject(
            jsonRpcResult(id, mcpInitializeResult(params.value("protocolVersion").toString())));
    } else if (method == QLatin1String("ping")) {
        sendObject(jsonRpcResult(id, QJsonObject()));
    } else if (method == QLatin1String("tools/list")) {
        const QJsonArray tools = m_registry ? m_registry->toolListJson() : QJsonArray();
        sendObject(jsonRpcResult(id, QJsonObject{{"tools", tools}}));
    } else if (method == QLatin1String("tools/call")) {
        callTool(id, params);
    } else {
        sendObject(jsonRpcError(id, kMethodNotFound,
                                QStringLiteral("Method \"%1\" is not supported.").arg(method)));
    }
}

void McpSession::callTool(const QJsonValue& id, const QJsonObject& params) {
    const QString name = params.value("name").toString();
    if (name.isEmpty()) {
        sendObject(jsonRpcError(id, kInvalidParams, QStringLiteral("Missing tool name.")));
        return;
    }
    if (!m_registry) {
        sendObject(jsonRpcResult(id, AgentToolResult::error("No tools available.").toJson()));
        return;
    }
    emit toolCalled(name);
    // A handler may answer after this session is gone (the client hung up
    // during a dialog) or, by mistake, twice; only the first answer to a live
    // session is sent.
    QPointer<McpSession> self(this);
    auto answered = std::make_shared<bool>(false);
    m_registry->call(name, params.value("arguments").toObject(),
                     [self, id, answered](const AgentToolResult& result) {
                         if (*answered || !self) {
                             return;
                         }
                         *answered = true;
                         self->sendObject(jsonRpcResult(id, result.toJson()));
                     });
}

void McpSession::sendObject(const QJsonObject& object) {
    emit send(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace traceview
