#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace traceview {

// What one tool call hands back to the MCP client: MCP content blocks (text,
// image) plus whether the call failed. A failed call is still a normal
// tools/call result with isError set, not a JSON-RPC error -- the model reads
// the message and can correct its arguments; JSON-RPC errors stay reserved
// for protocol faults (unknown method, malformed request).
struct AgentToolResult {
    QJsonArray content;
    bool isError = false;
    // MCP structuredContent, sent alongside the text when non-empty.
    QJsonObject structured;

    static AgentToolResult text(const QString& text);
    // Indented JSON as the text block, the object itself as structuredContent.
    static AgentToolResult json(const QJsonObject& object);
    static AgentToolResult error(const QString& message);
    static AgentToolResult image(const QByteArray& png, const QString& caption = QString());

    QJsonObject toJson() const;
};

using AgentToolReply = std::function<void(const AgentToolResult&)>;

// One operation an AI assistant can perform through the MCP server (see
// docs/MCP.md). Registered into a ToolRegistry by the layer that owns the
// feature -- MainWindow's tools in core/agentapptools.cpp, a module's own
// tools by that module.
struct AgentTool {
    QString name;  // snake_case verb_noun, unique within the registry
    QString title;
    // What the model reads to decide when and how to call the tool: say what
    // it does, what it returns and which other tool gives the ids it needs.
    QString description;
    QJsonObject inputSchema;  // a JSON Schema "object" (see AgentSchema)
    bool readOnly = false;
    // Deletes something that undo cannot bring back.
    bool destructive = false;
    // Runs on the registry's thread (the GUI thread). Must call `reply`
    // exactly once, right away or later (after a confirmation dialog, a
    // simulation run...). `args` already passed the schema check.
    std::function<void(const QJsonObject& args, const AgentToolReply& reply)> handler;

    QJsonObject toJson() const;
};

// Fluent builder for the flat argument schemas tools use, so a tool
// definition reads as its argument list instead of nested JSON literals.
class AgentSchema {
public:
    AgentSchema& string(const QString& name, const QString& description, bool required = false,
                        const QStringList& allowed = QStringList());
    AgentSchema& integer(const QString& name, const QString& description, bool required = false);
    AgentSchema& number(const QString& name, const QString& description, bool required = false);
    AgentSchema& boolean(const QString& name, const QString& description, bool required = false);
    // `schema` describes the object's own members; empty means free-form.
    AgentSchema& object(const QString& name, const QString& description, bool required = false,
                        const QJsonObject& schema = QJsonObject());
    AgentSchema& array(const QString& name, const QString& description, const QJsonObject& items,
                       bool required = false);

    QJsonObject toJson() const;

private:
    AgentSchema& add(const QString& name, QJsonObject property, bool required);

    QJsonObject m_properties;
    QJsonArray m_required;
};

// Empty when `args` satisfies `schema`'s required members and top-level
// member types, else a message naming the first offending argument. Nested
// objects are left to the handler, which knows their meaning.
QString validateAgentArguments(const QJsonObject& schema, const QJsonObject& args);

}  // namespace traceview
