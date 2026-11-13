#pragma once

#include <QJsonArray>
#include <QObject>
#include <QVector>

#include "agent/agenttool.h"

namespace traceview {

// The set of tools the MCP server offers. Every feature that wants to be
// drivable by an AI assistant registers its tools here (see CONTRIBUTING.md
// "Exposing features to AI assistants"); the protocol side (McpSession) only
// lists and calls what it finds, so adding a tool never touches it.
class ToolRegistry : public QObject {
    Q_OBJECT

public:
    explicit ToolRegistry(QObject* parent = nullptr);

    // False, keeping the first one, if a tool of that name already exists --
    // a duplicate is a programming error, caught by the tests.
    bool registerTool(const AgentTool& tool);
    // No-op for an unknown name.
    void unregisterTool(const QString& name);

    const QVector<AgentTool>& tools() const {
        return m_tools;
    }
    const AgentTool* find(const QString& name) const;
    QJsonArray toolListJson() const;

    // Checks `args` against the tool's schema and runs it; an unknown tool or
    // bad arguments reply with an error result instead of reaching it.
    void call(const QString& name, const QJsonObject& args, const AgentToolReply& reply) const;

signals:
    // Sessions forward it as notifications/tools/list_changed.
    void toolsChanged();

private:
    QVector<AgentTool> m_tools;
};

}  // namespace traceview
