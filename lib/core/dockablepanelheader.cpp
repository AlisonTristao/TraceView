#include "dockablepanelheader.h"

#include <QApplication>
#include <QMouseEvent>

#include "ribbon.h"

namespace traceview {

DockablePanelHeader::DockablePanelHeader(QWidget* parent) : QWidget(parent) {
    setObjectName("dockablePanelHeader");
    // Qt only auto-paints a QSS `background-color` for plain QWidget
    // instances, not subclasses -- see DockablePanel's constructor for the
    // same fix/rationale. Without it the "QWidget#dockablePanelHeader" rule
    // in stylesheet.cpp (the bar/border that marks this out as a drag
    // handle) wouldn't render.
    setAttribute(Qt::WA_StyledBackground, true);
    // Reinforces that this bar -- not the rest of the panel -- is what you
    // grab to move/dock it, the same way DockResizeGrip's cursor signals a
    // resize handle.
    setCursor(Qt::SizeAllCursor);
    // No child widgets left in here (the pin toggle this used to host is
    // gone -- panel visibility is MainWindow's show/hide toggle now, see
    // MainWindow::onTogglePanelsClicked()), so nothing else gives this bar a
    // height; pin it to the same size ribbon buttons use so it stays a
    // visible, easily-grabbed strip.
    setFixedHeight(kRibbonButtonSize);
}

void DockablePanelHeader::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_pressPos = event->globalPosition().toPoint();
    }
    QWidget::mousePressEvent(event);
}

void DockablePanelHeader::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint globalPos = event->globalPosition().toPoint();
    if (!m_dragging) {
        if ((globalPos - m_pressPos).manhattanLength() < QApplication::startDragDistance()) {
            QWidget::mouseMoveEvent(event);
            return;
        }
        m_dragging = true;
        emit dragStarted(globalPos);
        // Re-asserts mouse capture against whatever top-level window this
        // header now belongs to -- dragStarted() may have just reparented
        // the panel (dock -> floating), which swaps the native window
        // underneath the OS-level mouse grab; without grabbing again here,
        // Windows can stop delivering move/release events mid-drag.
        grabMouse();
        return;
    }
    emit dragMoved(globalPos);
}

void DockablePanelHeader::mouseReleaseEvent(QMouseEvent* event) {
    if (m_dragging && event->button() == Qt::LeftButton) {
        releaseMouse();
        m_dragging = false;
        emit dragFinished(event->globalPosition().toPoint());
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

}  // namespace traceview
