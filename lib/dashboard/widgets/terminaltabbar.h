#pragma once

#include <QTabBar>

namespace traceview {

// The tab strip above SerialMonitorWidget's per-device terminals: plain
// fixed-height rectangles butted together with 1px separators
// ("|terminal 1|terminal 2|"), the selected one left open at the bottom so it
// merges into the terminal below. Every tab is the same width (sized to the
// widest label) so the strip reads as an even row. Painted entirely via
// QPainter (colors straight from ThemeManager), so the shared stylesheet's
// QTabBar::tab rules don't apply.
class TerminalTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit TerminalTabBar(QWidget* parent = nullptr);

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Widest tab label, clamped to kMaxTextWidth -- every tab is sized to this.
    int uniformTextWidth() const;

    int m_hoverIndex = -1;
};

}  // namespace traceview
