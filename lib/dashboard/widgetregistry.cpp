#include "widgetregistry.h"

#include <QCoreApplication>

#include "dashboardwidget.h"
#include "widgets/chartconfigeditor.h"
#include "widgets/chartwidgets.h"
#include "widgets/controlconfigeditor.h"
#include "widgets/controlwidgets.h"
#include "widgets/gaugeconfigeditor.h"
#include "widgets/serialmonitorconfigeditor.h"
#include "widgets/serialmonitorwidget.h"
#include "widgets/textboardconfigeditor.h"
#include "widgets/textboardwidget.h"

namespace traceview {

WidgetRegistry& WidgetRegistry::instance() {
    static WidgetRegistry registry;
    return registry;
}

WidgetRegistry::WidgetRegistry() {
    // WidgetTypeInfo/WidgetRegistry aren't QObjects, so tr() isn't available
    // here; use QCoreApplication::translate() with the owning class as
    // context instead (same idiom as ThemePalette in palettes.cpp).
    registerType(
        {"dummy_line", QCoreApplication::translate("WidgetRegistry", "Line Chart (dummy)"),
         [](QWidget* parent) -> DashboardWidget* { return new DummyLineChartWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* { return new ChartConfigEditor(parent); }});
    registerType(
        {"dummy_bar", QCoreApplication::translate("WidgetRegistry", "Bar Chart (dummy)"),
         [](QWidget* parent) -> DashboardWidget* { return new DummyBarChartWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* { return new ChartConfigEditor(parent); }});
    registerType(
        {"dummy_gauge", QCoreApplication::translate("WidgetRegistry", "Gauge (dummy)"),
         [](QWidget* parent) -> DashboardWidget* { return new DummyGaugeWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* { return new GaugeConfigEditor(parent); }});
    registerType(
        {"serial_monitor", QCoreApplication::translate("WidgetRegistry", "Serial Monitor"),
         [](QWidget* parent) -> DashboardWidget* { return new SerialMonitorWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* {
             return new SerialMonitorConfigEditor(parent);
         }});
    registerType(
        {"text_board", QCoreApplication::translate("WidgetRegistry", "Text Board"),
         [](QWidget* parent) -> DashboardWidget* { return new TextBoardWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* {
             return new TextBoardConfigEditor(parent);
         }});
    registerType({"push_button", QCoreApplication::translate("WidgetRegistry", "Push Button"),
                  [](QWidget* parent) -> DashboardWidget* { return new PushButtonWidget(parent); },
                  [](QWidget* parent) -> WidgetConfigEditor* {
                      return new PushButtonConfigEditor(parent);
                  }});
    registerType(
        {"toggle_switch", QCoreApplication::translate("WidgetRegistry", "Toggle Switch"),
         [](QWidget* parent) -> DashboardWidget* { return new ToggleSwitchWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* {
             return new ToggleSwitchConfigEditor(parent);
         }});
    registerType(
        {"slider", QCoreApplication::translate("WidgetRegistry", "Slider"),
         [](QWidget* parent) -> DashboardWidget* { return new SliderWidget(parent); },
         [](QWidget* parent) -> WidgetConfigEditor* { return new SliderConfigEditor(parent); }});
}

void WidgetRegistry::registerType(const WidgetTypeInfo& info) {
    if (indexOf(info.typeId) >= 0) {
        return;
    }
    m_types.append(info);
}

QVector<WidgetTypeInfo> WidgetRegistry::availableTypes() const {
    return m_types;
}

DashboardWidget* WidgetRegistry::create(const QString& typeId, QWidget* parent) const {
    const int idx = indexOf(typeId);
    if (idx < 0) {
        return nullptr;
    }
    return m_types[idx].factory(parent);
}

QString WidgetRegistry::displayName(const QString& typeId) const {
    const int idx = indexOf(typeId);
    return idx >= 0 ? m_types[idx].displayName : QString();
}

int WidgetRegistry::indexOf(const QString& typeId) const {
    for (int i = 0; i < m_types.size(); ++i) {
        if (m_types[i].typeId == typeId) {
            return i;
        }
    }
    return -1;
}

}  // namespace traceview
