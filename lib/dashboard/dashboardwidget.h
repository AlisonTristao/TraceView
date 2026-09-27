#pragma once

#include <QByteArray>
#include <QFlags>
#include <QJsonObject>
#include <QPainterPath>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include "traceview/framestyle.h"
#include "dashboard/widgetviewoption.h"

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
    virtual bool wantsCellHeader() const {
        return true;
    }

    // Called by DashboardCell::setEditMode() on every edit/run transition, so
    // widgets whose look depends on which mode is active can react. Default
    // no-op -- only the headerless controls (widgets/controlwidgets.cpp)
    // override this today: they show a contrasting surface panel while
    // arranging (Layout), so the rounded cell corner reads as a panel and
    // not a stray disconnected curve, but drop that fill in Run so only the
    // control itself (button/switch/slider) is visible, with no background
    // box behind it.
    virtual void setEditModeHint(bool editMode) {
        Q_UNUSED(editMode);
    }

    // Called by DashboardGrid with this item's DashboardItem::config: once
    // right after construction (fresh insert, load from disk, or a type
    // change), and again every time the user edits it in the
    // PropertiesPanel's WidgetConfigEditor (including undo/redo of that
    // edit) -- see DashboardGrid::createCell/applySetConfig. Default no-op
    // for kinds with no ConfigEditor registered (see WidgetRegistry).
    virtual void setConfig(const QJsonObject& config) {
        Q_UNUSED(config);
    }

    // The operational controls DashboardCell's header draws for this widget
    // in Run mode (see dashboard/dashboardcell.cpp): a connection-state dot
    // before the title, and buttons packed from the header's right edge in
    // the order Settings (gear), Clear, Pause -- so a widget offering only
    // Clear gets it in the corner a chart's gear sits in. None by default.
    enum class HeaderControl : quint8 {
        ConnectionDot = 1u << 0,
        Pause = 1u << 1,
        Clear = 1u << 2,
        Settings = 1u << 3,
    };
    Q_DECLARE_FLAGS(HeaderControls, HeaderControl)

    virtual HeaderControls headerControls() const {
        return {};
    }

    // Pause state the header's play/pause button toggles: while paused, a
    // chart-family widget drops incoming samples instead of buffering them,
    // so the plot visibly freezes until resumed. No-ops for kinds that don't
    // offer HeaderControl::Pause.
    virtual bool isPaused() const {
        return false;
    }
    virtual void setPaused(bool paused) {
        Q_UNUSED(paused);
    }

    // Clears whatever this widget is currently showing -- chart series
    // buffers, a terminal's scrollback, a log -- triggered by the header's
    // clear button (HeaderControl::Clear).
    virtual void clearChartData() {}

    // The header settings-gear menu's content (DashboardCell::
    // showSettingsMenu()): every option this widget offers, with its
    // current value. Empty (the default) means no gear menu at all. Chart
    // kinds build this from their ChartViewOptions (widgets/chartstyle.h);
    // see docs/CHART_STYLE.md.
    virtual QVector<WidgetViewOption> viewOptions() const {
        return {};
    }

    // This widget's view state (the object DashboardGrid stores under
    // config["view"]) with option `id` changed to `value` -- a bool for a
    // Toggle option, a choice id for a Choice. Pure: nothing changes until
    // setViewConfig() is called with the result.
    virtual QJsonObject viewConfigWith(const QString& id, const QVariant& value) const {
        Q_UNUSED(id);
        Q_UNUSED(value);
        return {};
    }

    // Applies a view state produced by viewConfigWith(). setConfig() applies
    // config["view"] the same way, so a saved dashboard reopens with the
    // view it was saved with.
    virtual void setViewConfig(const QJsonObject& view) {
        Q_UNUSED(view);
    }

    // Which of this widget's own corners take the frame style's corner shape
    // (rounded, chamfered...; see traceview/framestyle.h), kept in
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

    // `r` in the current frame style's shape, at this widget's own
    // per-corner state.
    QPainterPath roundedPath(const QRectF& r) const {
        return currentFramePath(r, m_roundTopLeft, m_roundTopRight, m_roundBottomLeft,
                                m_roundBottomRight);
    }

    // This widget's rounded fill spans its true, full bounds. The cell
    // outline is composed later by DashboardCell's BorderOverlay child, so the fill
    // does not need to compensate for the outline pen's half-pixel inset.
    QPainterPath contentFillPath() const {
        return roundedPath(QRectF(rect()));
    }

private:
    bool m_roundTopLeft = true;
    bool m_roundTopRight = true;
    bool m_roundBottomLeft = true;
    bool m_roundBottomRight = true;
};

}  // namespace traceview

// Outside the namespace, as Qt requires (see chartstyle.h).
Q_DECLARE_OPERATORS_FOR_FLAGS(traceview::DashboardWidget::HeaderControls)
