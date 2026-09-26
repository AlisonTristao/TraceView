#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QRect>
#include <QtMath>
#include <functional>

#include "dashboard/dashboardwidget.h"
#include "dashboard/widgets/chartdata.h"
#include "dashboard/widgets/chartpainting.h"
#include "dashboard/widgets/chartstyle.h"
#include "telemetry/telemetrybinding.h"

class QMouseEvent;
class QPainter;

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
// How each chart looks -- axes, grid, frame, legend -- comes from the
// ChartStyle picked in its header gear menu (widgets/chartstyle.h) and is
// drawn with the shared building blocks in widgets/chartpainting.h. See
// docs/CHART_STYLE.md before adding a new chart kind.

// Looks up the retained history of one of a widget's own fields (by
// fieldId, within the widget's configured device/source/topic) -- nullptr
// when there is none. How MainWindow hands a freshly built widget what its
// field received while no widget was showing it (telemetry/telemetryhistory.h).
using FieldHistoryLookup = std::function<const TelemetrySeriesBuffer*(quint16 fieldId)>;

// What every chart kind has in common, whatever it draws: the header
// controls (pause, clear, gear), the repaint throttle, and the view state
// (style + gear toggles) persisted under config["view"]. A new chart kind
// derives from this (or from ChartWidgetBase below, for series-over-X
// charts) and only declares which gear options it supports via
// viewFeatures().
class StyledChartWidget : public DashboardWidget {
public:
    explicit StyledChartWidget(QWidget* parent = nullptr);

    HeaderControls headerControls() const override {
        return HeaderControl::ConnectionDot | HeaderControl::Pause | HeaderControl::Clear |
               HeaderControl::Settings;
    }
    bool isPaused() const override {
        return m_paused;
    }
    void setPaused(bool paused) override {
        m_paused = paused;
    }

    QVector<WidgetViewOption> viewOptions() const override;
    QJsonObject viewConfigWith(const QString& id, const QVariant& value) const override;
    void setViewConfig(const QJsonObject& view) override;

    const ChartViewOptions& viewState() const {
        return m_view;
    }

    // The style this chart draws in: its own, or the app-wide one
    // (AppSettings::chartStyleId()) while its gear menu says "App default".
    ChartStyleId effectiveStyle() const;

    // Caps how often scheduleRepaint() triggers an actual repaint,
    // independent of how fast samples arrive (topico 14 PASSO 12: ingestion
    // rate stays separate from repaint rate). 33ms (~30Hz) by default, from
    // AppSettings; 0 removes the cap entirely. Exposed only for
    // DebugChartsWindow's stress-mode toggle -- nothing in production code
    // calls this.
    void setRepaintIntervalMs(int ms) {
        m_repaintIntervalMs = ms;
    }

protected:
    // Which gear-menu options this chart kind offers (the style select is
    // always there).
    virtual ChartViewFeatures viewFeatures() const = 0;

    // Parses config["view"] -- call from setConfig().
    void applyViewFromConfig(const QJsonObject& config);

    // Coalesces repaints to at most one per repaint interval.
    void scheduleRepaint();

    // What to draw for `targets` this frame: each value eased toward its
    // target (about 120ms) while motion is animated, straight to it while
    // ThemeManager::reduceMotion(). Keeps repainting itself until every
    // value has settled; `range` (the value span) decides what "settled"
    // means. NaN (no reading) never eases -- it shows at once.
    QVector<double> easedValues(const QVector<double>& targets, double range);

    // Re-applies the data color scheme (ThemeManager::seriesColor()) to the
    // series colors -- called on every appearance change. Kinds with series
    // override it; their own configured colors are kept separately so
    // "Per series" can bring them back.
    virtual void refreshDataColors() {}

    ChartViewOptions m_view;
    bool m_paused = false;

private:
    QVector<double> m_eased;
    QElapsedTimer m_easeClock;
    bool m_easeRepaintPending = false;
    bool m_repaintPending = false;
    int m_repaintIntervalMs = 33;  // initialized from AppSettings in the constructor
};

// Series plotted against a value axis: the line and bar charts. Owns the
// per-series sample buffers, the clickable legend, and the hover position.
class ChartWidgetBase : public StyledChartWidget {
public:
    explicit ChartWidgetBase(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;
    void clearChartData() override;

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
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value);

    // Replaces every series buffer with its field's retained history (the
    // newest samples up to this chart's own capacity), leaving a series
    // whose field has none empty. Meant for a widget just built from JSON,
    // before live samples are connected -- see MainWindow::
    // wireChartWidgetToTelemetry().
    void seedFromHistory(const FieldHistoryLookup& lookup);

    // The parsed config this widget is currently rendering. Read by
    // MainWindow to derive the wire-level SUBSCRIBE this widget implies
    // (topico 17): which (sourceId, topicId) it consumes and how fast it
    // wants samples.
    const ChartConfig& config() const {
        return m_config;
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void refreshDataColors() override;

    // Every series buffer's values, same order as m_config.series.
    QVector<QVector<double>> seriesValues() const;

    // One value axis request per Y axis this chart draws -- a single axis,
    // or one per unit under ChartConfig::autoAxis -- and, for every series,
    // the index of the axis it is scaled by.
    struct SeriesAxes {
        QVector<ChartAxisRequest> requests;
        QVector<int> axisOfSeries;
    };
    SeriesAxes seriesAxes(const QVector<QVector<double>>& values) const;

    // The series legend, wherever the view puts it: rows above and below
    // the plot (Outside), a box in one of the plot's corners, or nowhere.
    // `withValues` adds each series' latest value -- under its name when
    // Outside, after it in a corner box. Fills m_legendHitRects.
    void paintLegends(QPainter& painter, const ChartCartesianLayout& layout,
                      const QVector<QVector<double>>& values, bool withValues,
                      const ChartStyle& style, const ChartColors& colors,
                      const ThemePalette& palette);

    ChartConfig m_config;  // series colors already resolved, see refreshDataColors()
    QVector<QColor> m_ownColors;  // each series' configured color, same order
    QVector<TelemetrySeriesBuffer> m_seriesBuffers;  // one per m_config.series, same order
    // Per-series "hidden via legend click" flag, same index as m_config.series
    // -- toggled by mousePressEvent() below when a click lands inside
    // m_legendHitRects, kept sized/aligned to m_config.series by setConfig()
    // (new series default to visible). A hidden series is skipped in the
    // plot and grayed out in the legend.
    QVector<bool> m_seriesHidden;
    // Clickable rect per series from the legend actually painted last frame
    // (same index as m_seriesHidden) -- mousePressEvent() hit-tests against
    // these instead of recomputing the legend layout itself, so the click
    // target can never drift from what's actually drawn.
    QVector<QRect> m_legendHitRects;
    // Current mouse position in this widget's own coordinates, updated by
    // mouseMoveEvent()/leaveEvent() below and read by DummyLineChartWidget::
    // paintEvent() to place the hover crosshair. m_hasHoverPos is false
    // whenever the mouse isn't over this widget at all (leaveEvent(), or
    // simply never having entered it yet) -- distinct from m_hoverPos sitting
    // outside plotRect, which paintHoverCrosshair() itself handles.
    QPoint m_hoverPos;
    bool m_hasHoverPos = false;
};

class DummyLineChartWidget : public ChartWidgetBase {
public:
    explicit DummyLineChartWidget(QWidget* parent = nullptr);

protected:
    ChartViewFeatures viewFeatures() const override;
    void paintEvent(QPaintEvent* event) override;
};

// Fixed-bar snapshot chart: one bar per configured series (not per sample --
// see paintBarSnapshot() in chartwidgets.cpp), always redrawn at the same X
// position and showing only that series' latest buffered value, with the
// value itself printed directly under its bar. Unlike DummyLineChartWidget,
// there is no history to scroll through, so its gear menu only offers the
// style, the Y axis toggles, and the value labels.
class DummyBarChartWidget : public ChartWidgetBase {
public:
    explicit DummyBarChartWidget(QWidget* parent = nullptr);

protected:
    ChartViewFeatures viewFeatures() const override;
    void paintEvent(QPaintEvent* event) override;
};

class DummyGaugeWidget : public StyledChartWidget {
public:
    explicit DummyGaugeWidget(QWidget* parent = nullptr);

    void setConfig(const QJsonObject& config) override;

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
    void onFieldSample(const traceview::TelemetryFieldBinding& binding, quint64 timestampUs,
                       double value);

    // See ChartWidgetBase::seedFromHistory() -- a ring only takes its field's
    // newest retained value.
    void seedFromHistory(const FieldHistoryLookup& lookup);

    // See ChartWidgetBase::config() -- same role for a gauge's own config.
    const GaugeConfig& config() const {
        return m_config;
    }

protected:
    ChartViewFeatures viewFeatures() const override;
    void paintEvent(QPaintEvent* event) override;
    void refreshDataColors() override;

private:
    GaugeConfig m_config;  // ring colors already resolved, see refreshDataColors()
    QVector<QColor> m_ownColors;
    QVector<double> m_values;  // one per m_config.series, same order -- kept
                               // in sync as m_config.series changes by
                               // setConfig() in the .cpp.
};

}  // namespace traceview
