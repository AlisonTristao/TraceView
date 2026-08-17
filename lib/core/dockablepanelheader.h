#pragma once

#include <QPoint>
#include <QWidget>

class QMouseEvent;
class QToolButton;

namespace traceview {

// The draggable strip at the top of a DockablePanel: a stretch plus the pin
// toggle button, exactly what used to be built inline as an anonymous
// QHBoxLayout in LayersPanel/PropertiesPanel. Doubles as the panel's drag
// handle -- a press-and-move anywhere on this bar (other than the pin button,
// which consumes its own press as an opaque child) starts a drag once the
// pointer clears QApplication::startDragDistance(), reported purely as raw
// global-position signals. This widget knows nothing about docking/floating;
// PanelDockController owns those decisions.
class DockablePanelHeader : public QWidget {
    Q_OBJECT

public:
    explicit DockablePanelHeader(QWidget* parent = nullptr);

    QToolButton* pinButton() const { return m_pinButton; }

signals:
    // Fired once, the first time a press-then-move crosses the drag
    // threshold. globalPos is the cursor position at that moment (not the
    // original press position), so the caller can derive its initial grab
    // offset directly from it.
    void dragStarted(QPoint globalPos);
    // Fired on every subsequent mouse move while dragging.
    void dragMoved(QPoint globalPos);
    // Fired on mouse release, only if a drag was in progress.
    void dragFinished(QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    QToolButton* m_pinButton = nullptr;
    QPoint m_pressPos;
    bool m_dragging = false;
};

} // namespace traceview
