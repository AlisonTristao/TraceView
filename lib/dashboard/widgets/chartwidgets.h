#pragma once

#include <QtMath>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgets/chartdata.h"

namespace traceview {

// DummyLineChartWidget/DummyBarChartWidget/DummyGaugeWidget render live
// telemetry per their ConfigEditor config (widgets/chartconfigeditor.h,
// widgets/gaugeconfigeditor.h): setConfig() stores the parsed config,
// onSerialPayload() decodes each arriving frame's payload, and paintEvent()
// draws whatever's currently held. Retain the "Dummy" class/type-id names
// (see widgetregistry.cpp) even though rendering is now real -- renaming is
// a save-format/UI-label concern independent of this task (BACKEND_TODO.txt
// Tasks 7/8).

class ChartWidgetBase : public DashboardWidget {
public:
    using DashboardWidget::DashboardWidget;

    QColor cellFillColor(const ThemePalette& palette) const override { return palette.surface; }
    void setConfig(const QJsonObject& config) override;
    void onSerialPayload(const QByteArray& payload) override;

protected:
    ChartConfig m_config;
    QVector<QVector<double>> m_seriesBuffers; // one per m_config.series, same order

private:
    bool m_repaintPending = false;
};

class DummyLineChartWidget : public ChartWidgetBase {
public:
    explicit DummyLineChartWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class DummyBarChartWidget : public ChartWidgetBase {
public:
    explicit DummyBarChartWidget(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};

class DummyGaugeWidget : public DashboardWidget {
public:
    explicit DummyGaugeWidget(QWidget* parent = nullptr);

    QColor cellFillColor(const ThemePalette& palette) const override { return palette.surface; }
    void setConfig(const QJsonObject& config) override;
    void onSerialPayload(const QByteArray& payload) override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    GaugeConfig m_config;
    double m_value = qQNaN();
    bool m_repaintPending = false;
};

} // namespace traceview
