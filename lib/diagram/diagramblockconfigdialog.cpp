#include "diagramblockconfigdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextCursor>
#include <QVBoxLayout>

#include "diagramscriptruntime.h"

namespace traceview {

DiagramBlockConfigDialog::DiagramBlockConfigDialog(const QString& blockLabel,
                                                    const QString& initialScript,
                                                    DiagramScriptRuntime* runtime,
                                                    QWidget* parent)
    : QDialog(parent), m_runtime(runtime) {
    setWindowTitle(tr("Configure %1").arg(blockLabel));
    resize(560, 520);

    auto* layout = new QVBoxLayout(this);

    auto* hint = new QLabel(
        tr("JavaScript. Define onTelemetry(sample) and/or onTerminal(text) to react to this "
           "device's traffic; call device.sendCommand(text), device.sendTerminal(text) or "
           "device.log(text) any time."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* splitter = new QSplitter(Qt::Vertical, this);

    m_scriptEdit = new QPlainTextEdit(splitter);
    m_scriptEdit->setPlaceholderText(tr("// Script for this block"));
    m_scriptEdit->setPlainText(initialScript);
    auto scriptFont = m_scriptEdit->font();
    scriptFont.setFamily("Consolas");
    m_scriptEdit->setFont(scriptFont);
    splitter->addWidget(m_scriptEdit);

    m_logView = new QPlainTextEdit(splitter);
    m_logView->setReadOnly(true);
    m_logView->setPlaceholderText(tr("No log output yet"));
    auto logFont = m_logView->font();
    logFont.setFamily("Consolas");
    m_logView->setFont(logFont);
    m_logView->setPlainText(runtime->recentLog().join('\n'));
    m_logView->moveCursor(QTextCursor::End);
    splitter->addWidget(m_logView);

    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, /*stretch=*/1);

    connect(m_runtime, &DiagramScriptRuntime::logMessage, m_logView, &QPlainTextEdit::appendPlainText);
    connect(m_runtime, &DiagramScriptRuntime::errorOccurred, this, [this](const QString& text) {
        m_logView->appendPlainText(tr("Error: %1").arg(text));
    });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &DiagramBlockConfigDialog::onAccept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString DiagramBlockConfigDialog::script() const {
    return m_scriptEdit->toPlainText();
}

void DiagramBlockConfigDialog::onAccept() {
    if (m_runtime->setScript(m_scriptEdit->toPlainText())) {
        accept();
    }
    // On failure, setScript() has already emitted errorOccurred() (shown in
    // m_logView above) -- leave the dialog open so the operator can fix it.
}

}  // namespace traceview
