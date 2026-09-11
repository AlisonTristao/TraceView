#pragma once

#include <QDialog>

#include "updater/updateinfo.h"

namespace traceview {

// Shown when UpdateChecker finds a newer release -- either from the silent
// startup check or the Settings ▸ Updates "Check now" button. Purely a
// confirm/skip prompt: nothing is downloaded until the user clicks
// "Update Now" (see MainWindow::startUpdateDownload).
class UpdateAvailableDialog : public QDialog {
    Q_OBJECT

public:
    UpdateAvailableDialog(const UpdateInfo& info, QWidget* parent = nullptr);

signals:
    void updateRequested();
    void skipRequested();
};

}  // namespace traceview
