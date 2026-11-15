#pragma once

#include <QDialog>

#include "updater/updateinfo.h"

namespace traceview {

// Shown when UpdateChecker finds a newer release -- either from the silent
// startup check or the Settings ▸ Updates "Check now" button. Just the new
// and current version with Later / Update Now.
//
// `downloaded` is the usual case: MainWindow already fetched and verified
// the asset in the background (see MainWindow::onUpdateAvailable), so the
// dialog says it's ready and the button reads "Install Now" -- clicking it
// installs straight away instead of starting a wait. Without it (no
// auto-installable asset, or the background download failed) clicking
// "Update Now" starts the download (MainWindow::startUpdateDownload).
class UpdateAvailableDialog : public QDialog {
    Q_OBJECT

public:
    UpdateAvailableDialog(const UpdateInfo& info, bool downloaded, QWidget* parent = nullptr);

signals:
    void updateRequested();
};

}  // namespace traceview
