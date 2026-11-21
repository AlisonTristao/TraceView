#pragma once

#include <QWidget>

#include "diagnostics/notificationentry.h"

class QComboBox;
class QLabel;
class QListWidget;
class QToolButton;

namespace traceview {

class NotificationLog;

// What the bell opens: the notifications of this run, newest first, as cards
// (severity stripe, source and time, text, an X to dismiss one), under a
// toolbar with a severity filter, Pin, Clear, an options menu (pop-up mode,
// pinned panel style, full history) and Close.
//
// The same widget fills the docked/floating NotificationPanel on desktop and
// the Page dialog on a phone (where Pin is hidden: there is no dashboard to
// keep it beside). It never decides visibility itself -- it asks through its
// signals.
class NotificationListView : public QWidget {
    Q_OBJECT

public:
    NotificationListView(NotificationLog* log, bool showPin, QWidget* parent = nullptr);

    void setPinned(bool pinned);

signals:
    void pinToggled(bool pinned);
    void closeRequested();
    void historyRequested();

private:
    void rebuild();
    void addRow(const NotificationEntry& entry);
    void applyFilter();
    void refreshIcons();
    void refreshEmptyState();

    NotificationLog* m_log;
    QComboBox* m_filter = nullptr;
    QToolButton* m_pinButton = nullptr;
    QToolButton* m_clearButton = nullptr;
    QToolButton* m_optionsButton = nullptr;
    QToolButton* m_closeButton = nullptr;
    QListWidget* m_list = nullptr;
    QLabel* m_empty = nullptr;
};

}  // namespace traceview
