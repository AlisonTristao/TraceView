#include "dashboarditem.h"

namespace traceview {

namespace {

QJsonObject geometryToJson(const DashboardItem::Geometry& geometry) {
    QJsonObject object;
    object["x"] = geometry.x;
    object["y"] = geometry.y;
    object["width"] = geometry.width;
    object["height"] = geometry.height;
    return object;
}

DashboardItem::Geometry geometryFromJson(const QJsonObject& object) {
    DashboardItem::Geometry geometry;
    geometry.x = qBound(0.0, object.value("x").toDouble(0.0), 1.0);
    geometry.y = qBound(0.0, object.value("y").toDouble(0.0), 1.0);
    geometry.width = qBound(0.0, object.value("width").toDouble(0.0), 1.0);
    geometry.height = qBound(0.0, object.value("height").toDouble(0.0), 1.0);
    return geometry;
}

}  // namespace

QJsonObject dashboardItemToJson(const DashboardItem& item) {
    QJsonObject object;
    object["id"] = item.id;
    object["type"] = item.typeId;
    object["name"] = item.name;
    object["key"] = item.key;
    object["config"] = item.config;
    object["groupId"] = item.groupId;

    QJsonObject layouts;
    layouts["small"] = geometryToJson(item.small);
    layouts["medium"] = geometryToJson(item.medium);
    layouts["large"] = geometryToJson(item.large);
    object["layouts"] = layouts;
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

    if (object.contains("layouts")) {
        const QJsonObject layouts = object.value("layouts").toObject();
        item.small = geometryFromJson(layouts.value("small").toObject());
        item.medium = geometryFromJson(layouts.value("medium").toObject());
        item.large = geometryFromJson(layouts.value("large").toObject());
    } else {
        // Projects saved before per-screen-size layouts existed store a
        // single flat x/y/width/height -- seed all three breakpoints with it
        // so an old dashboard looks exactly as before until a developer
        // customizes one breakpoint's arrangement on its own.
        const DashboardItem::Geometry legacy = geometryFromJson(object);
        item.small = legacy;
        item.medium = legacy;
        item.large = legacy;
    }

    *ok = !item.id.isEmpty() && !item.typeId.isEmpty() && item.large.width > 0.0 &&
          item.large.height > 0.0;
    return item;
}

}  // namespace traceview
