#pragma once

#include <QString>

namespace traceview {

// For AI assistants (the list_widget_types tool, docs/MCP.md): what a widget
// type is for and the shape of its DashboardItem::config, written for a model
// that has to build that JSON without the properties panel. Every type
// WidgetRegistry registers needs an entry -- test_widgetregistry fails on a
// type without one. Empty for an unknown type. Deliberately not translated:
// it is read by the model, not shown in the UI.
QString widgetAgentHelp(const QString& typeId);

}  // namespace traceview
