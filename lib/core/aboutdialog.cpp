#include "aboutdialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QSysInfo>
#include <QVBoxLayout>
#include <QtGlobal>

#include "traceview/version.h"

namespace traceview {

namespace {

constexpr char kDeveloper[] = "AlisonTristao";
constexpr char kContactEmail[] = "AlisonTristao@hotmail.com";

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("About TraceView"));
    setMinimumWidth(320);

    auto* titleLabel = new QLabel(QStringLiteral("<b>TraceView</b>"), this);
    titleLabel->setTextFormat(Qt::RichText);

    const auto addRow = [this](QFormLayout* form, const QString& label, const QString& value) {
        auto* valueLabel = new QLabel(value, this);
        valueLabel->setWordWrap(true);
        valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(label, valueLabel);
        return valueLabel;
    };

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    addRow(form, tr("Version:"), QString::fromLatin1(kVersion));
    addRow(form, tr("Platform:"),
           QStringLiteral("%1 (%2)").arg(QSysInfo::prettyProductName(),
                                         QSysInfo::currentCpuArchitecture()));
    addRow(form, tr("Installed from:"), installSource());
    addRow(form, tr("Qt:"), QString::fromLatin1(qVersion()));
    addRow(form, tr("Developer:"), QString::fromLatin1(kDeveloper));
    auto* contactLabel =
        addRow(form, tr("Contact:"),
               QStringLiteral("<a href=\"mailto:%1\">%1</a>").arg(QLatin1String(kContactEmail)));
    contactLabel->setTextFormat(Qt::RichText);
    contactLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    contactLabel->setOpenExternalLinks(true);
    addRow(form, tr("License:"), QStringLiteral("MIT"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(titleLabel);
    layout->addSpacing(4);
    layout->addLayout(form);
    layout->addSpacing(8);
    layout->addWidget(buttons);
}

// Best guess from how this binary is running -- each official channel
// leaves its own trace (a compile-time define, APPIMAGE, the NSIS install
// directory); anything else is assumed to be a local build.
QString AboutDialog::installSource() {
#if defined(TRACEVIEW_FLATPAK_BUILD)
    return tr("Flatpak (alisontristao.github.io/TraceView)");
#elif defined(Q_OS_ANDROID)
    return tr("APK from GitHub Releases");
#elif defined(Q_OS_WIN)
    const QString programFiles = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles"));
    if (!programFiles.isEmpty() &&
        QCoreApplication::applicationDirPath().startsWith(programFiles, Qt::CaseInsensitive)) {
        return tr("Windows installer from GitHub Releases");
    }
    return tr("Local build");
#elif defined(Q_OS_LINUX)
    if (!qEnvironmentVariableIsEmpty("APPIMAGE")) {
        return tr("AppImage from GitHub Releases");
    }
    return tr("Local build");
#else
    return tr("Local build");
#endif
}

}  // namespace traceview
