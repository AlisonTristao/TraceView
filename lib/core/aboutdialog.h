#pragma once

#include <QDialog>

namespace traceview {

// Version/requirements info shown from the top-level "About" action.
class AboutDialog : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

} // namespace traceview
