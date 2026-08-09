#pragma once

#include "dashboard/dashboardwidget.h"

namespace traceview {

// Placeholder chart widgets used to exercise the dashboard grid (drag,
// resize, save/load) before real telemetry visualizations exist. Each just
// paints a themed, vaguely chart-shaped placeholder with fake data.
//
// DummyGaugeWidget's settings (which frame slot it reads, fixed min/max,
// unit, decimals) live in GaugeConfigEditor (widgets/gaugeconfigeditor.h),
// unlike the line/bar charts above whose settings live alongside them in
// ChartConfigEditor.

class DummyLineChartWidget : public DashboardWidget {
public:
    explicit DummyLineChartWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class DummyBarChartWidget : public DashboardWidget {
public:
    explicit DummyBarChartWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class DummyGaugeWidget : public DashboardWidget {
public:
    explicit DummyGaugeWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace traceview
