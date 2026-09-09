#include "diagramscriptruntime.h"

#include <QJSEngine>

namespace traceview {

namespace {
constexpr int kMaxLogHistory = 200;
}  // namespace

DiagramScriptRuntime::DiagramScriptRuntime(QObject* parent) : QObject(parent) {
    m_engine = new QJSEngine(this);
    // Exposed to the script as `device` -- has a parent (this QObject), so
    // QJSEngine does not take C++ ownership of it (see newQObject()'s own
    // ownership rule), leaving it owned normally by whoever owns this
    // DiagramScriptRuntime.
    m_engine->globalObject().setProperty("device", m_engine->newQObject(this));
}

DiagramScriptRuntime::~DiagramScriptRuntime() = default;

bool DiagramScriptRuntime::setScript(const QString& source) {
    delete m_engine;
    m_engine = new QJSEngine(this);
    m_engine->globalObject().setProperty("device", m_engine->newQObject(this));

    if (source.trimmed().isEmpty()) {
        return true;
    }

    const QJSValue result = m_engine->evaluate(source);
    if (result.isError()) {
        const QString message = tr("Line %1: %2")
                                    .arg(result.property("lineNumber").toInt())
                                    .arg(result.toString());
        appendToHistory(tr("Error: %1").arg(message));
        emit errorOccurred(message);
        return false;
    }
    return true;
}

void DiagramScriptRuntime::handleTelemetry(quint16 topicId, quint16 fieldId, quint16 elementIndex,
                                           double value, quint64 timestampUs) {
    QJSValue sample = m_engine->newObject();
    sample.setProperty("topicId", topicId);
    sample.setProperty("fieldId", fieldId);
    sample.setProperty("elementIndex", elementIndex);
    sample.setProperty("value", value);
    sample.setProperty("timestampUs", double(timestampUs));
    callIfDefined("onTelemetry", {sample});
}

void DiagramScriptRuntime::handleTerminal(const QString& text) {
    callIfDefined("onTerminal", {text});
}

void DiagramScriptRuntime::log(const QString& text) {
    appendToHistory(text);
    emit logMessage(text);
}

void DiagramScriptRuntime::sendCommand(const QString& text) {
    emit sendCommandRequested(text);
}

void DiagramScriptRuntime::sendTerminal(const QString& text) {
    emit sendTerminalRequested(text);
}

void DiagramScriptRuntime::callIfDefined(const QString& functionName, const QJSValueList& args) {
    QJSValue fn = m_engine->globalObject().property(functionName);
    if (!fn.isCallable()) {
        return;
    }
    const QJSValue result = fn.call(args);
    if (result.isError()) {
        const QString message = tr("Line %1: %2")
                                    .arg(result.property("lineNumber").toInt())
                                    .arg(result.toString());
        appendToHistory(tr("Error: %1").arg(message));
        emit errorOccurred(message);
    }
}

void DiagramScriptRuntime::appendToHistory(const QString& line) {
    m_logHistory.append(line);
    if (m_logHistory.size() > kMaxLogHistory) {
        m_logHistory.removeFirst();
    }
}

}  // namespace traceview
