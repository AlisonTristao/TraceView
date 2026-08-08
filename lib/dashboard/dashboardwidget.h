#pragma once

#include <QWidget>

namespace traceview {

// Base class for anything that can be placed on the dashboard grid: chart,
// serial, button-panel, or any future element kind. Each kind lives in its
// own module under widgets/ (see widgets/chartwidgets.h, widgets/serialpanelwidget.h,
// widgets/buttonpanelwidget.h) and is registered with WidgetRegistry — the
// grid, DashboardItem, and PropertiesPanel treat every kind identically
// (id/name/key/position), only the widget's own behavior differs.
class DashboardWidget : public QWidget {
public:
    explicit DashboardWidget(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace traceview
