#pragma once

#include <QJsonObject>
#include <QWidget>

namespace traceview {

// Base class for a widget type's type-specific settings, shown in the
// PropertiesPanel below the common Type/Name/Key fields (see
// PropertiesPanel, lib/core/propertiespanel.h). A type opts in by setting
// WidgetTypeInfo::configEditorFactory (lib/dashboard/widgetregistry.h) to a
// factory that builds one of these; types that don't register one simply
// show no config section. The panel owns the instance, calls setConfig()
// whenever the selection/config changes elsewhere (undo/redo, load), and
// listens for configChanged() to push edits back out — mirrors how
// DashboardWidget is the per-type extension point for the canvas cell
// itself, this is the analogous one for its properties editor.
class WidgetConfigEditor : public QWidget {
    Q_OBJECT

public:
    explicit WidgetConfigEditor(QWidget* parent = nullptr) : QWidget(parent) {}

    // One-way sync into the editor — must never emit configChanged().
    virtual void setConfig(const QJsonObject& config) = 0;

    virtual QJsonObject config() const = 0;

signals:
    // Emitted whenever the user edits something; the new value is fetched
    // via config() rather than passed here, same shape as the rest of the
    // panel's *ChangeRequested signals.
    void configChanged();
};

} // namespace traceview
