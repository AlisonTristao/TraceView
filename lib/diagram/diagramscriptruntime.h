#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "backend/statusseverity.h"
#include "telemetry/deviceinforecord.h"

class QJSEngine;
class QTimer;

namespace traceview {

// One device's live script engine. Owns a QJSEngine holding whatever the
// script last set (top-level `function onTelemetry(sample) {...}` / `function
// onTerminal(text) {...}` / `function onConnectionChange(connected) {...}` /
// `function onStatus(text, severity) {...}` / `function onDeviceInfo(info)
// {...}` declarations become properties of its global object, same as any JS
// global scope) and exposes itself to that script as the `device` object --
// log()/sendCommand()/sendTerminal()/setInterval()/clearInterval()/
// setTimeout()/clearTimeout() are Q_INVOKABLE so QJSEngine can call them
// directly.
//
// One instance per Device, created by MainWindow::onDeviceAdded() alongside
// its DeviceConnection and kept for that device's whole lifetime -- not tied
// to any UI surface being open. MainWindow feeds handleTelemetry()/
// handleTerminal()/handleConnectionChange()/handleStatus()/handleDeviceInfo()
// from that device's Backend/DeviceConnection regardless of whether the
// script editor (DiagramBlockConfigDialog, opened via the script icon on
// that device's DeviceCard) is currently on screen -- the dialog just gives
// the operator a window into what's already running (pre-fills from
// recentLog(), then attaches to logMessage()/errorOccurred() while open, so
// reopening shows what happened while it was closed too) plus the editor
// that calls setScript(). DiagramPage (diagrampage.h) also uses this class,
// for the shelved canvas-based entry point -- see that file's own note.
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
    // Calls the script's onConnectionChange(connected) function, if it
    // defined one -- mirrors DeviceConnection::connectionStateChanged.
    void handleConnectionChange(bool connected);
    // Calls the script's onStatus(text, severity) function, if it defined
    // one -- mirrors Backend::statusMessage (session established/failed, a
    // subscription rate-limited/rejected, a sendCommand() result -- anything
    // that ends up in the status bar for this device). `severity` is handed
    // over as one of the lowercase strings statusSeverityKey() returns
    // ("info"/"success"/"warning"/"error"), not the C++ enum.
    void handleStatus(const QString& text, traceview::StatusSeverity severity);
    // Calls the script's onDeviceInfo(info) function, if it defined one --
    // mirrors DeviceConnection::deviceInfoReported. `info` is handed over as
    // an array of {key, label, value} objects, one per DeviceInfoRecord.
    void handleDeviceInfo(const QVector<traceview::DeviceInfoRecord>& info);

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
    // device.setInterval(fn, ms) -- calls `fn` every `ms` milliseconds until
    // cancelled. Returns 0 (and starts nothing) if `fn` isn't callable or
    // `ms` is negative. Every live interval is stopped when the script is
    // replaced (setScript()) or this runtime is destroyed -- a script never
    // needs its own cleanup for this.
    Q_INVOKABLE int setInterval(const QJSValue& callback, int intervalMs);
    // device.clearInterval(id) -- no-op for an unknown or already-fired id.
    Q_INVOKABLE void clearInterval(int id);
    // device.setTimeout(fn, ms) -- calls `fn` once, `ms` milliseconds from
    // now. Same id space as setInterval(); clearTimeout()/clearInterval()
    // are interchangeable, same as in a browser.
    Q_INVOKABLE int setTimeout(const QJSValue& callback, int delayMs);
    // device.clearTimeout(id) -- no-op for an unknown or already-fired id.
    Q_INVOKABLE void clearTimeout(int id);

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
    // Shared by callIfDefined() (looked up by name) and the timer callbacks
    // (already held as a QJSValue) -- calling and reporting a callback error
    // is otherwise identical either way.
    void invokeCallback(QJSValue callback, const QJSValueList& args);
    void appendToHistory(const QString& line);
    int startTimer(const QJSValue& callback, int intervalMs, bool repeating);
    void stopTimer(int id);
    // Stops and deletes every live timer -- called from setScript() before
    // the QJSEngine they close over is discarded, and from the destructor.
    void clearAllTimers();

    QJSEngine* m_engine;
    QStringList m_logHistory;
    QHash<int, QTimer*> m_timers;
    int m_nextTimerId = 1;
};

}  // namespace traceview
