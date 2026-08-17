#pragma once

#include <QDialog>

namespace traceview {

// Pix QR code + international donation note, shown from the top-level
// "Donate" action. Mirrors AboutDialog's shape (see aboutdialog.h).
class DonateDialog : public QDialog {
public:
    explicit DonateDialog(QWidget* parent = nullptr);
};

} // namespace traceview
