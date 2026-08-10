#pragma once

#include <QtMath>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgets/chartdata.h"
#include "protocol/telemetryfieldrouter.h"

namespace traceview {

// DummyLineChartWidget/DummyBarChartWidget/DummyGaugeWidget render live
// telemetry per their ConfigEditor config (widgets/chartconfigeditor.h,
// widgets/gaugeconfigeditor.h): setConfig() stores the parsed config, and
// appendFieldSample()/setValue() below feed each decoded (timestamp, value)
// pair in as it arrives, coalescing repaints independently of ingestion rate
// (topico 14 PASSO 12) so a fast telemetry stream doesn't force a full
// repaint per sample. Retain the "Dummy" class/type-id names (see
// widgetregistry.cpp) even though rendering is now real -- renaming is a
// save-format/UI-label concern independent of this.
//
// Wiring these to a live TelemetryFieldRouter::fieldSample() signal (i.e.
// which widget receives which field, filtered by this widget's own
// sourceId/topicId/fieldId config) is topico 15's job ("fatia vertical de
// telemetria binaria"); this class only owns the data model and paint logic
// and stays paintable/testable with directly-injected synthetic samples
// either way (see tools/chart_preview).

class ChartWidgetBase : public DashboardWidget {
public:
    using DashboardWidget::DashboardWidget;

    QColor cellFillColor(const ThemePalette& palette) const override { return palette.surface; }
    void setConfig(const QJsonObject& config) override;

    // Appends one decoded (timestampUs, value) pair to every series bound to
    // `fieldId` (ChartSeriesConfig::fieldId) and schedules a repaint.
    void appendFieldSample(quint16 fieldId, quint64 timestampUs, double value);

    // Connects directly to TelemetryFieldRouter::fieldSample() (topico 15):
    // a no-op unless `binding` matches this widget's own configured
    // sourceId/topicId, in which case it forwards to appendFieldSample()
    // above. Not a Q_SLOT (ChartWidgetBase has no Q_OBJECT of its own) but
    // still connectable via the functor-based QObject::connect overload.
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs, double value);

protected:
    ChartConfig m_config;
    QVector<TelemetrySeriesBuffer> m_seriesBuffers;  // one per m_config.series, same order

private:
    void scheduleRepaint();

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

    // Sets the current value if `fieldId` matches this gauge's configured
    // GaugeConfig::fieldId (a no-op otherwise) and schedules a repaint.
    void appendFieldSample(quint16 fieldId, quint64 timestampUs, double value);

    // See ChartWidgetBase::onFieldSample() above -- same role, filtered by
    // GaugeConfig's sourceId/topicId instead of ChartConfig's.
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs, double value);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void scheduleRepaint();

    GaugeConfig m_config;
    double m_value = qQNaN();
    bool m_repaintPending = false;
};

}  // namespace traceview
