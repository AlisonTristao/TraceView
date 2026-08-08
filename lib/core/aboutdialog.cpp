#include "aboutdialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QtGlobal>

#include "traceview/version.h"

namespace traceview {

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("About TraceView");
    setMinimumWidth(360);

    auto* titleLabel = new QLabel(QString("<b>TraceView</b> v%1").arg(kVersion), this);
    auto* descriptionLabel = new QLabel(kDescription, this);
    descriptionLabel->setWordWrap(true);

    auto* qtLabel = new QLabel(
        QString("Built with Qt %1 &middot; running with Qt %2").arg(QT_VERSION_STR, qVersion()), this);
    qtLabel->setTextFormat(Qt::RichText);

    auto* licenseLabel = new QLabel("MIT License &middot; AlisonTristao", this);
    licenseLabel->setTextFormat(Qt::RichText);

    auto* changelogLabel = new QLabel("See CHANGELOG.md for release history.", this);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addSpacing(8);
    layout->addWidget(qtLabel);
    layout->addWidget(licenseLabel);
    layout->addWidget(changelogLabel);
    layout->addSpacing(8);
    layout->addWidget(buttons);
}

} // namespace traceview
