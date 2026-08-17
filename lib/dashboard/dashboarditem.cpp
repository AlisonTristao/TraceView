#include "dashboarditem.h"

namespace traceview {

QJsonObject dashboardItemToJson(const DashboardItem& item) {
    QJsonObject object;
    object["id"] = item.id;
    object["type"] = item.typeId;
    object["name"] = item.name;
    object["key"] = item.key;
    object["config"] = item.config;
    object["groupId"] = item.groupId;
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
    // Absent in projects saved before renaming existed; empty just means
    // "use the type's default display name" (see displayNameFor()).
    item.name = object.value("name").toString();
    item.key = object.value("key").toString();
    // Absent in projects saved before per-type config existed.
    item.config = object.value("config").toObject();
    // Absent in projects saved before grouping existed.
    item.groupId = object.value("groupId").toString();
    item.x = qBound(0.0, object["x"].toDouble(0.0), 1.0);
    item.y = qBound(0.0, object["y"].toDouble(0.0), 1.0);
    item.width = qBound(0.0, object["width"].toDouble(0.0), 1.0);
    item.height = qBound(0.0, object["height"].toDouble(0.0), 1.0);

    *ok = !item.id.isEmpty() && !item.typeId.isEmpty() && item.width > 0.0 && item.height > 0.0;
    return item;
}

} // namespace traceview
