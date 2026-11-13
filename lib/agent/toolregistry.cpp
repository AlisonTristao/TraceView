#include "agent/toolregistry.h"

namespace traceview {

ToolRegistry::ToolRegistry(QObject* parent) : QObject(parent) {}

bool ToolRegistry::registerTool(const AgentTool& tool) {
    if (tool.name.isEmpty() || !tool.handler || find(tool.name)) {
        return false;
    }
    m_tools.append(tool);
    emit toolsChanged();
    return true;
}

void ToolRegistry::unregisterTool(const QString& name) {
    for (int i = 0; i < m_tools.size(); ++i) {
        if (m_tools[i].name == name) {
            m_tools.remove(i);
            emit toolsChanged();
            return;
        }
    }
}

const AgentTool* ToolRegistry::find(const QString& name) const {
    for (const AgentTool& tool : m_tools) {
        if (tool.name == name) {
            return &tool;
        }
    }
    return nullptr;
}

QJsonArray ToolRegistry::toolListJson() const {
    QJsonArray list;
    for (const AgentTool& tool : m_tools) {
        list.append(tool.toJson());
    }
    return list;
}

void ToolRegistry::call(const QString& name, const QJsonObject& args,
                        const AgentToolReply& reply) const {
    const AgentTool* tool = find(name);
    if (!tool) {
        reply(AgentToolResult::error(QStringLiteral("Unknown tool \"%1\".").arg(name)));
        return;
    }
    const QString problem = validateAgentArguments(tool->inputSchema, args);
    if (!problem.isEmpty()) {
        reply(AgentToolResult::error(problem));
        return;
    }
    // Copied: the handler may unregister tools (and so free `tool`) while
    // it runs.
    const auto handler = tool->handler;
    handler(args, reply);
}

}  // namespace traceview
