#pragma once

#include <QDialog>

#include "backend/statusseverity.h"

class QComboBox;
class QTableWidget;

namespace traceview {

class NotificationLog;
struct NotificationEntry;

// Non-modal window (File/View menu, and a click on the status bar's history
// button) showing every status-bar message posted this session -- the ones
// that otherwise scroll away after a few seconds. Read-only: it renders a
// NotificationLog it does not own, appends live as new messages arrive, and
// its Clear button clears the shared log. WA_DeleteOnClose + a QPointer on the
// MainWindow side, same lifetime pattern as DebugChartsWindow.
class NotificationHistoryWindow : public QDialog {
    Q_OBJECT

public:
    explicit NotificationHistoryWindow(NotificationLog* log, QWidget* parent = nullptr);

protected:
    void showEvent(QShowEvent* event) override;

private:
    // Hidden, the window only notes that it is behind (m_stale) and catches
    // up on the next show: once opened it lives on, and a robot left
    // reconnecting overnight posts thousands of entries nobody is looking at.
    void onEntryAdded(const NotificationEntry& entry, bool droppedOldest);
    void onEntryRemoved(quint64 id);
    // Drops the rows of entries the log no longer holds (its ring is bounded;
    // the table would otherwise keep every row ever added).
    void trimToLog();
    void rebuild();
    void appendRow(const NotificationEntry& entry);
    bool passesFilter(const NotificationEntry& entry) const;

    NotificationLog* m_log;
    QComboBox* m_severityFilter = nullptr;
    QTableWidget* m_table = nullptr;
    bool m_stale = false;
};

}  // namespace traceview
