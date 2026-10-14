#include "agent/agenttool.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <cmath>

namespace traceview {

AgentToolResult AgentToolResult::text(const QString& text) {
    AgentToolResult result;
    result.content.append(QJsonObject{{"type", "text"}, {"text", text}});
    return result;
}

AgentToolResult AgentToolResult::json(const QJsonObject& object) {
    AgentToolResult result =
        text(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)));
    result.structured = object;
    return result;
}

AgentToolResult AgentToolResult::error(const QString& message) {
    AgentToolResult result = text(message);
    result.isError = true;
    return result;
}

AgentToolResult AgentToolResult::image(const QByteArray& png, const QString& caption) {
    AgentToolResult result;
    if (!caption.isEmpty()) {
        result.content.append(QJsonObject{{"type", "text"}, {"text", caption}});
    }
    result.content.append(QJsonObject{{"type", "image"},
                                      {"data", QString::fromLatin1(png.toBase64())},
                                      {"mimeType", "image/png"}});
    return result;
}

QJsonObject AgentToolResult::toJson() const {
    QJsonObject object{{"content", content}, {"isError", isError}};
    if (!structured.isEmpty()) {
        object["structuredContent"] = structured;
    }
    return object;
}

QJsonObject AgentTool::toJson() const {
    QJsonObject annotations{
        {"readOnlyHint", readOnly},
        {"destructiveHint", destructive},
        // Everything a tool touches is inside this app.
        {"openWorldHint", false},
    };
    if (!title.isEmpty()) {
        annotations["title"] = title;
    }
    QJsonObject object{
        {"name", name},
        {"description", description},
        {"inputSchema", inputSchema.isEmpty() ? AgentSchema().toJson() : inputSchema},
        {"annotations", annotations},
    };
    if (!title.isEmpty()) {
        object["title"] = title;
    }
    return object;
}

AgentSchema& AgentSchema::string(const QString& name, const QString& description, bool required,
                                 const QStringList& allowed) {
    QJsonObject property{{"type", "string"}, {"description", description}};
    if (!allowed.isEmpty()) {
        property["enum"] = QJsonArray::fromStringList(allowed);
    }
    return add(name, property, required);
}

AgentSchema& AgentSchema::integer(const QString& name, const QString& description, bool required) {
    return add(name, {{"type", "integer"}, {"description", description}}, required);
}

AgentSchema& AgentSchema::number(const QString& name, const QString& description, bool required) {
    return add(name, {{"type", "number"}, {"description", description}}, required);
}

AgentSchema& AgentSchema::boolean(const QString& name, const QString& description, bool required) {
    return add(name, {{"type", "boolean"}, {"description", description}}, required);
}

AgentSchema& AgentSchema::object(const QString& name, const QString& description, bool required,
                                 const QJsonObject& schema) {
    QJsonObject property = schema;
    property["type"] = "object";
    property["description"] = description;
    return add(name, property, required);
}

AgentSchema& AgentSchema::array(const QString& name, const QString& description,
                                const QJsonObject& items, bool required) {
    return add(name, {{"type", "array"}, {"description", description}, {"items", items}}, required);
}

QJsonObject AgentSchema::toJson() const {
    QJsonObject schema{
        {"type", "object"},
        {"properties", m_properties},
        {"additionalProperties", false},
    };
    if (!m_required.isEmpty()) {
        schema["required"] = m_required;
    }
    return schema;
}

AgentSchema& AgentSchema::add(const QString& name, QJsonObject property, bool required) {
    m_properties[name] = property;
    if (required) {
        m_required.append(name);
    }
    return *this;
}

namespace {

bool matchesType(const QJsonValue& value, const QString& type) {
    if (type == QLatin1String("string")) {
        return value.isString();
    }
    if (type == QLatin1String("integer")) {
        return value.isDouble() && std::floor(value.toDouble()) == value.toDouble();
    }
    if (type == QLatin1String("number")) {
        return value.isDouble();
    }
    if (type == QLatin1String("boolean")) {
        return value.isBool();
    }
    if (type == QLatin1String("object")) {
        return value.isObject();
    }
    if (type == QLatin1String("array")) {
        return value.isArray();
    }
    return true;
}

}  // namespace

QString validateAgentArguments(const QJsonObject& schema, const QJsonObject& args) {
    const QJsonObject properties = schema.value("properties").toObject();
    for (const QJsonValue& required : schema.value("required").toArray()) {
        if (!args.contains(required.toString())) {
            return QStringLiteral("Missing required argument \"%1\".").arg(required.toString());
        }
    }
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        if (!properties.contains(it.key())) {
            return QStringLiteral("Unknown argument \"%1\". Expected: %2.")
                .arg(it.key(), properties.keys().join(QStringLiteral(", ")));
        }
        const QJsonObject property = properties.value(it.key()).toObject();
        const QString type = property.value("type").toString();
        if (!matchesType(it.value(), type)) {
            return QStringLiteral("Argument \"%1\" must be of type %2.").arg(it.key(), type);
        }
        const QJsonArray allowed = property.value("enum").toArray();
        if (!allowed.isEmpty() && !allowed.contains(it.value())) {
            QStringList names;
            for (const QJsonValue& value : allowed) {
                names.append(value.toString());
            }
            return QStringLiteral("Argument \"%1\" must be one of: %2.")
                .arg(it.key(), names.join(QStringLiteral(", ")));
        }
    }
    return QString();
}

}  // namespace traceview
