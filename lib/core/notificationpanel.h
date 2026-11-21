#pragma once

#include <QPointer>

#include "core/dockablepanel.h"

namespace traceview {

class NotificationListView;
class NotificationLog;

// The bell's panel on desktop: a NotificationListView in a DockablePanel, so
// it docks to an edge or floats like the layers/properties panels (dragged
// by its header, sized by its grip, remembered by PanelDockController). Docked
// to the right by default.
//
// Unpinned it behaves like a popover: a click anywhere outside it (and
// outside the bell that opened it) asks to close it. Pinned it stays open;
// MainWindow then either leaves it over the dashboard or shrinks the
// dashboard beside it (AppSettings::notificationPinStyle).
class NotificationPanel : public DockablePanel {
    Q_OBJECT

public:
    NotificationPanel(NotificationLog* log, QWidget* parent = nullptr);

    int preferredThickness() const override;
    NotificationListView* listView() const {
        return m_view;
    }
    // Clicks on this widget do not count as "outside" (the bell: its own
    // click toggles the panel, it must not close-then-reopen it).
    void setToggleButton(QWidget* button);
    void setPinned(bool pinned);
    bool isPinned() const {
        return m_pinned;
    }

signals:
    void dismissRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    NotificationListView* m_view = nullptr;
    QPointer<QWidget> m_toggleButton;
    bool m_pinned = false;
};

}  // namespace traceview
