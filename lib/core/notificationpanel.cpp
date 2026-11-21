#include "core/notificationpanel.h"

#include <QApplication>
#include <QMouseEvent>
#include <QVBoxLayout>

#include "core/notificationlistview.h"

namespace traceview {

namespace {
// Same as the properties panel: wide enough for a two-line message.
constexpr int kNotificationPanelWidth = 320;
}  // namespace

NotificationPanel::NotificationPanel(NotificationLog* log, QWidget* parent) : DockablePanel(parent) {
    setObjectName(QStringLiteral("notificationPanel"));
    // 1px: leaves the panel's outline (stylesheet.cpp) uncovered.
    mainLayout()->setContentsMargins(1, 1, 1, 1);
    m_view = new NotificationListView(log, /*showPin=*/true, this);
    bodyLayout()->addWidget(m_view);
}

int NotificationPanel::preferredThickness() const {
    return kNotificationPanelWidth;
}

void NotificationPanel::setToggleButton(QWidget* button) {
    m_toggleButton = button;
}

void NotificationPanel::setPinned(bool pinned) {
    m_pinned = pinned;
    m_view->setPinned(pinned);
}

void NotificationPanel::showEvent(QShowEvent* event) {
    DockablePanel::showEvent(event);
    qApp->installEventFilter(this);
}

void NotificationPanel::hideEvent(QHideEvent* event) {
    DockablePanel::hideEvent(event);
    qApp->removeEventFilter(this);
}

bool NotificationPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress && !m_pinned) {
        // Widgets only: the same press reaches the QWindow first.
        auto* target = qobject_cast<QWidget*>(watched);
        if (target == nullptr) return DockablePanel::eventFilter(watched, event);
        // Menus/popups opened from the panel (its options menu, the filter's
        // list) are separate windows: they belong to it. So do the dock's
        // resize grips, which sit beside the panel.
        const bool inside =
            target == this || isAncestorOf(target) ||
            (m_toggleButton && (target == m_toggleButton || m_toggleButton->isAncestorOf(target))) ||
            target->window()->windowType() == Qt::Popup ||
            target->objectName() == QLatin1String("dockResizeGrip");
        if (!inside) emit dismissRequested();
    }
    return DockablePanel::eventFilter(watched, event);
}

}  // namespace traceview
