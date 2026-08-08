#pragma once

#include "dashboard/dashboardwidget.h"

namespace traceview {

// Front-end shell for a panel of action buttons. Purely visual for now —
// the buttons don't trigger anything; wiring each one to a real action is
// a later, separate step.
class ButtonPanelWidget : public DashboardWidget {
public:
    explicit ButtonPanelWidget(QWidget* parent = nullptr);
};

} // namespace traceview
