#pragma once

#include <functional>

#include <QString>
#include <QVector>

class QWidget;

namespace traceview {

class DashboardWidget;
class WidgetConfigEditor;

// One selectable chart type: a stable id, a display name for the "Add
// Widget" picker, and a factory that builds an instance.
struct WidgetTypeInfo {
    QString typeId;
    QString displayName;
    std::function<DashboardWidget*(QWidget*)> factory;

    // Optional: builds the type-specific settings shown in the properties
    // panel below Type/Name/Key (see WidgetConfigEditor). Left null (the
    // default) for types with no config yet — the panel just shows nothing
    // below the divider in that case.
    std::function<WidgetConfigEditor*(QWidget*)> configEditorFactory;
};

// Owns the set of chart types the dashboard can place. Adding a new type
// later is a single registerType() call — see docs/DASHBOARD.md.
class WidgetRegistry {
public:
    static WidgetRegistry& instance();

    // No-op if `info.typeId` is already registered.
    void registerType(const WidgetTypeInfo& info);

    QVector<WidgetTypeInfo> availableTypes() const;

    // Returns nullptr if `typeId` isn't registered.
    DashboardWidget* create(const QString& typeId, QWidget* parent) const;

    QString displayName(const QString& typeId) const;

private:
    WidgetRegistry();

    int indexOf(const QString& typeId) const;

    QVector<WidgetTypeInfo> m_types;
};

} // namespace traceview
