#include "dashboarditem.h"

namespace traceview {

namespace {

// Single source of truth for the breakpoint <-> JSON-key mapping used by
// both directions below -- one array instead of a switch per direction, so
// breakpointFromString's loop is exhaustive by construction instead of an
// if/if/fallthrough that silently drops a size someone forgets to add.
constexpr const char* kBreakpointNames[kDashboardBreakpointCount] = {"small", "medium", "large"};

QJsonObject geometryToJson(const DashboardItem::Geometry& geometry) {
    QJsonObject object;
    object["x"] = geometry.x;
    object["y"] = geometry.y;
    object["width"] = geometry.width;
    object["height"] = geometry.height;
    return object;
}

// maxPages bounds y/height: 1.0 for Large (and the legacy flat format), up
// to kMaxCanvasPages for a Small/Medium layout -- see DashboardItem::Geometry.
DashboardItem::Geometry geometryFromJson(const QJsonObject& object, double maxPages = 1.0) {
    DashboardItem::Geometry geometry;
    geometry.x = qBound(0.0, object.value("x").toDouble(0.0), 1.0);
    geometry.y = qBound(0.0, object.value("y").toDouble(0.0), maxPages);
    geometry.width = qBound(0.0, object.value("width").toDouble(0.0), 1.0);
    geometry.height = qBound(0.0, object.value("height").toDouble(0.0), maxPages);
    return geometry;
}

}  // namespace

QString breakpointToString(DashboardBreakpoint breakpoint) {
    return QString::fromLatin1(kBreakpointNames[size_t(breakpoint)]);
}

// Defaults to Large for anything unrecognized -- absent (projects saved
// before per-screen-size layouts existed) or corrupt alike.
DashboardBreakpoint breakpointFromString(const QString& value) {
    for (int i = 0; i < kDashboardBreakpointCount; ++i) {
        if (value == QLatin1String(kBreakpointNames[i])) {
            return DashboardBreakpoint(i);
        }
    }
    return DashboardBreakpoint::Large;
}

QJsonObject dashboardItemToJson(const DashboardItem& item) {
    QJsonObject object;
    object["id"] = item.id;
    object["type"] = item.typeId;
    object["name"] = item.name;
    object["key"] = item.key;
    object["config"] = item.config;
    object["groupId"] = item.groupId;

    QJsonObject layouts;
    for (int i = 0; i < kDashboardBreakpointCount; ++i) {
        const DashboardBreakpoint breakpoint = DashboardBreakpoint(i);
        layouts[breakpointToString(breakpoint)] = geometryToJson(item.geometry(breakpoint));
    }
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
        for (int i = 0; i < kDashboardBreakpointCount; ++i) {
            const DashboardBreakpoint breakpoint = DashboardBreakpoint(i);
            item.geometry(breakpoint) =
                geometryFromJson(layouts.value(breakpointToString(breakpoint)).toObject(),
                                 isPreviewBreakpoint(breakpoint) ? kMaxCanvasPages : 1.0);
        }
    } else {
        // Projects saved before per-screen-size layouts existed store a
        // single flat x/y/width/height -- seed all three breakpoints with it
        // so an old dashboard looks exactly as before until a developer
        // customizes one breakpoint's arrangement on its own.
        const DashboardItem::Geometry legacy = geometryFromJson(object);
        item.geometries.fill(legacy);
    }

    const DashboardItem::Geometry& large = item.geometry(DashboardBreakpoint::Large);
    *ok = !item.id.isEmpty() && !item.typeId.isEmpty() && large.width > 0.0 && large.height > 0.0;
    return item;
}

}  // namespace traceview
