#pragma once

#include <QWidget>

namespace traceview {

// Base class for anything that can be placed on the dashboard grid. Real
// chart/telemetry widgets will subclass this; for now only the dummy
// placeholders in dummywidgets.h do.
class DashboardWidget : public QWidget {
public:
    explicit DashboardWidget(QWidget* parent = nullptr) : QWidget(parent) {}
};

} // namespace traceview
