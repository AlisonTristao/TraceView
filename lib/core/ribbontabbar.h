#pragma once

#include <QTabBar>

namespace traceview {

// Fixed-width tabs painted as trapezoids (narrow top, wide base) — a
// file-folder look, distinct from QTabBar's default rectangular style.
// Paints itself entirely via QPainter (like ribbonicons.h) rather than
// through QStyle, so the shared stylesheet's QTabBar::tab rules don't apply
// here; colors are pulled straight from ThemeManager instead.
class RibbonTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit RibbonTabBar(QWidget* parent = nullptr);

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int m_hoverIndex = -1;
};

} // namespace traceview
