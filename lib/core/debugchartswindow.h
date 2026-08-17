#pragma once

#include <QDialog>

namespace traceview {

class DummyLineChartWidget;
class DummyBarChartWidget;
class DummyGaugeWidget;

// Debug-only window (the menu bar's "Debug" action, mainwindow.cpp) that
// plots synthetic data through the exact same DashboardCell + chart-widget
// stack the real dashboard grid uses -- header controls, gear menu
// (pause/clear/last-value row/grid-point markers/hover crosshair), all of
// it -- so chart rendering/interaction can be eyeballed without a serial
// device or a saved project. Non-modal (shown via show(), not exec()) so its
// synthetic feed keeps ticking while the rest of the app stays usable. See
// tools/chart_preview for the older, bare (no DashboardCell chrome) version
// of this same idea.
class DebugChartsWindow : public QDialog {
    Q_OBJECT

public:
    explicit DebugChartsWindow(QWidget* parent = nullptr);

private:
    void tick();

    qint64 m_tick = 0;
    DummyLineChartWidget* m_lineChart = nullptr;
    DummyBarChartWidget* m_barChart = nullptr;
    DummyGaugeWidget* m_gauge = nullptr;
};

} // namespace traceview
