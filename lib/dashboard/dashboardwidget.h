#pragma once

#include <QWidget>

namespace traceview {

// Base class for anything that can be placed on the dashboard grid: chart,
// serial, button-panel, or any future element kind. Each kind lives in its
// own module under widgets/ (see widgets/chartwidgets.h, widgets/serialmonitorwidget.h,
// widgets/buttonpanelwidget.h) and is registered with WidgetRegistry — the
// grid, DashboardItem, and PropertiesPanel treat every kind identically
// (id/name/key/position), only the widget's own behavior differs.
class DashboardWidget : public QWidget {
public:
    explicit DashboardWidget(QWidget* parent = nullptr) : QWidget(parent) {
        // Qt only auto-paints the QSS `background-color` for plain QWidget
        // instances; subclasses (every widget kind here) stay transparent
        // without this, leaking whatever's behind the cell (e.g. the grid
        // lines DashboardGrid draws in edit mode) through any gap not
        // covered edge-to-edge by a child widget.
        setAttribute(Qt::WA_StyledBackground, true);
    }
};

} // namespace traceview
