#pragma once

#include <QDialog>

namespace traceview {

// Pix QR code + international donation note, shown from the top-level
// "Donate" action. Mirrors AboutDialog's shape (see aboutdialog.h).
// Needs Q_OBJECT so tr() resolves its own class as translation context --
// see the same note on AboutDialog.
class DonateDialog : public QDialog {
    Q_OBJECT

public:
    explicit DonateDialog(QWidget* parent = nullptr);
};

}  // namespace traceview
