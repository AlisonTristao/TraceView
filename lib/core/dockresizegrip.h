#pragma once

#include <QPoint>
#include <QWidget>

namespace traceview {

// Thickness of an edge-docked panel's resize strip, and the size of a
// floating panel's corner resize square (see PanelDockController).
inline constexpr int kDockResizeGripThickness = 6;
inline constexpr int kDockResizeCornerGripSize = 14;

// A thin (or corner-square) handle that reports a raw mouse drag as absolute
// global positions, exactly like DockablePanelHeader's drag signals -- it
// knows nothing about docking. PanelDockController owns one per registered
// panel (an edge grip for its docked thickness, a corner grip for its
// floating size) and turns these signals into the actual resize.
class DockResizeGrip : public QWidget {
    Q_OBJECT

public:
    enum class Orientation { Horizontal, Vertical, Corner };

    DockResizeGrip(Orientation orientation, QWidget* parent);

    // Re-picks the resize cursor -- called whenever the panel this grip
    // belongs to redocks to a different pair of edges (left/right vs.
    // top/bottom), since the same grip instance is reused rather than
    // recreated.
    void setOrientation(Orientation orientation);

signals:
    void dragStarted(QPoint globalPos);
    void dragMoved(QPoint globalPos);
    void dragFinished(QPoint globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    bool m_dragging = false;
};

}  // namespace traceview
