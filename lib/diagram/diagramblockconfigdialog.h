#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;

namespace traceview {

class DiagramScriptRuntime;

// Opened on double-clicking a DiagramBlockItem (DiagramScene::blockActivated
// -> DiagramPage). Edits that block's script (JavaScript, run by its
// DiagramScriptRuntime -- see that class for the onTelemetry/onTerminal/
// device.* surface) and shows a live log fed by the runtime's own
// logMessage()/errorOccurred() signals, so activity from *before* this
// dialog was opened isn't shown, only what happens while it's up.
//
// OK only closes the dialog once `runtime`'s script actually parses --
// setScript() failing leaves the dialog open with the error already visible
// in the log, rather than silently discarding the edit. Cancel discards the
// text but leaves the runtime's last successfully-applied script running
// untouched.
class DiagramBlockConfigDialog : public QDialog {
    Q_OBJECT

public:
    DiagramBlockConfigDialog(const QString& blockLabel, const QString& initialScript,
                             DiagramScriptRuntime* runtime, QWidget* parent = nullptr);

    QString script() const;

private:
    void onAccept();

    QPlainTextEdit* m_scriptEdit;
    QPlainTextEdit* m_logView;
    DiagramScriptRuntime* m_runtime;
};

}  // namespace traceview
