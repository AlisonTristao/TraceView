#pragma once

#include <QGraphicsView>

namespace traceview {

// The canvas widget: rubber-band selection by default (drag on empty space
// selects blocks, matching DashboardGrid's own selection convention) and
// space+drag / middle-button panning. No zoom -- blocks are fixed-size and
// the scene is small enough that 1:1 is always the right scale for this
// first pass.
class DiagramView : public QGraphicsView {
    Q_OBJECT

public:
    explicit DiagramView(QGraphicsScene* scene, QWidget* parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    bool m_spacePanning = false;
};

}  // namespace traceview
