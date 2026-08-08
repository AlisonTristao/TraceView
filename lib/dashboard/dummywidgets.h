#pragma once

#include "dashboardwidget.h"

namespace traceview {

// Placeholder chart widgets used to exercise the dashboard grid (drag,
// resize, save/load) before real telemetry visualizations exist. Each just
// paints a themed, vaguely chart-shaped placeholder with fake data.

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
