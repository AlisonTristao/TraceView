#pragma once

#include <QToolButton>

namespace traceview {

class NotificationLog;

// The bell at the right of the status row (or next to the options button in
// compact chrome) that opens the notification panel. It shows a Lucide bell
// and, while there are unread notifications, a small count badge in the
// colour of the worst of them (error red, warning amber, otherwise accent).
// Checkable: checked while the panel is open, shown by an accent glyph
// (buttons have no checked fill, see stylesheet.cpp).
class NotificationBell : public QToolButton {
    Q_OBJECT

public:
    NotificationBell(NotificationLog* log, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void refresh();

    NotificationLog* m_log;
};

}  // namespace traceview
