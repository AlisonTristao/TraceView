#pragma once

#include <QRect>
#include <QtMath>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgets/chartdata.h"
#include "telemetry/telemetrybinding.h"

class QMouseEvent;

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
    explicit ChartWidgetBase(QWidget* parent = nullptr) : DashboardWidget(parent) {
        // Needed for mouseMoveEvent() to fire on plain cursor movement (no
        // button held) -- that's how the hover crosshair below tracks the
        // mouse across the plot.
        setMouseTracking(true);
    }

    QColor cellFillColor(const ThemePalette& palette) const override { return palette.surface; }
    void setConfig(const QJsonObject& config) override;

    bool wantsHeaderControls() const override { return true; }
    bool isPaused() const override { return m_paused; }
    void setPaused(bool paused) override;
    void clearChartData() override;
    bool showsLastValueRow() const override { return m_showLastValueRow; }
    void setShowsLastValueRow(bool show) override;
    bool showsGridPointMarkers() const override { return m_showGridPointMarkers; }
    void setShowsGridPointMarkers(bool show) override;
    bool showsHoverCrosshair() const override { return m_showHoverCrosshair; }
    void setShowsHoverCrosshair(bool show) override;
    QString lineInterpolation() const override { return chartLineInterpolationId(m_lineInterpolation); }
    void setLineInterpolation(const QString& id) override;

    // Appends one decoded (timestampUs, value) pair to every series bound to
    // `fieldId` (ChartSeriesConfig::fieldId) and schedules a repaint. A
    // no-op while isPaused() -- the header's pause button drops incoming
    // samples instead of buffering them, so the plot visibly freezes until
    // resumed.
    void appendFieldSample(quint16 fieldId, quint64 timestampUs, double value);

    // Connects directly to TelemetryFieldRouter::fieldSample() (topico 15):
    // a no-op unless `binding` matches this widget's own configured
    // sourceId/topicId, in which case it forwards to appendFieldSample()
    // above. Not a Q_SLOT (ChartWidgetBase has no Q_OBJECT of its own) but
    // still connectable via the functor-based QObject::connect overload.
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs, double value);

    // The parsed config this widget is currently rendering. Read by
    // MainWindow to derive the wire-level SUBSCRIBE this widget implies
    // (topico 17): which (sourceId, topicId) it consumes and how fast it
    // wants samples.
    const ChartConfig& config() const { return m_config; }

    // Caps how often appendFieldSample() triggers an actual repaint via
    // scheduleRepaint() (chartwidgets.cpp), independent of how fast samples
    // arrive (topico 14 PASSO 12: ingestion rate stays separate from repaint
    // rate) -- data still gets appended to the buffers on every sample
    // regardless. 33ms (~30Hz) by default; 0 removes the cap entirely
    // (repaint on every appendFieldSample() call). Exposed only for
    // DebugChartsWindow's stress-mode toggle (mainwindow.cpp's Debug menu)
    // to exercise the paint code at its real, unthrottled ceiling -- nothing
    // in production code calls this.
    void setRepaintIntervalMs(int ms) { m_repaintIntervalMs = ms; }

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

    ChartConfig m_config;
    QVector<TelemetrySeriesBuffer> m_seriesBuffers;  // one per m_config.series, same order
    // Per-series "hidden via legend click" flag, same index as m_config.series
    // -- toggled by mousePressEvent() below when a click lands inside
    // m_legendHitRects, kept sized/aligned to m_config.series by setConfig()
    // (new series default to visible). Read by DummyLineChartWidget::
    // paintEvent()/DummyBarChartWidget::paintEvent() to skip a hidden series'
    // line/bar and to gray out its legend row.
    QVector<bool> m_seriesHidden;
    // Clickable rect per series from the legend actually painted last frame
    // (paintSeriesLegends()'s outHitRects param, same index as m_seriesHidden)
    // -- mousePressEvent() hit-tests against these instead of recomputing the
    // legend layout itself, so the click target can never drift from what's
    // actually drawn.
    QVector<QRect> m_legendHitRects;
    // Read directly by DummyLineChartWidget::paintEvent()/DummyBarChartWidget::
    // paintEvent() to pass through to paintSeriesLegends().
    bool m_showLastValueRow = true;
    // Read directly by DummyLineChartWidget::paintEvent() to gate
    // paintGridPointMarkers() -- bar charts have no line to mark a crossing
    // on, so DummyBarChartWidget never reads this.
    bool m_showGridPointMarkers = false;
    // Read directly by DummyLineChartWidget::paintEvent() to gate
    // paintHoverCrosshair() -- same bar-chart exclusion as m_showGridPointMarkers.
    bool m_showHoverCrosshair = false;
    // Read directly by DummyLineChartWidget::paintEvent() and passed through
    // to paintLineSeries()/paintGridPointMarkers()/paintHoverCrosshair() --
    // same bar-chart exclusion as m_showGridPointMarkers (bar chart inherits
    // the gear-menu select box but its own paintEvent never reads this).
    ChartLineInterpolation m_lineInterpolation = ChartLineInterpolation::Linear;
    // Current mouse position in this widget's own coordinates, updated by
    // mouseMoveEvent()/leaveEvent() below and read by DummyLineChartWidget::
    // paintEvent() to place the hover crosshair. m_hasHoverPos is false
    // whenever the mouse isn't over this widget at all (leaveEvent(), or
    // simply never having entered it yet) -- distinct from m_hoverPos sitting
    // outside plotRect, which paintHoverCrosshair() itself handles.
    QPoint m_hoverPos;
    bool m_hasHoverPos = false;

private:
    void scheduleRepaint();

    bool m_repaintPending = false;
    bool m_paused = false;
    int m_repaintIntervalMs = 33;  // see setRepaintIntervalMs() above
};

class DummyLineChartWidget : public ChartWidgetBase {
public:
    explicit DummyLineChartWidget(QWidget* parent = nullptr);

    bool hasChartOptionsMenu() const override { return true; }

protected:
    void paintEvent(QPaintEvent* event) override;
};

// Fixed-bar snapshot chart: one bar per configured series (not per sample --
// see paintBarSnapshot() in chartwidgets.cpp), always redrawn at the same X
// position and showing only that series' latest buffered value, with the
// value itself printed on the X axis directly under its bar. Unlike
// DummyLineChartWidget, there is no history to scroll through and no
// gear-menu options (hasChartOptionsMenu() stays at DashboardWidget's
// default false) -- pause/clear from the header still apply.
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

    // Same header cluster as the line/bar charts (ChartWidgetBase) -- a
    // connection-state dot, pause/resume, clear, and a settings gear. The
    // gear stays inert (hasChartOptionsMenu() defaults to false, same as
    // DummyBarChartWidget): a gauge has no per-series line/grid/hover
    // options for it to open.
    bool wantsHeaderControls() const override { return true; }
    bool isPaused() const override { return m_paused; }
    void setPaused(bool paused) override { m_paused = paused; }
    // Resets every ring to "no value yet" ("--") -- a gauge has no history
    // to clear, so this is what the header's clear button does here instead.
    void clearChartData() override;

    // Updates the current value of every ring bound to `fieldId`
    // (GaugeSeriesConfig::fieldId -- usually zero or one ring, but nothing
    // stops two rings from tracking the same field) and schedules a repaint.
    // A no-op while paused (samples are dropped, not buffered -- a gauge
    // keeps no history to catch up on once resumed) or if no ring binds to
    // `fieldId`.
    void appendFieldSample(quint16 fieldId, quint64 timestampUs, double value);

    // See ChartWidgetBase::onFieldSample() above -- same role, filtered by
    // GaugeConfig's sourceId/topicId instead of ChartConfig's.
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs, double value);

    // See ChartWidgetBase::config() -- same role for a gauge's own config.
    const GaugeConfig& config() const { return m_config; }

    // See ChartWidgetBase::setRepaintIntervalMs() above -- same role, this
    // class keeps its own throttle state instead of sharing ChartWidgetBase's
    // since it doesn't derive from it.
    void setRepaintIntervalMs(int ms) { m_repaintIntervalMs = ms; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void scheduleRepaint();

    GaugeConfig m_config;
    QVector<double> m_values;  // one per m_config.series, same order -- kept
                               // in sync as m_config.series changes by
                               // setConfig() in the .cpp.
    bool m_repaintPending = false;
    bool m_paused = false;
    int m_repaintIntervalMs = 33;  // see setRepaintIntervalMs() above
};

}  // namespace traceview
