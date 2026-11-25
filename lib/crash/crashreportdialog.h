#pragma once

#include <QDialog>

#include "crash/crashreport.h"

class QPlainTextEdit;

namespace traceview {

// "TraceView closed unexpectedly" (after a crash) or "Report a bug" (from the
// Help menu). Built on DialogChrome like every other dialog. Nothing is sent
// anywhere: the user copies, saves or opens an issue page themselves.
class CrashReportDialog : public QDialog {
    Q_OBJECT
public:
    explicit CrashReportDialog(const CrashReport& report, QWidget* parent = nullptr);

    // The report with the user's description filled in.
    CrashReport report() const;

private:
    CrashReport m_report;
    QPlainTextEdit* m_description = nullptr;
};

}  // namespace traceview
