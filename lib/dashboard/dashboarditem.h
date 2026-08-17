#pragma once

#include <QJsonObject>
#include <QString>

namespace traceview {

// Where one dashboard widget sits on the grid: its registered type and its
// position/size as fractions (0.0-1.0) of the grid's usable canvas area.
// Proportional coordinates keep layouts resolution-independent — an item
// stays at the same relative spot/size across resizes with no clamping or
// reflow needed. Plain data — the grid owns the actual QWidget and is the
// only place that converts these fractions to pixels.
struct DashboardItem {
    QString id;       // QUuid, stable identity across the item's lifetime
    QString typeId;   // key into WidgetRegistry
    QString name;     // user-editable display name; empty falls back to the
                       // type's default display name (see displayNameFor())
    QString key;      // user-editable, must be unique when non-empty (see
                       // DashboardGrid::isKeyAvailable); the future handle
                       // external data updates will target, independent of
                       // `id` (which stays internal/auto-generated) and of
                       // `name`/`typeId` (either of which can change freely)
    QJsonObject config; // type-specific settings edited via the properties
                        // panel's WidgetConfigEditor for `typeId`; opaque to
                        // everything except that editor, empty for types
                        // that don't register one
    QString groupId;   // empty = ungrouped; items sharing a non-empty value
                        // always select/drag together as one rigid unit (see
                        // DashboardGrid::groupSelected()/ungroupSelected())

    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

QJsonObject dashboardItemToJson(const DashboardItem& item);

// Returns a default-constructed DashboardItem and sets *ok = false if
// `object` is missing required fields.
DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok);

} // namespace traceview
