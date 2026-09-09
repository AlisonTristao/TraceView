#include "diagramview.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace traceview {

DiagramView::DiagramView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent) {
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    // DiagramScene's sceneRect gives blocks room to spread out; without this,
    // QGraphicsView's initial scroll position is arbitrary within it and can
    // leave (0,0) -- where DiagramPage places the first block -- scrolled
    // off-screen.
    centerOn(0, 0);
}

void DiagramView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        // Re-post as a left-button press: QGraphicsView's own ScrollHandDrag
        // implementation only pans on the left button.
        QMouseEvent fake(QEvent::MouseButtonPress, event->position(), event->globalPosition(),
                         Qt::LeftButton, Qt::LeftButton, event->modifiers());
        QGraphicsView::mousePressEvent(&fake);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void DiagramView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        QMouseEvent fake(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                         Qt::LeftButton, Qt::LeftButton, event->modifiers());
        QGraphicsView::mouseReleaseEvent(&fake);
        if (!m_spacePanning) {
            setDragMode(QGraphicsView::RubberBandDrag);
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void DiagramView::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePanning = true;
        setDragMode(QGraphicsView::ScrollHandDrag);
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void DiagramView::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePanning = false;
        setDragMode(QGraphicsView::RubberBandDrag);
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

}  // namespace traceview
