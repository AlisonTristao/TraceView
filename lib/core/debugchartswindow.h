#pragma once

#include <QDialog>

class QTimer;

namespace traceview {

class DummyLineChartWidget;
class DummyBarChartWidget;
class DummyGaugeWidget;
class SerialMonitorWidget;

// Debug-only window (the menu bar's "Debug" action, mainwindow.cpp) that
// plots synthetic data through the exact same DashboardCell + chart-widget
// stack the real dashboard grid uses -- header controls, gear menu
// (pause/clear/last-value row/grid-point markers/hover crosshair), all of
// it -- so chart rendering/interaction can be eyeballed without a serial
// device or a saved project. One instance of every WidgetRegistry type
// (widgetregistry.cpp) is present -- the three dummy charts plus a serial
// monitor and the three control widgets (push button/toggle/slider) -- so a
// font change (FontManager, include/traceview/fontmanager.h) or theme change
// can be eyeballed across every widget kind at once instead of one at a
// time. Non-modal (shown via show(), not exec()) so its synthetic feed keeps
// ticking while the rest of the app stays usable. See tools/chart_preview
// for the older, bare (no DashboardCell chrome) version of this same idea.
class DebugChartsWindow : public QDialog {
    Q_OBJECT

public:
    explicit DebugChartsWindow(QWidget* parent = nullptr);

private:
    void tick();
    void updateFpsTitle();
    // Wired to the stress-mode toggle button (see constructor) -- swaps the
    // synthetic-data tick timer between its normal 50ms pace and firing as
    // fast as the event loop allows, so the throttle already built into
    // ChartWidgetBase::scheduleRepaint() (chartwidgets.cpp, ~30Hz/widget)
    // becomes the only thing still limiting the repaint rate. Purely visual
    // -- lets you eyeball the charts genuinely maxed out, even though the
    // update rate itself is too fast to follow by eye once enabled.
    void setStressMode(bool enabled);

    qint64 m_tick = 0;
    quint64 m_lastFrameCount = 0;
    QTimer* m_tickTimer = nullptr;
    DummyLineChartWidget* m_lineChart = nullptr;
    DummyBarChartWidget* m_barChart = nullptr;
    DummyGaugeWidget* m_gauge = nullptr;
    SerialMonitorWidget* m_serialMonitor = nullptr;
};

}  // namespace traceview
