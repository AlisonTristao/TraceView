#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;

namespace traceview {

class DiagramScriptRuntime;

// Opened via the script icon on a device's card (DeviceCard::scriptRequested
// -> MainWindow::onDeviceScriptRequested). Edits that device's script
// (JavaScript, run by its DiagramScriptRuntime -- see that class for the
// onTelemetry/onTerminal/device.* surface) and shows a log pre-filled from
// the runtime's recentLog() and then kept live via its logMessage()/
// errorOccurred() signals, so both what happened before this dialog opened
// and what happens while it's up are visible.
//
// Also used, unmodified, by the shelved DiagramPage canvas (diagrampage.h) --
// opened there from a block double-click instead of a card icon, everything
// else the same.
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
