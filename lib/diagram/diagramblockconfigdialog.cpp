#include "diagramblockconfigdialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "diagramscriptruntime.h"
#include "traceview/thememanager.h"

namespace traceview {

namespace {
// Applies a visible border to `edit` in `color` -- QPlainTextEdit gets no
// border from this app's global stylesheet (theme/stylesheet.cpp only
// covers QLineEdit/QComboBox/QSpinBox), so without this the script editor
// and the log view are both a flat white/surface rectangle with nothing
// showing where one ends and the other begins.
void applyPaneBorder(QPlainTextEdit* edit, const QColor& color) {
    edit->setStyleSheet(
        QString("QPlainTextEdit { border: 1px solid %1; border-radius: 4px; }").arg(color.name()));
}

QWidget* buildHelpMenuContent(QWidget* parent) {
    auto* label = new QLabel(
        QObject::tr("<b>JavaScript</b>, run live against this device's real traffic.<br><br>"
                    "<b>Define to react to traffic:</b><br>"
                    "&nbsp;&nbsp;<code>onTelemetry(sample)</code> -- called for a telemetry "
                    "value already subscribed elsewhere (e.g. a Dashboard chart). "
                    "<code>sample</code> has <code>topicId</code>, <code>fieldId</code>, "
                    "<code>elementIndex</code>, <code>value</code>, <code>timestampUs</code>.<br>"
                    "&nbsp;&nbsp;<code>onTerminal(text)</code> -- called for each chunk of text "
                    "this device's console/serial channel sends back.<br>"
                    "&nbsp;&nbsp;<code>onConnectionChange(connected)</code> -- called when this "
                    "device connects or disconnects.<br>"
                    "&nbsp;&nbsp;<code>onStatus(text, severity)</code> -- called for a one-off "
                    "status update (session established/failed, a subscription rejected, a "
                    "sendCommand() result, ...) -- the same text the status bar shows. "
                    "<code>severity</code> is one of <code>\"info\"</code>, "
                    "<code>\"success\"</code>, <code>\"warning\"</code>, <code>\"error\"</code>.<br>"
                    "&nbsp;&nbsp;<code>onDeviceInfo(info)</code> -- called when this device "
                    "reports its info block (firmware version, chip, partition, ...). "
                    "<code>info</code> is an array of <code>{key, label, value}</code>.<br><br>"
                    "<b>Call any time:</b><br>"
                    "&nbsp;&nbsp;<code>device.log(text)</code> -- write to the output pane.<br>"
                    "&nbsp;&nbsp;<code>device.sendCommand(text)</code> -- send a command to "
                    "this device.<br>"
                    "&nbsp;&nbsp;<code>device.sendTerminal(text)</code> -- send text over its "
                    "console channel.<br>"
                    "&nbsp;&nbsp;<code>device.setInterval(fn, ms)</code> / "
                    "<code>device.clearInterval(id)</code> -- run <code>fn</code> every "
                    "<code>ms</code> milliseconds until cancelled.<br>"
                    "&nbsp;&nbsp;<code>device.setTimeout(fn, ms)</code> / "
                    "<code>device.clearTimeout(id)</code> -- run <code>fn</code> once, "
                    "<code>ms</code> milliseconds from now."),
        parent);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setMargin(8);
    label->setMaximumWidth(360);
    return label;
}
}  // namespace

DiagramBlockConfigDialog::DiagramBlockConfigDialog(const QString& blockLabel,
                                                    const QString& initialScript,
                                                    DiagramScriptRuntime* runtime,
                                                    QWidget* parent)
    : QDialog(parent), m_runtime(runtime) {
    setWindowTitle(tr("Configure %1").arg(blockLabel));
    resize(560, 520);

    auto* layout = new QVBoxLayout(this);

    // A dropdown instead of a permanent hint label -- keeps the dialog's
    // fixed real estate for what actually changes (the code and its output),
    // with the API reference a click away rather than always taking up
    // space.
    auto* helpMenu = new QMenu(this);
    auto* helpAction = new QWidgetAction(helpMenu);
    helpAction->setDefaultWidget(buildHelpMenuContent(helpMenu));
    helpMenu->addAction(helpAction);

    auto* helpButton = new QToolButton(this);
    // "▾" (small down triangle) baked into the label instead of relying
    // on QToolButton's native menu-indicator glyph: that indicator is a
    // separate subcontrol the app-wide stylesheet (theme/stylesheet.cpp)
    // doesn't position, and every attempt to fix its placement with
    // ::menu-indicator rules still left it a couple pixels off-center
    // vertically. Baked into the text, it's just another character on the
    // same baseline -- centers itself for free.
    helpButton->setText(tr("Help ▾"));
    helpButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    helpButton->setPopupMode(QToolButton::InstantPopup);
    helpButton->setMenu(helpMenu);
    // Suppresses the native indicator now that the triangle lives in the
    // text -- otherwise the button would show two.
    helpButton->setStyleSheet("QToolButton::menu-indicator { image: none; width: 0px; }");

    auto* toolbar = new QHBoxLayout();
    toolbar->addStretch();
    toolbar->addWidget(helpButton);
    layout->addLayout(toolbar);

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

    const auto refreshBorders = [this] {
        const QColor border = ThemeManager::instance().currentTheme().border;
        applyPaneBorder(m_scriptEdit, border);
        applyPaneBorder(m_logView, border);
    };
    refreshBorders();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [refreshBorders](const ThemePalette&) { refreshBorders(); });

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
