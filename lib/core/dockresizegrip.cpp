#include "dockresizegrip.h"

#include <QMouseEvent>

namespace traceview {

DockResizeGrip::DockResizeGrip(Orientation orientation, QWidget* parent) : QWidget(parent) {
    setObjectName("dockResizeGrip");
    setAttribute(Qt::WA_StyledBackground, true);
    // Needed for the QSS ":hover" rule (see stylesheet.cpp) to actually
    // repaint on enter/leave -- a plain QWidget doesn't request that on its
    // own the way built-in styled widgets (QPushButton, etc.) do.
    setAttribute(Qt::WA_Hover, true);
    setOrientation(orientation);
}

void DockResizeGrip::setOrientation(Orientation orientation) {
    switch (orientation) {
    case Orientation::Horizontal:
        setCursor(Qt::SizeHorCursor);
        break;
    case Orientation::Vertical:
        setCursor(Qt::SizeVerCursor);
        break;
    case Orientation::Corner:
        setCursor(Qt::SizeFDiagCursor);
        break;
    }
}

void DockResizeGrip::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    // Unlike DockablePanelHeader's move-drag, a press on a dedicated resize
    // handle is unambiguous -- no drag-distance threshold before it counts.
    m_dragging = true;
    grabMouse();
    emit dragStarted(event->globalPosition().toPoint());
}

void DockResizeGrip::mouseMoveEvent(QMouseEvent* event) {
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    emit dragMoved(event->globalPosition().toPoint());
}

void DockResizeGrip::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_dragging || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    releaseMouse();
    m_dragging = false;
    emit dragFinished(event->globalPosition().toPoint());
}

} // namespace traceview
