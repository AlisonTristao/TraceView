#pragma once

#include <QVector>
#include <QWidget>

#include "protocol/logentry.h"

class QTableWidget;

namespace traceview {

class BusySpinner;

// The Logs tab's content -- opens a bally_OS ".blog" file (see
// protocol/logfilereader.h) and lists every decoded LOG entry in a table,
// one row per message, in file order. A read-only file inspector, not a live
// view: unlike DevicesGrid/DashboardGrid it owns no undo stack and nothing
// here is persisted into the .tvproj.
class LogViewer : public QWidget {
    Q_OBJECT

public:
    explicit LogViewer(QWidget* parent = nullptr);

    // Parses `filePath` with LogFileReader on a background thread (it has
    // no Qt/UI dependency of its own, see the class comment on
    // LogFileReader) and replaces the table's contents with its entries
    // once that finishes, showing a BusySpinner in the corner for as long
    // as it runs so a large .blog never reads as a frozen window. Shows a
    // QMessageBox::warning (same convention as MainWindow::onOpenProject)
    // instead of populating the table if the file could not be opened.
    void openFile(const QString& filePath);

private:
    void populateTable(const QVector<LogEntry>& entries);

    QTableWidget* m_table;
    BusySpinner* m_spinner;
};

}  // namespace traceview
