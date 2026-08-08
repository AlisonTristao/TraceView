#pragma once

#include <functional>

#include <QString>
#include <QVector>

class QWidget;

namespace traceview {

class DashboardWidget;

// One selectable chart type: a stable id, a display name for the "Add
// Widget" picker, and a factory that builds an instance.
struct WidgetTypeInfo {
    QString typeId;
    QString displayName;
    std::function<DashboardWidget*(QWidget*)> factory;
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
