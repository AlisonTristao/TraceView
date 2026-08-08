#pragma once

#include <QJsonObject>
#include <QString>

namespace traceview {

// Where one dashboard widget sits on the grid: its registered type and the
// cells it occupies. Plain data — the grid owns the actual QWidget.
struct DashboardItem {
    QString id;       // QUuid, stable identity across the item's lifetime
    QString typeId;   // key into WidgetRegistry

    int row = 0;
    int column = 0;
    int rowSpan = 1;
    int columnSpan = 1;
};

QJsonObject dashboardItemToJson(const DashboardItem& item);

// Returns a default-constructed DashboardItem and sets *ok = false if
// `object` is missing required fields.
DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok);

} // namespace traceview
