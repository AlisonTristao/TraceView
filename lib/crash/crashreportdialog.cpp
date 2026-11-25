#include "crash/crashreportdialog.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "crash/crashhandler.h"
#include "theme/dialogchrome.h"
#include "theme/dialogpresenter.h"

namespace traceview {

namespace {

// One small "Version / System / When" card.
DialogCard* infoCard(const QString& caption, const QString& value, QWidget* parent) {
    auto* card = new DialogCard(caption, parent);
    auto* label = new QLabel(value, card);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    card->body()->addWidget(label);
    return card;
}

}  // namespace

CrashReportDialog::CrashReportDialog(const CrashReport& report, QWidget* parent)
    : QDialog(parent), m_report(report) {
    setWindowTitle(report.crashed ? tr("TraceView closed unexpectedly") : tr("Report a bug"));
    setMinimumWidth(300);
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    resize(560, sizeHint().height());
#endif

    auto* header = new DialogHeader(
        windowTitle(),
        report.crashed ? tr("This report helps find the problem. Nothing is sent without you.")
                       : tr("Describe the problem. Nothing is sent without you."),
        this);

    // Version / System / When.
    auto* infoRow = new QHBoxLayout;
    infoRow->setSpacing(DialogChrome::kSpacing);
    infoRow->addWidget(infoCard(tr("Version"), report.appVersion, this), 1);
    infoRow->addWidget(infoCard(tr("System"), report.osName, this), 2);
    infoRow->addWidget(
        infoCard(tr("When"),
                 report.when.isValid() ? report.when.toString(QStringLiteral("yyyy-MM-dd HH:mm"))
                                       : tr("unknown"),
                 this),
        1);

    QLabel* updateNote = nullptr;
    if (!report.updatedFrom.isEmpty()) {
        updateNote = DialogChrome::hintLabel(
            tr("This happened right after updating from v%1.").arg(report.updatedFrom), this);
    }

    auto* descriptionLabel = new QLabel(tr("What were you doing?"), this);
    m_description = new QPlainTextEdit(this);
    m_description->setPlaceholderText(tr("I connected the robot over BLE and opened a workspace"));
    m_description->setTabChangesFocus(true);
    m_description->setMinimumHeight(90);

    // Technical details: collapsed by default.
    auto* detailsToggle = new QToolButton(this);
    detailsToggle->setText(tr("Technical details"));
    detailsToggle->setCheckable(true);
    detailsToggle->setChecked(false);
    detailsToggle->setArrowType(Qt::RightArrow);
    detailsToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    detailsToggle->setAutoRaise(true);

    auto* details = new QPlainTextEdit(this);
    details->setReadOnly(true);
    details->setLineWrapMode(QPlainTextEdit::NoWrap);
    details->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    details->setMinimumHeight(140);
    QString detailsText;
    if (CrashHandler::canCaptureStack() && !report.stackTrace.isEmpty()) {
        detailsText +=
            tr("Stack trace") + QStringLiteral("\n") + report.stackTrace + QStringLiteral("\n\n");
    }
    detailsText += tr("Log (last %1 lines)").arg(report.logTail.size()) + QStringLiteral("\n") +
                   report.logTail.join(QLatin1Char('\n'));
    details->setPlainText(detailsText);
    details->setVisible(false);
    connect(detailsToggle, &QToolButton::toggled, this, [detailsToggle, details](bool open) {
        detailsToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        details->setVisible(open);
    });

    // Footer.
    auto* copyButton = new QPushButton(tr("Copy report"), this);
    connect(copyButton, &QPushButton::clicked, this,
            [this] { QGuiApplication::clipboard()->setText(this->report().toText()); });
    auto* saveButton = new QPushButton(tr("Save…"), this);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Save report in folder"));
        if (dir.isEmpty()) {
            return;
        }
        QString error;
        const QString folder = this->report().saveToFolder(dir, &error);
        if (folder.isEmpty()) {
            DialogPresenter::warning(this, tr("Save report"),
                                     tr("The report could not be saved: %1").arg(error));
        } else {
            DialogPresenter::information(this, tr("Save report"),
                                         tr("Report saved in %1").arg(folder));
        }
    });

    auto* buttons = new QDialogButtonBox(this);
    buttons->addButton(tr("Don't send"), QDialogButtonBox::RejectRole);
    auto* openIssue = buttons->addButton(tr("Open issue"), QDialogButtonBox::AcceptRole);
    openIssue->setToolTip(tr("Copies the report and opens the issue page — paste it there."));
    DialogChrome::setButtonVariant(openIssue, "primary");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(openIssue, &QPushButton::clicked, this, [this] {
        const CrashReport current = this->report();
        QGuiApplication::clipboard()->setText(current.toText());
        QDesktopServices::openUrl(QUrl(current.issueUrl()));
        accept();
    });

    QVBoxLayout* layout = DialogChrome::setupRootLayout(this);
    layout->addWidget(header);
    layout->addLayout(infoRow);
    if (updateNote) {
        layout->addWidget(updateNote);
    }
    layout->addWidget(descriptionLabel);
    layout->addWidget(m_description, 1);
    layout->addWidget(detailsToggle);
    layout->addWidget(details, 1);
    layout->addLayout(DialogChrome::footer(buttons, {copyButton, saveButton}));
}

CrashReport CrashReportDialog::report() const {
    CrashReport result = m_report;
    result.userDescription = m_description ? m_description->toPlainText().trimmed() : QString();
    return result;
}

}  // namespace traceview
