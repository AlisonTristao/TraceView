#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QPainterPath>
#include <QString>
#include <QWidget>

#include "dashboard/roundedcorners.h"

namespace traceview {

// Base class for anything that can be placed on the dashboard grid: chart,
// serial, control (push button/toggle/slider), or any future element kind.
// Each kind lives in its own module under widgets/ (see
// widgets/chartwidgets.h, widgets/serialmonitorwidget.h,
// widgets/controlwidgets.h) and is registered with WidgetRegistry — the
// grid, DashboardItem, and PropertiesPanel treat every kind identically
// (id/name/key/position), only the widget's own behavior differs.
class DashboardWidget : public QWidget {
public:
    explicit DashboardWidget(QWidget* parent = nullptr) : QWidget(parent) {
        // Qt only auto-paints the QSS `background-color` for plain QWidget
        // instances; subclasses (every widget kind here) stay transparent
        // without this, leaking whatever's behind the cell (e.g. the grid
        // lines DashboardGrid draws in edit mode) through any gap not
        // covered edge-to-edge by a child widget. Kinds that want a
        // transparent cell (see widgets/controlwidgets.cpp) turn this back
        // off themselves and take on the responsibility of covering their
        // own area edge-to-edge so nothing leaks through instead.
        setAttribute(Qt::WA_StyledBackground, true);
    }

    // Whether DashboardCell reserves its header strip (drag handle + title,
    // see dashboard/dashboardcell.cpp) above this widget in edit mode. True
    // for kinds where the title is useful context relative to the cell's
    // size (chart, serial monitor). Small single-control kinds — push
    // button, toggle switch, slider (widgets/controlwidgets.h) — override
    // this to false: the header would eat a disproportionate share of an
    // already-small cell. DashboardCell falls back to letting a click
    // anywhere in the selected body start a move-drag in that case, since
    // there's no dedicated header region left to grab.
    virtual bool wantsCellHeader() const { return true; }

    // Called by DashboardCell::setEditMode() on every edit/run transition, so
    // widgets whose look depends on which mode is active can react. Default
    // no-op -- only the headerless controls (widgets/controlwidgets.cpp)
    // override this today: they show a contrasting surface panel while
    // arranging (Layout), so the rounded cell corner reads as a panel and
    // not a stray disconnected curve, but drop that fill in Run so only the
    // control itself (button/switch/slider) is visible, with no background
    // box behind it.
    virtual void setEditModeHint(bool editMode) { Q_UNUSED(editMode); }

    // Called by DashboardGrid with this item's DashboardItem::config: once
    // right after construction (fresh insert, load from disk, or a type
    // change), and again every time the user edits it in the
    // PropertiesPanel's WidgetConfigEditor (including undo/redo of that
    // edit) -- see DashboardGrid::createCell/applySetConfig. Default no-op
    // for kinds with no ConfigEditor registered (see WidgetRegistry).
    virtual void setConfig(const QJsonObject& config) { Q_UNUSED(config); }

    // True for kinds that want the extra operational cluster DashboardCell's
    // header draws in Run mode: a connection-state dot before the title, and
    // pause/resume + clear + settings-gear buttons at the header's right
    // edge (see dashboard/dashboardcell.cpp). Defaults to false so gauge/
    // serial-monitor headers stay exactly as they are today; ChartWidgetBase
    // (widgets/chartwidgets.h) is the only override for now.
    virtual bool wantsHeaderControls() const { return false; }

    // Pause state the header's play/pause button toggles: while paused, a
    // chart-family widget drops incoming samples instead of buffering them,
    // so the plot visibly freezes until resumed. No-ops for kinds that don't
    // override wantsHeaderControls().
    virtual bool isPaused() const { return false; }
    virtual void setPaused(bool paused) { Q_UNUSED(paused); }

    // Clears whatever history this widget is currently holding (chart series
    // buffers), triggered by the header's clear button.
    virtual void clearChartData() {}

    // Whether the "latest value" row below the plot is drawn — the header's
    // settings-gear menu toggles this. Defaults to true, matching the
    // always-on behavior before this toggle existed.
    virtual bool showsLastValueRow() const { return true; }
    virtual void setShowsLastValueRow(bool show) { Q_UNUSED(show); }

    // Whether a line chart marks every point where a series' line crosses a
    // vertical X-gridline with a dot + its interpolated value -- the header's
    // settings-gear menu toggles this. Defaults to false (opt-in via the
    // gear), unlike showsLastValueRow() above, since a dot-per-gridline-per-
    // series is visually busier than a single legend row.
    virtual bool showsGridPointMarkers() const { return false; }
    virtual void setShowsGridPointMarkers(bool show) { Q_UNUSED(show); }

    // Whether a line chart tracks the mouse with a vertical guide line plus a
    // balloon (beside the cursor) listing every series' interpolated value at
    // the hovered X -- the header's settings-gear menu toggles this. Defaults
    // to false (opt-in via the gear), same reasoning as showsGridPointMarkers()
    // above.
    virtual bool showsHoverCrosshair() const { return false; }
    virtual void setShowsHoverCrosshair(bool show) { Q_UNUSED(show); }

    // Which shape a line chart reconstructs between buffered samples with --
    // the header's settings-gear menu's interpolation select box reads/writes
    // this. A plain QString id ("linear"/"zoh"/"stem"/"none", see
    // ChartLineInterpolation in widgets/chartdata.h) rather than that enum
    // type directly, so this base class stays free of any one widget kind's
    // types -- same reasoning as the typeId strings WidgetRegistry dispatches
    // on elsewhere. Defaults to "linear", matching the straight-line behavior
    // before this setting existed. Only ChartWidgetBase overrides this (bar
    // chart inherits it, same as showsGridPointMarkers(), but its paintEvent
    // never reads it).
    virtual QString lineInterpolation() const { return QStringLiteral("linear"); }
    virtual void setLineInterpolation(const QString& id) { Q_UNUSED(id); }

    // Whether the header's settings-gear menu has anything to show at all --
    // gates the three chart toggles above (showsLastValueRow/
    // showsGridPointMarkers/showsHoverCrosshair) as a group. Defaults to
    // false, matching every non-chart kind (which has no gear menu content
    // regardless, see wantsHeaderControls()). DummyLineChartWidget is the
    // only override for now: DummyBarChartWidget's snapshot-style render
    // always shows each bar's current value directly under it and has no
    // line/grid/hover concept to toggle, so it deliberately stays false
    // rather than exposing options that would silently do nothing.
    virtual bool hasChartOptionsMenu() const { return false; }

    // Which of this widget's own corners should read as rounded, kept in
    // sync by DashboardCell::updateContentMask() every time header
    // presence/geometry changes -- bottom corners are rounded whenever they
    // sit on the cell's outer edge, top corners only when there's no header
    // strip above stealing that edge (see the comment there). Content-
    // painting code (e.g. paintBackground() in chartwidgets.cpp) reads this
    // back via contentFillPath()/roundedPath() so whatever a widget paints
    // inside its own paintEvent lands on the exact same curve as the
    // QWidget::setMask() DashboardCell applies around it -- see "Corner radius" in
    // docs/VISUAL_IDENTITY.md ("the corners must line up"). Defaults to
    // fully rounded, matching the common case (no header) before the first
    // updateContentMask() call.
    void setRoundedCorners(bool topLeft, bool topRight, bool bottomLeft, bool bottomRight) {
        m_roundTopLeft = topLeft;
        m_roundTopRight = topRight;
        m_roundBottomLeft = bottomLeft;
        m_roundBottomRight = bottomRight;
    }

    // `r` rounded at this widget's own per-corner state.
    QPainterPath roundedPath(const QRectF& r) const {
        return partiallyRoundedRect(r, kContainerCornerRadius, m_roundTopLeft, m_roundTopRight, m_roundBottomLeft,
                                     m_roundBottomRight);
    }

    // This widget's rounded fill spans its true, full bounds. The cell
    // outline is composed later by DashboardCell's BorderOverlay child, so the fill
    // does not need to compensate for the outline pen's half-pixel inset.
    QPainterPath contentFillPath() const { return roundedPath(QRectF(rect())); }

private:
    bool m_roundTopLeft = true;
    bool m_roundTopRight = true;
    bool m_roundBottomLeft = true;
    bool m_roundBottomRight = true;
};

} // namespace traceview
