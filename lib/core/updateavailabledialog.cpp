#include "updateavailabledialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/dialogchrome.h"
#include "traceview/version.h"

namespace traceview {

UpdateAvailableDialog::UpdateAvailableDialog(const UpdateInfo& info, bool downloaded,
                                             QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Update Available"));

    auto* header = new DialogHeader(
        tr("Update Available"),
        downloaded ? tr("A new update has been downloaded and is ready to install.")
                   : tr("A new update is available. Do you want to update?"),
        this);

    auto* versionsLabel = new QLabel(
        tr("New version: v%1<br>Your version: v%2").arg(info.version, kVersion), this);
    versionsLabel->setTextFormat(Qt::RichText);
    versionsLabel->setWordWrap(true);

    // A plain row, not a QDialogButtonBox: DialogPresenter stacks a button
    // box vertically once it's wider than a phone card, and these two
    // should stay side by side. Laid out like DialogChrome::footer().
    auto* laterButton = new QPushButton(tr("Later"), this);
    auto* updateButton = new QPushButton(downloaded ? tr("Install Now") : tr("Update Now"), this);
    updateButton->setDefault(true);
    DialogChrome::setButtonVariant(updateButton, "primary");
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(laterButton);
    buttonRow->addWidget(updateButton);

    connect(updateButton, &QPushButton::clicked, this, [this] {
        emit updateRequested();
        accept();
    });
    connect(laterButton, &QPushButton::clicked, this, &QDialog::reject);

    QVBoxLayout* layout = DialogChrome::setupRootLayout(this);
    layout->addWidget(header);
    layout->addWidget(versionsLabel);
    layout->addLayout(buttonRow);
}

}  // namespace traceview
