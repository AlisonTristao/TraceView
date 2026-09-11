#include "updateavailabledialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "traceview/version.h"

namespace traceview {

UpdateAvailableDialog::UpdateAvailableDialog(const UpdateInfo& info, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Update Available"));
    setMinimumWidth(420);

    auto* titleLabel = new QLabel(
        tr("<b>TraceView %1</b> is available (you have v%2).").arg(info.version, kVersion), this);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setWordWrap(true);

    auto* notes = new QPlainTextEdit(this);
    notes->setReadOnly(true);
    notes->setPlainText(info.releaseNotes.isEmpty() ? tr("No release notes provided.")
                                                     : info.releaseNotes);
    notes->setMaximumHeight(160);

    auto* buttons = new QDialogButtonBox(this);
    auto* updateButton = buttons->addButton(tr("Update Now"), QDialogButtonBox::AcceptRole);
    auto* skipButton =
        buttons->addButton(tr("Skip This Version"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(tr("Remind Me Later"), QDialogButtonBox::RejectRole);

    connect(updateButton, &QPushButton::clicked, this, [this] {
        emit updateRequested();
        accept();
    });
    connect(skipButton, &QPushButton::clicked, this, [this] {
        emit skipRequested();
        reject();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(notes);
    layout->addWidget(buttons);
}

}  // namespace traceview
