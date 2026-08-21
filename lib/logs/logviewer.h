#pragma once

#include <QWidget>

class QTableWidget;

namespace traceview {

// The Logs tab's content -- opens a bally_OS ".blog" file (see
// protocol/logfilereader.h) and lists every decoded LOG entry in a table,
// one row per message, in file order. A read-only file inspector, not a live
// view: unlike DevicesGrid/DashboardGrid it owns no undo stack and nothing
// here is persisted into the .tvproj.
class LogViewer : public QWidget {
    Q_OBJECT

public:
    explicit LogViewer(QWidget* parent = nullptr);

    // Parses `filePath` with LogFileReader and replaces the table's
    // contents with its entries. Shows a QMessageBox::warning (same
    // convention as MainWindow::onOpenProject) instead of populating the
    // table if the file could not be opened.
    void openFile(const QString& filePath);

private:
    QTableWidget* m_table;
};

}  // namespace traceview
