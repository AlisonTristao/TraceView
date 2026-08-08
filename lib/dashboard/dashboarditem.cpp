#include "dashboarditem.h"

namespace traceview {

QJsonObject dashboardItemToJson(const DashboardItem& item) {
    QJsonObject object;
    object["id"] = item.id;
    object["type"] = item.typeId;
    object["row"] = item.row;
    object["column"] = item.column;
    object["rowSpan"] = item.rowSpan;
    object["columnSpan"] = item.columnSpan;
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
    item.row = object["row"].toInt(0);
    item.column = object["column"].toInt(0);
    item.rowSpan = qMax(1, object["rowSpan"].toInt(1));
    item.columnSpan = qMax(1, object["columnSpan"].toInt(1));

    *ok = !item.id.isEmpty() && !item.typeId.isEmpty();
    return item;
}

} // namespace traceview
