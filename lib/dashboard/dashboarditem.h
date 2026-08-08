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
