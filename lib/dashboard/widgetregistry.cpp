#include "widgetregistry.h"

#include "dashboardwidget.h"
#include "dummywidgets.h"

namespace traceview {

WidgetRegistry& WidgetRegistry::instance() {
    static WidgetRegistry registry;
    return registry;
}

WidgetRegistry::WidgetRegistry() {
    registerType({"dummy_line", "Line Chart (dummy)",
                  [](QWidget* parent) -> DashboardWidget* { return new DummyLineChartWidget(parent); }});
    registerType({"dummy_bar", "Bar Chart (dummy)",
                  [](QWidget* parent) -> DashboardWidget* { return new DummyBarChartWidget(parent); }});
    registerType({"dummy_gauge", "Gauge (dummy)",
                  [](QWidget* parent) -> DashboardWidget* { return new DummyGaugeWidget(parent); }});
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

} // namespace traceview
