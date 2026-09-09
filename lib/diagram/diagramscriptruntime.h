#pragma once

#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>

class QJSEngine;

namespace traceview {

// One block's live script engine. Owns a QJSEngine holding whatever the
// script last set (top-level `function onTelemetry(sample) {...}` / `function
// onTerminal(text) {...}` declarations become properties of its global
// object, same as any JS global scope) and exposes itself to that script as
// the `device` object -- log()/sendCommand()/sendTerminal() are Q_INVOKABLE
// so QJSEngine can call them directly.
//
// Lives independently of DiagramBlockConfigDialog: MainWindow feeds
// handleTelemetry()/handleTerminal() from that device's Backend for as long
// as the Control Diagram tab is open, whether or not the config dialog is
// currently on screen -- the dialog just gives the operator a window into
// what's already running (pre-fills from recentLog(), then attaches to
// logMessage()/errorOccurred() while open, so reopening shows what happened
// while it was closed too) plus the editor that calls setScript().
class DiagramScriptRuntime : public QObject {
    Q_OBJECT

public:
    explicit DiagramScriptRuntime(QObject* parent = nullptr);
    ~DiagramScriptRuntime() override;

    // Replaces the running script with `source`: discards the previous
    // QJSEngine (and any state it held) and evaluates the new one fresh.
    // Returns false -- and emits errorOccurred() -- if `source` itself
    // fails to parse/evaluate; the runtime is left with an empty script in
    // that case (no onTelemetry/onTerminal handlers), not the old one.
    bool setScript(const QString& source);

    // Calls the script's onTelemetry(sample) function, if it defined one --
    // a no-op otherwise. `sample` is handed over as a plain JS object with
    // topicId/fieldId/elementIndex/value/timestampUs properties.
    void handleTelemetry(quint16 topicId, quint16 fieldId, quint16 elementIndex, double value,
                         quint64 timestampUs);
    // Calls the script's onTerminal(text) function, if it defined one.
    void handleTerminal(const QString& text);

    // The last kMaxLogHistory lines this runtime has logged (device.log()
    // calls and errorOccurred() messages, oldest first) -- survives a
    // DiagramBlockConfigDialog being closed and reopened, though not the
    // block itself being removed or the app restarting (never persisted to
    // .tvproj: this is a live diagnostics trail, not configuration).
    QStringList recentLog() const {
        return m_logHistory;
    }

    // device.log(text) from the script.
    Q_INVOKABLE void log(const QString& text);
    // device.sendCommand(text) -- relayed outward as sendCommandRequested().
    Q_INVOKABLE void sendCommand(const QString& text);
    // device.sendTerminal(text) -- relayed outward as sendTerminalRequested().
    Q_INVOKABLE void sendTerminal(const QString& text);

signals:
    // A device.log() call, or the result of console.log() (QJSEngine wires
    // that to qDebug() by default; this app has no interest in the app log
    // for script output, so log() is the one console-like surface offered).
    void logMessage(const QString& text);
    // A script parse/evaluate/callback error -- includes the line number
    // when QJSEngine reports one.
    void errorOccurred(const QString& text);
    void sendCommandRequested(const QString& text);
    void sendTerminalRequested(const QString& text);

private:
    void callIfDefined(const QString& functionName, const QJSValueList& args);
    void appendToHistory(const QString& line);

    QJSEngine* m_engine;
    QStringList m_logHistory;
};

}  // namespace traceview
