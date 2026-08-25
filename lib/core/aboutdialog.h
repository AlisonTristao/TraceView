#pragma once

#include <QDialog>

namespace traceview {

// Version/requirements info shown from the top-level "About" action.
// Needs Q_OBJECT so tr() resolves its own class as translation context --
// without it, tr() calls inside this class silently fall back to QDialog's
// context and never match the traceview::AboutDialog entries in the .ts
// files, regardless of the active language.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);
};

}  // namespace traceview
