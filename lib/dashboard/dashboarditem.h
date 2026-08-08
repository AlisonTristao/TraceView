#pragma once

#include <QJsonObject>
#include <QString>

namespace traceview {

// Where one dashboard widget sits on the grid: its registered type and the
// cells it occupies. The grid uses a fixed, small square cell size (see
// DashboardGrid), so positions/spans are plain integer cell counts — no
// rounding math involved, which is what keeps items pixel-exact on the
// grid lines. Plain data — the grid owns the actual QWidget.
struct DashboardItem {
    QString id;       // QUuid, stable identity across the item's lifetime
    QString typeId;   // key into WidgetRegistry

    int column = 0;
    int row = 0;
    int columnSpan = 1;
    int rowSpan = 1;
};

QJsonObject dashboardItemToJson(const DashboardItem& item);

// Returns a default-constructed DashboardItem and sets *ok = false if
// `object` is missing required fields.
DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok);

} // namespace traceview
