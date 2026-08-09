#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QWidget>

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

    // Called by SerialDataRouter (lib/core/serialdatarouter.h) when a
    // decoded serial frame's <id> matches this widget's DashboardItem::key
    // (see BACKEND_TODO.txt Task 5). Default no-op so kinds that don't
    // consume serial data (most controls) aren't forced to override this --
    // real per-type payload parsing (delimited text vs bytes, per
    // ChartConfigEditor/GaugeConfigEditor Format) is Tasks 7/8.
    virtual void onSerialPayload(const QByteArray& payload) { Q_UNUSED(payload); }

    // Called by DashboardGrid with this item's DashboardItem::config: once
    // right after construction (fresh insert, load from disk, or a type
    // change), and again every time the user edits it in the
    // PropertiesPanel's WidgetConfigEditor (including undo/redo of that
    // edit) -- see DashboardGrid::createCell/applySetConfig. Default no-op
    // for kinds with no ConfigEditor registered (see WidgetRegistry).
    virtual void setConfig(const QJsonObject& config) { Q_UNUSED(config); }
};

} // namespace traceview
