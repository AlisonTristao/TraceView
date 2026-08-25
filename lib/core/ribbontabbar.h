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

    // Marks a tab as carrying the small "x" drawn in its trapezoid's top
    // right corner (see paintEvent); clicking it emits tabCloseRequested
    // instead of selecting the tab. Off by default -- the fixed Run/Layout/
    // Devices tabs never call this.
    void setTabClosable(int index, bool closable);

signals:
    void tabCloseRequested(int index);

protected:
    QSize tabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Empty (isNull()) for a non-closable tab, or one not carrying the flag
    // set via setTabClosable(). Anchored off tabRect(index)'s top edge --
    // the trapezoid's right side only slants further outward moving down
    // from there, so a rect sized to fit at the top edge is guaranteed to
    // stay inside the shape for its whole height.
    QRect closeButtonRect(int index) const;

    int m_hoverIndex = -1;
    int m_hoverCloseIndex = -1;
};

}  // namespace traceview
