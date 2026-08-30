#pragma once

#include <QTabBar>

namespace traceview {

// The tab strip above SerialMonitorWidget's per-device terminals: fixed-height
// trapezoids (narrow top, wide base) drawn as file folders -- the same
// "/device 1\/device 2\" look as core/ribbontabbar.h's RibbonTabBar, but
// sized to each label instead of a fixed width, and with an optional
// connection dot per tab. Painted entirely via QPainter (colors straight from
// ThemeManager), so the shared stylesheet's QTabBar::tab rules don't apply.
//
// RibbonTabBar itself lives in traceview_ui and can't be reached from this
// (traceview_dashboard) layer, hence the small reimplementation here rather
// than a shared base.
class TerminalTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit TerminalTabBar(QWidget* parent = nullptr);

    // Draws a green/red dot before the tab's label. Stored per index via
    // setTabData(); SerialMonitorWidget re-applies it after every tab rebuild.
    void setTabConnected(int index, bool connected);

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int m_hoverIndex = -1;
};

}  // namespace traceview
