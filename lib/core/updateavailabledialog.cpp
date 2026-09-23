#include "updateavailabledialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "traceview/version.h"

namespace traceview {

UpdateAvailableDialog::UpdateAvailableDialog(const UpdateInfo& info, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Update Available"));

    auto* titleLabel = new QLabel(tr("<b>Update available</b>"), this);
    titleLabel->setTextFormat(Qt::RichText);

    auto* versionsLabel = new QLabel(
        tr("New version: v%1<br>Your version: v%2").arg(info.version, kVersion), this);
    versionsLabel->setTextFormat(Qt::RichText);
    versionsLabel->setWordWrap(true);

    // A plain row, not a QDialogButtonBox: DialogPresenter stacks a button
    // box vertically once it's wider than a phone card, and these two
    // should stay side by side, sharing the width.
    auto* laterButton = new QPushButton(tr("Later"), this);
    auto* updateButton = new QPushButton(tr("Update Now"), this);
    updateButton->setDefault(true);
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(laterButton, 1);
    buttonRow->addWidget(updateButton, 1);

    connect(updateButton, &QPushButton::clicked, this, [this] {
        emit updateRequested();
        accept();
    });
    connect(laterButton, &QPushButton::clicked, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(versionsLabel);
    layout->addSpacing(8);
    layout->addLayout(buttonRow);
}

}  // namespace traceview
