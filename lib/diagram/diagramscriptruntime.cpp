#include "diagramscriptruntime.h"

#include <QJSEngine>
#include <QTimer>
#include <utility>

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

DiagramScriptRuntime::~DiagramScriptRuntime() {
    clearAllTimers();
}

bool DiagramScriptRuntime::setScript(const QString& source) {
    // Every live timer closes over a QJSValue that belongs to the engine
    // being discarded below -- letting one fire afterward would call into a
    // dead engine.
    clearAllTimers();
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

void DiagramScriptRuntime::handleConnectionChange(bool connected) {
    callIfDefined("onConnectionChange", {connected});
}

void DiagramScriptRuntime::handleStatus(const QString& text, traceview::StatusSeverity severity) {
    callIfDefined("onStatus", {text, QString::fromLatin1(statusSeverityKey(severity))});
}

void DiagramScriptRuntime::handleDeviceInfo(const QVector<traceview::DeviceInfoRecord>& info) {
    QJSValue array = m_engine->newArray(info.size());
    for (int i = 0; i < info.size(); ++i) {
        QJSValue entry = m_engine->newObject();
        entry.setProperty("key", info[i].key);
        entry.setProperty("label", info[i].label);
        entry.setProperty("value", info[i].value);
        array.setProperty(i, entry);
    }
    callIfDefined("onDeviceInfo", {array});
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

int DiagramScriptRuntime::setInterval(const QJSValue& callback, int intervalMs) {
    return startTimer(callback, intervalMs, /*repeating=*/true);
}

void DiagramScriptRuntime::clearInterval(int id) {
    stopTimer(id);
}

int DiagramScriptRuntime::setTimeout(const QJSValue& callback, int delayMs) {
    return startTimer(callback, delayMs, /*repeating=*/false);
}

void DiagramScriptRuntime::clearTimeout(int id) {
    stopTimer(id);
}

void DiagramScriptRuntime::callIfDefined(const QString& functionName, const QJSValueList& args) {
    invokeCallback(m_engine->globalObject().property(functionName), args);
}

void DiagramScriptRuntime::invokeCallback(QJSValue callback, const QJSValueList& args) {
    if (!callback.isCallable()) {
        return;
    }
    const QJSValue result = callback.call(args);
    if (result.isError()) {
        const QString message = tr("Line %1: %2")
                                    .arg(result.property("lineNumber").toInt())
                                    .arg(result.toString());
        appendToHistory(tr("Error: %1").arg(message));
        emit errorOccurred(message);
    }
}

int DiagramScriptRuntime::startTimer(const QJSValue& callback, int intervalMs, bool repeating) {
    if (!callback.isCallable() || intervalMs < 0) {
        return 0;
    }
    const int id = m_nextTimerId++;
    auto* timer = new QTimer(this);
    timer->setSingleShot(!repeating);
    timer->setInterval(intervalMs);
    connect(timer, &QTimer::timeout, this, [this, id, repeating, callback]() {
        invokeCallback(callback, {});
        if (!repeating) {
            stopTimer(id);
        }
    });
    m_timers.insert(id, timer);
    timer->start();
    return id;
}

void DiagramScriptRuntime::stopTimer(int id) {
    if (QTimer* timer = m_timers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }
}

void DiagramScriptRuntime::clearAllTimers() {
    for (QTimer* timer : std::as_const(m_timers)) {
        timer->stop();
        timer->deleteLater();
    }
    m_timers.clear();
}

void DiagramScriptRuntime::appendToHistory(const QString& line) {
    m_logHistory.append(line);
    if (m_logHistory.size() > kMaxLogHistory) {
        m_logHistory.removeFirst();
    }
}

}  // namespace traceview
