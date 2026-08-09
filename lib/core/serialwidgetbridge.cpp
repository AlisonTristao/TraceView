#include "serialwidgetbridge.h"

#include "dashboard/dashboardgrid.h"
#include "dashboard/widgets/controlwidgets.h"
#include "dashboard/widgets/serialmonitorwidget.h"
#include "serialmanager.h"

namespace traceview {

SerialWidgetBridge::SerialWidgetBridge(SerialManager* serialManager, DashboardGrid* grid, QObject* parent)
    : QObject(parent), m_serialManager(serialManager) {
    connect(grid, &DashboardGrid::widgetCreated, this, &SerialWidgetBridge::wireWidget);
}

void SerialWidgetBridge::wireWidget(DashboardWidget* widget) {
    if (auto* button = qobject_cast<PushButtonWidget*>(widget)) {
        connect(button, &PushButtonWidget::sendRequested, m_serialManager, &SerialManager::writeCommand);
    } else if (auto* toggle = qobject_cast<ToggleSwitchWidget*>(widget)) {
        connect(toggle, &ToggleSwitchWidget::sendRequested, m_serialManager, &SerialManager::writeCommand);
    } else if (auto* slider = qobject_cast<SliderWidget*>(widget)) {
        connect(slider, &SliderWidget::sendRequested, m_serialManager, &SerialManager::writeCommand);
    } else if (auto* monitor = qobject_cast<SerialMonitorWidget*>(widget)) {
        connect(monitor, &SerialMonitorWidget::sendRequested, m_serialManager, &SerialManager::write);
        connect(m_serialManager, &SerialManager::dataReceived, monitor, &SerialMonitorWidget::appendData);
    }
}

} // namespace traceview
