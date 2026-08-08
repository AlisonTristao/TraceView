#include "dashboarditem.h"

namespace traceview {

QJsonObject dashboardItemToJson(const DashboardItem& item) {
    QJsonObject object;
    object["id"] = item.id;
    object["type"] = item.typeId;
    object["x"] = item.x;
    object["y"] = item.y;
    object["width"] = item.width;
    object["height"] = item.height;
    return object;
}

DashboardItem dashboardItemFromJson(const QJsonObject& object, bool* ok) {
    DashboardItem item;
    if (!object.contains("id") || !object.contains("type")) {
        *ok = false;
        return item;
    }

    item.id = object["id"].toString();
    item.typeId = object["type"].toString();
    item.x = qBound(0.0, object["x"].toDouble(0.0), 1.0);
    item.y = qBound(0.0, object["y"].toDouble(0.0), 1.0);
    item.width = qBound(0.0, object["width"].toDouble(0.0), 1.0);
    item.height = qBound(0.0, object["height"].toDouble(0.0), 1.0);

    *ok = !item.id.isEmpty() && !item.typeId.isEmpty() && item.width > 0.0 && item.height > 0.0;
    return item;
}

} // namespace traceview
