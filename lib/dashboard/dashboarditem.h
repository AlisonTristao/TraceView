#pragma once

#include <QJsonObject>
#include <QString>

namespace traceview {

// Where one dashboard widget sits on the canvas: its registered type and
// its position/size as fractions of the canvas (0..1). Deliberately not
// stored in grid cells — the background grid's precision (how many cells
// wide it's divided into) is just a snapping aid for drag/resize, so a
// widget's actual size/position never changes when that precision changes.
// Plain data — the grid owns the actual QWidget.
struct DashboardItem {
    QString id;       // QUuid, stable identity across the item's lifetime
    QString typeId;   // key into WidgetRegistry

    qreal x = 0;       // fraction of canvas width, 0..1
    qreal y = 0;       // fraction of canvas height, 0..1
    qreal width = 0;   // fraction of canvas width, 0..1
    qreal height = 0;  // fraction of canvas height, 0..1
};

QJsonObject dashboardItemToJson(const DashboardItem& item);

// Returns a default-constructed DashboardItem and sets *ok = false if
// `object` is missing required fields.
DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok);

} // namespace traceview
