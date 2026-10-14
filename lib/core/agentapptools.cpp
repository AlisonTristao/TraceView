#include "core/agentapptools.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>

#include "agent/toolregistry.h"
#include "backend/backend.h"
#include "core/deviceconnection.h"
#include "core/mainwindow.h"
#include "core/ribbon.h"
#include "dashboard/dashboardgrid.h"
#include "dashboard/widgetagenthelp.h"
#include "dashboard/widgetconfigeditor.h"
#include "dashboard/widgetregistry.h"
#include "devices/devicesgrid.h"
#include "project/dashboardgallery.h"
#include "project/projectstore.h"
#include "project/workspacemanager.h"
#include "theme/dialogpresenter.h"

namespace traceview {

namespace {

// Screenshots are scaled down to this on their longer side: enough to read a
// layout, without spending the model's context on full-resolution pixels.
constexpr int kMaxScreenshotSide = 1600;
constexpr int kDefaultSignalPoints = 200;
constexpr int kMaxSignalPoints = 2000;

double round2(double value) {
    return std::round(value * 100.0) / 100.0;
}

QStringList breakpointNames() {
    QStringList names;
    for (int i = 0; i < kDashboardBreakpointCount; ++i) {
        names.append(breakpointToString(DashboardBreakpoint(i)));
    }
    return names;
}

// Positions go over the wire in grid cells (see get_workspace's `grid`), not
// DashboardItem's canvas fractions: cells are what the grid snaps to and
// what a model can reason about ("8 columns wide").
QJsonObject geometryToCells(const DashboardItem::Geometry& geometry,
                            DashboardBreakpoint breakpoint) {
    const int columns = DashboardGrid::gridColumns(breakpoint);
    const int rows = DashboardGrid::gridRowsPerPage(breakpoint);
    return QJsonObject{
        {"col", round2(geometry.x * columns)},
        {"row", round2(geometry.y * rows)},
        {"cols", round2(geometry.width * columns)},
        {"rows", round2(geometry.height * rows)},
    };
}

bool cellsToGeometry(const QJsonObject& cells, DashboardBreakpoint breakpoint, QRectF* out,
                     QString* error) {
    for (const char* key : {"col", "row", "cols", "rows"}) {
        if (!cells.value(QLatin1String(key)).isDouble()) {
            *error = QStringLiteral("Layout \"%1\" needs numeric col, row, cols and rows.")
                         .arg(breakpointToString(breakpoint));
            return false;
        }
    }
    const double columns = DashboardGrid::gridColumns(breakpoint);
    const double rows = DashboardGrid::gridRowsPerPage(breakpoint);
    *out = QRectF(cells.value("col").toDouble() / columns, cells.value("row").toDouble() / rows,
                  cells.value("cols").toDouble() / columns, cells.value("rows").toDouble() / rows);
    return true;
}

QJsonObject widgetToJson(const DashboardItem& item) {
    QJsonObject layouts;
    for (int i = 0; i < kDashboardBreakpointCount; ++i) {
        const DashboardBreakpoint breakpoint = DashboardBreakpoint(i);
        layouts[breakpointToString(breakpoint)] =
            geometryToCells(item.geometry(breakpoint), breakpoint);
    }
    return QJsonObject{
        {"id", item.id},           {"type", item.typeId},   {"name", item.name},
        {"key", item.key},         {"config", item.config}, {"layouts", layouts},
        {"groupId", item.groupId},
    };
}

QJsonObject gridJson(const DashboardGrid* grid) {
    QJsonObject layouts;
    for (int i = 0; i < kDashboardBreakpointCount; ++i) {
        const DashboardBreakpoint breakpoint = DashboardBreakpoint(i);
        QJsonObject spec{
            {"columns", DashboardGrid::gridColumns(breakpoint)},
            {"rowsPerPage", DashboardGrid::gridRowsPerPage(breakpoint)},
        };
        if (grid) {
            spec["pages"] = grid->canvasPages(breakpoint);
        }
        layouts[breakpointToString(breakpoint)] = spec;
    }
    return layouts;
}

QJsonObject layoutSchema() {
    QJsonObject cells{
        {"type", "object"},
        {"properties", QJsonObject{{"col", QJsonObject{{"type", "number"}}},
                                   {"row", QJsonObject{{"type", "number"}}},
                                   {"cols", QJsonObject{{"type", "number"}}},
                                   {"rows", QJsonObject{{"type", "number"}}}}},
        {"required", QJsonArray{"col", "row", "cols", "rows"}},
    };
    QJsonObject properties;
    for (const QString& name : breakpointNames()) {
        properties[name] = cells;
    }
    return QJsonObject{{"properties", properties}, {"additionalProperties", false}};
}

QString layoutDescription() {
    return QStringLiteral(
        "Position and size in grid cells per screen-size layout: {\"large\": {col, row, cols, "
        "rows}, \"medium\": ..., \"small\": ...}. Columns span the canvas width; rows count "
        "per page (one viewport height), see get_workspace's grid. A layout left out keeps its "
        "current placement (add_widget: the default size in the first free spot). large is "
        "the desktop window, small a phone.");
}

// Applies every layout named in `layout`; stops at the first bad one.
bool applyLayout(DashboardGrid* grid, const QString& itemId, const QJsonObject& layout,
                 QString* error) {
    for (auto it = layout.constBegin(); it != layout.constEnd(); ++it) {
        const DashboardBreakpoint breakpoint = breakpointFromString(it.key());
        QRectF geometry;
        if (!cellsToGeometry(it.value().toObject(), breakpoint, &geometry, error) ||
            !grid->setItemGeometry(itemId, breakpoint, geometry, error)) {
            return false;
        }
    }
    return true;
}

QByteArray toPng(QPixmap pixmap) {
    if (qMax(pixmap.width(), pixmap.height()) > kMaxScreenshotSide) {
        pixmap = pixmap.scaled(kMaxScreenshotSide, kMaxScreenshotSide, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, "PNG");
    return png;
}

// endMacro() keeps a macro even when nothing in it changed anything, and
// undoing a failed one would leave it behind as a redo step. So it's undone,
// then marked obsolete: QUndoStack::redo() deletes an obsolete command
// without running it.
void dropMacro(QUndoStack* stack, int indexBefore, bool rollBack) {
    if (stack->index() <= indexBefore) {
        return;
    }
    QUndoCommand* macro = const_cast<QUndoCommand*>(stack->command(stack->index() - 1));
    if (rollBack || macro->childCount() == 0) {
        stack->undo();
        macro->setObsolete(true);
        stack->redo();
    }
}

}  // namespace

void AgentAppTools::registerTools(MainWindow* window, ToolRegistry* registry) {
    registerStateTools(window, registry);
    registerWorkspaceTools(window, registry);
    registerWidgetTools(window, registry);
    registerDashboardViewTools(window, registry);
    registerProjectTools(window, registry);
    registerDeviceTools(window, registry);
    registerDeviceIoTools(window, registry);
    registerOtaTools(window, registry);
    registerAppearanceTools(window, registry);
    registerSettingsTools(window, registry);
    registerDiagnosticsTools(window, registry);
}

bool AgentAppTools::confirm(MainWindow* window, const QString& title, const QString& text) {
    return DialogPresenter::question(window, title, text, QMessageBox::Yes | QMessageBox::No,
                                     QMessageBox::No) == QMessageBox::Yes;
}

bool AgentAppTools::allowHardware(MainWindow* window, const QString& what) {
    if (window->m_agentHardwareAllowed) {
        return true;
    }
    window->m_agentHardwareAllowed = confirm(
        window, QCoreApplication::translate("AgentAppTools", "AI Assistant"),
        QCoreApplication::translate(
            "AgentAppTools",
            "An AI assistant wants to %1.\n\nAllow it to connect devices and send them commands "
            "until AI assistant access is turned off or TraceView closes?")
            .arg(what));
    return window->m_agentHardwareAllowed;
}

void AgentAppTools::registerStateTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool state;
    state.name = "get_app_state";
    state.title = "Get app state";
    state.readOnly = true;
    state.description =
        "Overview of the running TraceView: version, open project file, workspaces (the "
        "active one is the dashboard on screen), devices with their connection state, and the "
        "screen-size layout being shown. Call this first.";
    state.handler = [window](const QJsonObject&, const AgentToolReply& reply) {
        const WorkspaceManager& workspaces = WorkspaceManager::instance();
        QJsonArray workspaceList;
        for (const Workspace& workspace : workspaces.workspaces()) {
            const bool active = workspace.id == workspaces.activeId();
            const int widgets = active ? window->m_dashboardGrid->items().size()
                                       : workspace.dashboard.value("items").toArray().size();
            workspaceList.append(QJsonObject{{"id", workspace.id},
                                             {"name", workspace.name},
                                             {"icon", workspace.icon},
                                             {"active", active},
                                             {"widgetCount", widgets}});
        }
        QJsonArray devices;
        for (const Device& device : window->m_devicesGrid->devices()) {
            DeviceConnection* connection = window->m_deviceConnections.value(device.id);
            devices.append(QJsonObject{
                {"id", device.id},
                {"name", device.name},
                {"transport", transportTypeLabel(device.transportType)},
                {"connected", connection && connection->isConnected()},
            });
        }
        const QString path = ProjectStore::instance().currentPath();
        reply(AgentToolResult::json(QJsonObject{
            {"version", QCoreApplication::applicationVersion()},
            {"projectFile", DashboardGallery::isBuiltInExample(path) ? QString() : path},
            {"activeWorkspaceId", workspaces.activeId()},
            {"workspaces", workspaceList},
            {"devices", devices},
            {"layout", breakpointToString(window->m_dashboardGrid->currentBreakpoint())},
            {"editMode", window->m_dashboardGrid->editMode()},
        }));
    };
    registry->registerTool(state);

    AgentTool signals_;
    signals_.name = "list_signals";
    signals_.title = "List signals";
    signals_.readOnly = true;
    signals_.description =
        "The telemetry each device publishes, from its manifest: topics (sourceId, topicId, "
        "name) and their fields (fieldId, name, type, unit, declared range). These ids are "
        "what widget configs bind to. A device that never connected has no topics yet.";
    signals_.inputSchema =
        AgentSchema().string("deviceId", "Only this device (from get_app_state).").toJson();
    signals_.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString only = args.value("deviceId").toString();
        QJsonArray devices;
        for (const Device& device : window->m_devicesGrid->devices()) {
            if (!only.isEmpty() && device.id != only) {
                continue;
            }
            DeviceConnection* connection = window->m_deviceConnections.value(device.id);
            QJsonArray topics;
            if (connection && connection->backend()) {
                for (const CatalogTopicInfo& topic : connection->backend()->catalogTopics()) {
                    QJsonArray fields;
                    for (const CatalogTopicField& field : topic.fields) {
                        QJsonObject entry{{"fieldId", field.fieldId},
                                          {"name", field.name},
                                          {"type", field.type},
                                          {"unit", field.unit}};
                        if (!std::isnan(field.minValue)) {
                            entry["min"] = field.minValue;
                        }
                        if (!std::isnan(field.maxValue)) {
                            entry["max"] = field.maxValue;
                        }
                        fields.append(entry);
                    }
                    topics.append(QJsonObject{{"sourceId", formatHexId(topic.sourceId, 8)},
                                              {"topicId", formatHexId(topic.topicId, 4)},
                                              {"name", topic.name},
                                              {"fields", fields}});
                }
            }
            devices.append(QJsonObject{
                {"deviceId", device.id},
                {"name", device.name},
                {"connected", connection && connection->isConnected()},
                {"selfSourceId", formatHexId(window->deviceSelfSourceId(device), 8)},
                {"topics", topics},
            });
        }
        if (!only.isEmpty() && devices.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(only)));
            return;
        }
        reply(AgentToolResult::json(QJsonObject{{"devices", devices}}));
    };
    registry->registerTool(signals_);

    AgentTool readSignal;
    readSignal.name = "read_signal";
    readSignal.title = "Read signal";
    readSignal.readOnly = true;
    readSignal.description =
        "Recent samples of one telemetry field, with min/max/mean/last over everything kept. "
        "Only fields some widget plots (in any workspace) are recorded: add a chart bound to "
        "the field first if this says it isn't. Times are seconds since the first sample "
        "returned.";
    readSignal.inputSchema =
        AgentSchema()
            .string("deviceId", "Device id (get_app_state).", true)
            .string("sourceId", "Hex text, e.g. \"0x00000001\" (list_signals).", true)
            .string("topicId", "Hex text, e.g. \"0x0101\" (list_signals).", true)
            .integer("fieldId", "Field id (list_signals).", true)
            .integer("maxPoints", "Samples to return, evenly spread (default 200, max 2000).")
            .toJson();
    readSignal.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        TelemetryHistoryKey key;
        key.deviceId = args.value("deviceId").toString();
        key.sourceId = quint32(args.value("sourceId").toString().toULongLong(nullptr, 0));
        key.topicId = quint16(args.value("topicId").toString().toUInt(nullptr, 0));
        key.fieldId = quint16(args.value("fieldId").toInt());
        const TelemetrySeriesBuffer* buffer = window->m_telemetryHistory.buffer(key);
        if (!buffer) {
            reply(AgentToolResult::error(QStringLiteral(
                "That field isn't being recorded: no widget plots it. Add a chart bound to it "
                "(add_widget), wait for data, then read again.")));
            return;
        }
        const QVector<TelemetrySeriesBuffer::Sample>& samples = buffer->samples();
        if (samples.isEmpty()) {
            reply(AgentToolResult::json(QJsonObject{{"count", 0}}));
            return;
        }
        double minimum = samples.first().value;
        double maximum = minimum;
        double sum = 0.0;
        for (const TelemetrySeriesBuffer::Sample& sample : samples) {
            minimum = qMin(minimum, sample.value);
            maximum = qMax(maximum, sample.value);
            sum += sample.value;
        }
        const int wanted = qBound(
            1, args.contains("maxPoints") ? args.value("maxPoints").toInt() : kDefaultSignalPoints,
            kMaxSignalPoints);
        const int count = samples.size();
        const int points = qMin(wanted, count);
        QJsonArray series;
        int first = -1;
        for (int i = 0; i < points; ++i) {
            const int index = points == 1 ? count - 1 : int(qint64(i) * (count - 1) / (points - 1));
            if (first < 0) {
                first = index;
            }
            const double t = double(samples[index].timestampUs - samples[first].timestampUs) / 1e6;
            series.append(QJsonArray{std::round(t * 1000.0) / 1000.0, samples[index].value});
        }
        reply(AgentToolResult::json(QJsonObject{
            {"count", count},
            {"spanSeconds", double(samples.last().timestampUs - samples.first().timestampUs) / 1e6},
            {"min", minimum},
            {"max", maximum},
            {"mean", sum / count},
            {"last", samples.last().value},
            {"samples", series},
        }));
    };
    registry->registerTool(readSignal);

    AgentTool screenshot;
    screenshot.name = "screenshot";
    screenshot.title = "Screenshot";
    screenshot.readOnly = true;
    screenshot.description =
        "A PNG of what TraceView is showing, to check a layout or chart after editing it. "
        "target: \"dashboard\" (default, the whole canvas of the active workspace), "
        "\"window\" (everything, panels included) or a widget id (just that widget).";
    screenshot.inputSchema =
        AgentSchema().string("target", "\"dashboard\", \"window\" or a widget id.").toJson();
    screenshot.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString target = args.value("target").toString("dashboard");
        QPixmap pixmap;
        if (target == QLatin1String("window")) {
            pixmap = window->grab();
        } else if (target == QLatin1String("dashboard")) {
            pixmap = window->m_dashboardGrid->grab();
        } else {
            pixmap = window->m_dashboardGrid->grabItem(target);
            if (pixmap.isNull()) {
                reply(AgentToolResult::error(
                    QStringLiteral("No widget \"%1\" in the active workspace.").arg(target)));
                return;
            }
        }
        reply(AgentToolResult::image(toPng(pixmap)));
    };
    registry->registerTool(screenshot);
}

void AgentAppTools::registerWorkspaceTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool get;
    get.name = "get_workspace";
    get.title = "Get workspace";
    get.readOnly = true;
    get.description =
        "A workspace's widgets with their full config and their layouts in grid cells, plus "
        "the grid itself (columns and rows per page of each layout). Defaults to the active "
        "workspace.";
    get.inputSchema = AgentSchema().string("workspaceId", "Workspace id (get_app_state).").toJson();
    get.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const WorkspaceManager& workspaces = WorkspaceManager::instance();
        const QString id = args.value("workspaceId").toString(workspaces.activeId());
        const QString name = workspaces.nameFor(id);
        if (name.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Unknown workspace \"%1\".").arg(id)));
            return;
        }
        const bool active = id == workspaces.activeId();
        QJsonArray widgets;
        if (active) {
            for (const DashboardItem& item : window->m_dashboardGrid->items()) {
                widgets.append(widgetToJson(item));
            }
        } else {
            for (const QJsonValue& value : workspaces.dashboardFor(id).value("items").toArray()) {
                bool ok = false;
                const DashboardItem item = dashboardItemFromJson(value.toObject(), &ok);
                if (ok) {
                    widgets.append(widgetToJson(item));
                }
            }
        }
        reply(AgentToolResult::json(QJsonObject{
            {"id", id},
            {"name", name},
            {"active", active},
            {"grid", gridJson(active ? window->m_dashboardGrid : nullptr)},
            {"widgets", widgets},
        }));
    };
    registry->registerTool(get);

    AgentTool create;
    create.name = "create_workspace";
    create.title = "Create workspace";
    create.description =
        "Adds an empty workspace and makes it the active one, so add_widget fills it next. "
        "The name is made unique if taken. Returns its id.";
    create.inputSchema =
        AgentSchema()
            .string("name", "Workspace name.", true)
            .string("icon", "Icon id such as \"lucide:gauge\" (any Lucide icon name).")
            .toJson();
    create.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString().trimmed();
        if (name.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("The name can't be blank.")));
            return;
        }
        WorkspaceManager& workspaces = WorkspaceManager::instance();
        const QString id = window->createWorkspace(name);
        if (args.contains("icon")) {
            workspaces.setIconFor(id, args.value("icon").toString());
            window->refreshWorkspaceSwitcher();
        }
        window->postStatus(QCoreApplication::translate("AgentAppTools",
                                                       "An AI assistant created workspace \"%1\".")
                               .arg(workspaces.nameFor(id)),
                           3000, StatusSeverity::Success);
        reply(AgentToolResult::json(QJsonObject{{"id", id}, {"name", workspaces.nameFor(id)}}));
    };
    registry->registerTool(create);

    AgentTool switchTo;
    switchTo.name = "switch_workspace";
    switchTo.title = "Switch workspace";
    switchTo.description =
        "Makes a workspace the active one (shown on screen); widget tools act on it. Its "
        "dashboard undo history starts over.";
    switchTo.inputSchema =
        AgentSchema().string("workspaceId", "Workspace id (get_app_state).", true).toJson();
    switchTo.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("workspaceId").toString();
        if (WorkspaceManager::instance().nameFor(id).isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Unknown workspace \"%1\".").arg(id)));
            return;
        }
        window->switchToWorkspace(id);
        reply(AgentToolResult::text(QStringLiteral("Workspace \"%1\" is active.")
                                        .arg(WorkspaceManager::instance().nameFor(id))));
    };
    registry->registerTool(switchTo);

    AgentTool update;
    update.name = "update_workspace";
    update.title = "Update workspace";
    update.description = "Renames a workspace and/or changes its icon.";
    update.inputSchema =
        AgentSchema()
            .string("workspaceId", "Workspace id (get_app_state).", true)
            .string("name", "New name (made unique if taken).")
            .string("icon", "Icon id such as \"lucide:gauge\"; empty for the default.")
            .toJson();
    update.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        WorkspaceManager& workspaces = WorkspaceManager::instance();
        const QString id = args.value("workspaceId").toString();
        if (workspaces.nameFor(id).isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Unknown workspace \"%1\".").arg(id)));
            return;
        }
        if (args.contains("name") &&
            workspaces.renameWorkspace(id, args.value("name").toString()).isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("The name can't be blank.")));
            return;
        }
        if (args.contains("icon")) {
            workspaces.setIconFor(id, args.value("icon").toString());
        }
        window->refreshWorkspaceSwitcher();
        reply(AgentToolResult::json(QJsonObject{
            {"id", id}, {"name", workspaces.nameFor(id)}, {"icon", workspaces.iconFor(id)}}));
    };
    registry->registerTool(update);

    AgentTool remove;
    remove.name = "delete_workspace";
    remove.title = "Delete workspace";
    remove.destructive = true;
    remove.description =
        "Deletes a workspace and its widgets after the user confirms in a dialog; this can't "
        "be undone. The last remaining workspace can't be deleted.";
    remove.inputSchema =
        AgentSchema().string("workspaceId", "Workspace id (get_app_state).", true).toJson();
    remove.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        WorkspaceManager& workspaces = WorkspaceManager::instance();
        const QString id = args.value("workspaceId").toString();
        const QString name = workspaces.nameFor(id);
        if (name.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("Unknown workspace \"%1\".").arg(id)));
            return;
        }
        if (workspaces.workspaces().size() <= 1) {
            reply(AgentToolResult::error(QStringLiteral("The last workspace can't be deleted.")));
            return;
        }
        if (DialogPresenter::question(
                window, QCoreApplication::translate("AgentAppTools", "Delete Workspace"),
                QCoreApplication::translate(
                    "AgentAppTools",
                    "An AI assistant wants to delete workspace \"%1\". This cannot be undone.")
                    .arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        window->deleteWorkspace(id);
        reply(AgentToolResult::text(QStringLiteral("Deleted workspace \"%1\".").arg(name)));
    };
    registry->registerTool(remove);
}

void AgentAppTools::registerWidgetTools(MainWindow* window, ToolRegistry* registry) {
    QStringList typeIds;
    for (const WidgetTypeInfo& info : WidgetRegistry::instance().availableTypes()) {
        typeIds.append(info.typeId);
    }

    AgentTool types;
    types.name = "list_widget_types";
    types.title = "List widget types";
    types.readOnly = true;
    types.description =
        "Every widget type add_widget can place, with what it shows and the exact shape of "
        "its config.";
    types.handler = [](const QJsonObject&, const AgentToolReply& reply) {
        QJsonArray list;
        for (const WidgetTypeInfo& info : WidgetRegistry::instance().availableTypes()) {
            list.append(QJsonObject{{"type", info.typeId},
                                    {"name", info.displayName},
                                    {"config", widgetAgentHelp(info.typeId)}});
        }
        reply(AgentToolResult::json(QJsonObject{{"types", list}}));
    };
    registry->registerTool(types);

    AgentTool add;
    add.name = "add_widget";
    add.title = "Add widget";
    add.description =
        "Places a widget on the active workspace's dashboard (one undo step). Get the config "
        "shape from list_widget_types and the binding ids from list_signals. Returns the "
        "widget as get_workspace lists it.";
    add.inputSchema = AgentSchema()
                          .string("type", "Widget type (list_widget_types).", true, typeIds)
                          .string("name", "Title shown on the widget; empty for the type's.")
                          .string("key", "Optional unique handle for the widget.")
                          .object("config", "Type-specific config (list_widget_types).")
                          .object("layout", layoutDescription(), false, layoutSchema())
                          .toJson();
    add.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        DashboardGrid* grid = window->m_dashboardGrid;
        DashboardItem item;
        item.typeId = args.value("type").toString();
        item.name = args.value("name").toString();
        item.key = args.value("key").toString();
        item.config = args.value("config").toObject();
        QString error;
        QUndoStack* stack = grid->undoStack();
        const int indexBefore = stack->index();
        stack->beginMacro(
            QCoreApplication::translate("AgentAppTools", "Add Widget (AI assistant)"));
        const QString id = grid->insertItem(item, &error);
        const bool placed =
            !id.isEmpty() && applyLayout(grid, id, args.value("layout").toObject(), &error);
        stack->endMacro();
        dropMacro(stack, indexBefore, !placed);
        if (!placed) {
            reply(AgentToolResult::error(error));
            return;
        }
        reply(AgentToolResult::json(widgetToJson(*grid->findItem(id))));
    };
    registry->registerTool(add);

    AgentTool update;
    update.name = "update_widget";
    update.title = "Update widget";
    update.description =
        "Changes a widget on the active workspace (one undo step): name, key, type, config "
        "and/or layout. config is merged into the current one key by key unless replaceConfig "
        "is true.";
    update.inputSchema =
        AgentSchema()
            .string("widgetId", "Widget id (get_workspace).", true)
            .string("name", "New title; empty for the type's default.")
            .string("key", "New key; empty to clear it.")
            .string("type", "New widget type; keeps position, size and config.", false, typeIds)
            .object("config", "Config keys to set (list_widget_types).")
            .boolean("replaceConfig", "Replace the whole config instead of merging.")
            .object("layout", layoutDescription(), false, layoutSchema())
            .toJson();
    update.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        DashboardGrid* grid = window->m_dashboardGrid;
        const QString id = args.value("widgetId").toString();
        const DashboardItem* item = grid->findItem(id);
        if (!item) {
            reply(AgentToolResult::error(
                QStringLiteral("No widget \"%1\" in the active workspace.").arg(id)));
            return;
        }
        QJsonObject config = args.value("config").toObject();
        if (!args.value("replaceConfig").toBool()) {
            QJsonObject merged = item->config;
            for (auto it = config.constBegin(); it != config.constEnd(); ++it) {
                merged[it.key()] = it.value();
            }
            config = merged;
        }

        QString error;
        QUndoStack* stack = grid->undoStack();
        const int indexBefore = stack->index();
        stack->beginMacro(
            QCoreApplication::translate("AgentAppTools", "Edit Widget (AI assistant)"));
        bool ok = true;
        if (args.contains("type")) {
            ok = grid->changeItemType(id, args.value("type").toString());
        }
        if (ok && args.contains("config")) {
            ok = grid->setItemConfig(id, config);
        }
        if (ok && args.contains("name")) {
            ok = grid->renameItem(id, args.value("name").toString());
        }
        if (ok && args.contains("key")) {
            ok = grid->setItemKey(id, args.value("key").toString());
            if (!ok) {
                error = QStringLiteral("Another widget already uses that key.");
            }
        }
        if (ok && args.contains("layout")) {
            ok = applyLayout(grid, id, args.value("layout").toObject(), &error);
        }
        stack->endMacro();
        dropMacro(stack, indexBefore, !ok);
        if (!ok) {
            reply(AgentToolResult::error(error.isEmpty() ? QStringLiteral("Rejected.") : error));
            return;
        }
        reply(AgentToolResult::json(widgetToJson(*grid->findItem(id))));
    };
    registry->registerTool(update);

    AgentTool remove;
    remove.name = "remove_widgets";
    remove.title = "Remove widgets";
    remove.description =
        "Removes widgets from the active workspace as one undo step (undo brings them back).";
    remove.inputSchema = AgentSchema()
                             .array("widgetIds", "Widget ids (get_workspace).",
                                    QJsonObject{{"type", "string"}}, true)
                             .toJson();
    remove.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        QStringList ids;
        for (const QJsonValue& value : args.value("widgetIds").toArray()) {
            ids.append(value.toString());
        }
        const int removed = window->m_dashboardGrid->removeItems(ids);
        if (removed == 0) {
            reply(AgentToolResult::error(
                QStringLiteral("None of those widgets is on the active workspace.")));
            return;
        }
        reply(AgentToolResult::text(QStringLiteral("Removed %1 widget(s).").arg(removed)));
    };
    registry->registerTool(remove);

    const auto historyTool = [window](bool undo) {
        AgentTool tool;
        tool.name = undo ? "undo" : "redo";
        tool.title = undo ? "Undo" : "Redo";
        tool.description =
            undo ? "Undoes the last edits, the user's or yours (each tool call is one step): "
                   "target \"dashboard\" (default) is the active workspace's widgets, "
                   "\"devices\" the device list. Returns what was undone."
                 : "Redoes edits undo took back, on the same targets.";
        tool.inputSchema = AgentSchema()
                               .integer("steps", "How many steps (default 1).")
                               .string("target", "Which history (default dashboard).", false,
                                       {"dashboard", "devices"})
                               .toJson();
        tool.handler = [window, undo](const QJsonObject& args, const AgentToolReply& reply) {
            QUndoStack* stack = args.value("target").toString() == QLatin1String("devices")
                                    ? window->m_devicesGrid->undoStack()
                                    : window->m_dashboardGrid->undoStack();
            const int steps = qMax(1, args.contains("steps") ? args.value("steps").toInt() : 1);
            QJsonArray done;
            for (int i = 0; i < steps && (undo ? stack->canUndo() : stack->canRedo()); ++i) {
                done.append(undo ? stack->undoText() : stack->redoText());
                undo ? stack->undo() : stack->redo();
            }
            reply(AgentToolResult::json(QJsonObject{{undo ? "undone" : "redone", done}}));
        };
        return tool;
    };
    registry->registerTool(historyTool(true));
    registry->registerTool(historyTool(false));
}

void AgentAppTools::registerDashboardViewTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool layout;
    layout.name = "set_layout";
    layout.title = "Set layout";
    layout.description =
        "Shows (and makes editable) one screen-size layout of the dashboard: large (desktop), "
        "medium (tablet) or small (phone). In User mode the layout follows the window size, so "
        "it can switch back on its own.";
    layout.inputSchema =
        AgentSchema().string("layout", "Layout to show.", true, breakpointNames()).toJson();
    layout.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        window->onScreenSizeBreakpointSelected(
            breakpointFromString(args.value("layout").toString()));
        reply(AgentToolResult::text(
            QStringLiteral("Showing the %1 layout.")
                .arg(breakpointToString(window->m_dashboardGrid->currentBreakpoint()))));
    };
    registry->registerTool(layout);

    AgentTool editMode;
    editMode.name = "set_edit_mode";
    editMode.title = "Set edit mode";
    editMode.description =
        "Turns the dashboard's edit mode (the padlock: drag/resize handles, properties panel) "
        "on or off for the user. Only available while TraceView is in Developer mode; the "
        "widget tools work either way.";
    editMode.inputSchema = AgentSchema().boolean("enabled", "On or off.", true).toJson();
    editMode.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        if (!window->developerUiActive()) {
            reply(AgentToolResult::error(QStringLiteral(
                "Edit mode needs Developer mode; the user has to log in (Access menu).")));
            return;
        }
        window->m_editModeButton->setChecked(args.value("enabled").toBool());
        reply(AgentToolResult::text(window->m_editModeEnabled ? QStringLiteral("Edit mode on.")
                                                              : QStringLiteral("Edit mode off.")));
    };
    registry->registerTool(editMode);

    AgentTool arrange;
    arrange.name = "arrange_widget";
    arrange.title = "Arrange widget";
    arrange.description =
        "Moves a widget in the stacking order of overlapping widgets (one undo step).";
    arrange.inputSchema =
        AgentSchema()
            .string("widgetId", "Widget id (get_workspace).", true)
            .string("order", "Where to move it.", true, {"front", "back", "forward", "backward"})
            .toJson();
    arrange.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        DashboardGrid* grid = window->m_dashboardGrid;
        const QString id = args.value("widgetId").toString();
        if (!grid->findItem(id)) {
            reply(AgentToolResult::error(
                QStringLiteral("No widget \"%1\" in the active workspace.").arg(id)));
            return;
        }
        grid->selectItem(id);
        const QString order = args.value("order").toString();
        if (order == QLatin1String("front")) {
            grid->bringSelectedToFront();
        } else if (order == QLatin1String("back")) {
            grid->sendSelectedToBack();
        } else if (order == QLatin1String("forward")) {
            grid->bringSelectedForward();
        } else {
            grid->sendSelectedBackward();
        }
        reply(AgentToolResult::text(QStringLiteral("Done.")));
    };
    registry->registerTool(arrange);

    const auto groupTool = [window](bool group) {
        AgentTool tool;
        tool.name = group ? "group_widgets" : "ungroup_widgets";
        tool.title = group ? "Group widgets" : "Ungroup widgets";
        tool.description = group ? "Welds two or more widgets together so they select and move "
                                   "as one unit (one undo step)."
                                 : "Releases widgets from their groups (one undo step).";
        tool.inputSchema = AgentSchema()
                               .array("widgetIds", "Widget ids (get_workspace).",
                                      QJsonObject{{"type", "string"}}, true)
                               .toJson();
        tool.handler = [window, group](const QJsonObject& args, const AgentToolReply& reply) {
            DashboardGrid* grid = window->m_dashboardGrid;
            QSet<QString> ids;
            for (const QJsonValue& value : args.value("widgetIds").toArray()) {
                if (grid->findItem(value.toString())) {
                    ids.insert(value.toString());
                }
            }
            if (ids.size() < (group ? 2 : 1)) {
                reply(AgentToolResult::error(
                    group ? QStringLiteral("Grouping needs at least two widgets of this workspace.")
                          : QStringLiteral("None of those widgets is on the active workspace.")));
                return;
            }
            grid->selectItems(ids, false);
            group ? grid->groupSelected() : grid->ungroupSelected();
            reply(AgentToolResult::text(QStringLiteral("Done.")));
        };
        return tool;
    };
    registry->registerTool(groupTool(true));
    registry->registerTool(groupTool(false));

    AgentTool select;
    select.name = "select_widget";
    select.title = "Select widget";
    select.description =
        "Selects a widget on screen, to point the user at it (the properties panel shows it "
        "in edit mode). An empty widgetId clears the selection.";
    select.inputSchema = AgentSchema()
                             .string("widgetId", "Widget id (get_workspace); empty to clear.", true)
                             .toJson();
    select.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("widgetId").toString();
        if (!id.isEmpty() && !window->m_dashboardGrid->findItem(id)) {
            reply(AgentToolResult::error(
                QStringLiteral("No widget \"%1\" in the active workspace.").arg(id)));
            return;
        }
        window->m_dashboardGrid->selectItem(id);
        reply(AgentToolResult::text(QStringLiteral("Done.")));
    };
    registry->registerTool(select);

    AgentTool canvas;
    canvas.name = "resize_canvas";
    canvas.title = "Resize canvas";
    canvas.description =
        "Grows (positive steps) or shrinks (negative) the height of the small or medium "
        "layout's canvas, adding pages below for more widgets; it can't shrink past the lowest "
        "widget. Acts on the layout being shown (set_layout); the large layout always matches "
        "the window.";
    canvas.inputSchema =
        AgentSchema().integer("steps", "Steps to grow (<0 shrinks).", true).toJson();
    canvas.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        DashboardGrid* grid = window->m_dashboardGrid;
        const DashboardBreakpoint breakpoint = grid->currentBreakpoint();
        if (!isPreviewBreakpoint(breakpoint)) {
            reply(AgentToolResult::error(
                QStringLiteral("The large layout's canvas matches the window; set_layout small or "
                               "medium first.")));
            return;
        }
        const int steps = args.value("steps").toInt();
        for (int i = 0; i < qAbs(steps); ++i) {
            steps > 0 ? grid->growCanvasHeight() : grid->shrinkCanvasHeight();
        }
        window->applyBreakpointViewport();
        window->updateCanvasHeightButtons();
        reply(AgentToolResult::json(QJsonObject{{"layout", breakpointToString(breakpoint)},
                                                {"pages", grid->canvasPages(breakpoint)}}));
    };
    registry->registerTool(canvas);

    AgentTool show;
    show.name = "show_view";
    show.title = "Show view";
    show.description =
        "Brings a part of TraceView to the front for the user (and for a following window "
        "screenshot): the dashboard, the devices page, the subscriptions view, the OTA page, "
        "the BTP traffic monitor, the notification history or the settings.";
    show.inputSchema = AgentSchema()
                           .string("view", "What to show.", true,
                                   {"dashboard", "devices", "subscriptions", "ota", "btp_monitor",
                                    "notifications", "settings"})
                           .toJson();
    show.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString view = args.value("view").toString();
        if (view == QLatin1String("devices") && window->m_devicesTabIndex < 0) {
            reply(AgentToolResult::error(QStringLiteral("The devices page needs Developer mode.")));
            return;
        }
        // Answered first: settings and the notification history are dialogs
        // that would hold the reply until the user closes them.
        reply(AgentToolResult::text(QStringLiteral("Showing %1.").arg(view)));
        QTimer::singleShot(0, window, [window, view] {
            if (view == QLatin1String("dashboard")) {
                window->switchToWorkspace(WorkspaceManager::instance().activeId());
                window->m_ribbon->setCurrentIndex(window->m_dashboardTabIndex);
                window->onRibbonTabChanged(window->m_dashboardTabIndex);
            } else if (view == QLatin1String("devices")) {
                window->m_ribbon->setCurrentIndex(window->m_devicesTabIndex);
                window->onRibbonTabChanged(window->m_devicesTabIndex);
            } else if (view == QLatin1String("subscriptions")) {
                window->switchToWorkspace(QString::fromLatin1(kSubscriptionsWorkspaceId));
            } else if (view == QLatin1String("ota")) {
                window->onOpenOtaTab();
            } else if (view == QLatin1String("btp_monitor")) {
                window->onOpenBtpMonitor();
            } else if (view == QLatin1String("notifications")) {
                window->onShowNotificationHistory();
            } else {
                window->onOpenSettings();
            }
        });
    };
    registry->registerTool(show);
}

void AgentAppTools::registerProjectTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool list;
    list.name = "list_projects";
    list.title = "List projects";
    list.readOnly = true;
    list.description =
        "The open project's file, File > Open Recent's list, and the dashboard gallery "
        "(projects kept inside TraceView; the default one opens at startup).";
    list.handler = [](const QJsonObject&, const AgentToolReply& reply) {
        const QString path = ProjectStore::instance().currentPath();
        reply(AgentToolResult::json(QJsonObject{
            {"current", DashboardGallery::isBuiltInExample(path)
                            ? QStringLiteral("(built-in example)")
                            : path},
            {"currentGalleryName", DashboardGallery::nameForPath(path)},
            {"recent", QJsonArray::fromStringList(MainWindow::recentProjectFiles())},
            {"gallery", QJsonArray::fromStringList(DashboardGallery::names())},
            {"galleryDefault", DashboardGallery::defaultName()},
        }));
    };
    registry->registerTool(list);

    AgentTool create;
    create.name = "new_project";
    create.title = "New project";
    create.destructive = true;
    create.description =
        "Starts a new, empty project (no devices, one empty workspace) after the user confirms; "
        "unsaved changes to the open one are lost, so save_project first if they matter.";
    create.handler = [window](const QJsonObject&, const AgentToolReply& reply) {
        if (!confirm(window, QCoreApplication::translate("AgentAppTools", "New Project"),
                     QCoreApplication::translate(
                         "AgentAppTools",
                         "An AI assistant wants to start a new, empty project. Unsaved changes "
                         "to the current one will be lost."))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        window->startNewProject();
        reply(AgentToolResult::text(QStringLiteral("Started a new project.")));
    };
    registry->registerTool(create);

    const auto openPath = [window](const QString& path, const QString& label,
                                   const AgentToolReply& reply) {
        if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Open Project"),
                     QCoreApplication::translate(
                         "AgentAppTools",
                         "An AI assistant wants to open \"%1\". Unsaved changes to the current "
                         "project will be lost.")
                         .arg(label))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        if (!window->loadProjectFile(path)) {
            reply(AgentToolResult::error(ProjectStore::instance().lastError()));
            return;
        }
        window->addRecentFile(path);
        reply(AgentToolResult::text(QStringLiteral("Opened %1.").arg(label)));
    };

    AgentTool open;
    open.name = "open_project";
    open.title = "Open project";
    open.description =
        "Opens a .tvproj file after the user confirms (unsaved changes to the open project are "
        "lost). Paths of recent projects come from list_projects.";
    open.inputSchema = AgentSchema().string("path", "Absolute path of the .tvproj.", true).toJson();
    open.handler = [openPath](const QJsonObject& args, const AgentToolReply& reply) {
        const QString path = args.value("path").toString();
        if (!QFileInfo(path).isFile()) {
            reply(AgentToolResult::error(QStringLiteral("No file at %1.").arg(path)));
            return;
        }
        openPath(path, QDir::toNativeSeparators(path), reply);
    };
    registry->registerTool(open);

    AgentTool openGallery;
    openGallery.name = "open_gallery_dashboard";
    openGallery.title = "Open gallery dashboard";
    openGallery.description =
        "Opens a project from the dashboard gallery by name (list_projects) after the user "
        "confirms. An empty name opens the built-in example.";
    openGallery.inputSchema = AgentSchema().string("name", "Gallery name.", true).toJson();
    openGallery.handler = [openPath](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString();
        if (name.isEmpty()) {
            openPath(DashboardGallery::kBuiltInExamplePath, QStringLiteral("the built-in example"),
                     reply);
            return;
        }
        if (!DashboardGallery::contains(name)) {
            reply(AgentToolResult::error(QStringLiteral("The gallery has no \"%1\".").arg(name)));
            return;
        }
        openPath(DashboardGallery::pathFor(name), name, reply);
    };
    registry->registerTool(openGallery);

    AgentTool addGallery;
    addGallery.name = "save_to_gallery";
    addGallery.title = "Save to gallery";
    addGallery.description =
        "Saves the open project into the dashboard gallery under a name (replacing an entry of "
        "that name asks the user first). makeDefault opens it at every startup.";
    addGallery.inputSchema = AgentSchema()
                                 .string("name", "Gallery name (must work as a file name).", true)
                                 .boolean("makeDefault", "Open it at startup.")
                                 .toJson();
    addGallery.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString().trimmed();
        QString error;
        if (!DashboardGallery::isValidName(name, &error)) {
            reply(AgentToolResult::error(error));
            return;
        }
        const QString current =
            DashboardGallery::nameForPath(ProjectStore::instance().currentPath());
        if (name != current && DashboardGallery::contains(name) &&
            !confirm(window, QCoreApplication::translate("AgentAppTools", "Add to Gallery"),
                     QCoreApplication::translate(
                         "AgentAppTools",
                         "An AI assistant wants to replace the gallery dashboard \"%1\".")
                         .arg(name))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        window->syncProjectSections();
        if (!ProjectStore::instance().saveAs(DashboardGallery::pathFor(name))) {
            reply(AgentToolResult::error(ProjectStore::instance().lastError()));
            return;
        }
        if (args.value("makeDefault").toBool() ||
            !DashboardGallery::contains(DashboardGallery::defaultName())) {
            DashboardGallery::setDefaultName(name);
        }
        reply(AgentToolResult::json(
            QJsonObject{{"name", name}, {"default", DashboardGallery::defaultName() == name}}));
    };
    registry->registerTool(addGallery);

    AgentTool galleryDefault;
    galleryDefault.name = "set_gallery_default";
    galleryDefault.title = "Set gallery default";
    galleryDefault.description =
        "Chooses which gallery dashboard opens at startup; an empty name goes back to the "
        "built-in example.";
    galleryDefault.inputSchema = AgentSchema().string("name", "Gallery name.", true).toJson();
    galleryDefault.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString();
        if (!name.isEmpty() && !DashboardGallery::contains(name)) {
            reply(AgentToolResult::error(QStringLiteral("The gallery has no \"%1\".").arg(name)));
            return;
        }
        DashboardGallery::setDefaultName(name);
        reply(AgentToolResult::text(QStringLiteral("Done.")));
    };
    registry->registerTool(galleryDefault);

    AgentTool removeGallery;
    removeGallery.name = "delete_gallery_dashboard";
    removeGallery.title = "Delete gallery dashboard";
    removeGallery.destructive = true;
    removeGallery.description =
        "Deletes a dashboard from the gallery after the user confirms; this can't be undone.";
    removeGallery.inputSchema = AgentSchema().string("name", "Gallery name.", true).toJson();
    removeGallery.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString();
        if (!DashboardGallery::contains(name)) {
            reply(AgentToolResult::error(QStringLiteral("The gallery has no \"%1\".").arg(name)));
            return;
        }
        if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Delete Dashboard"),
                     QCoreApplication::translate(
                         "AgentAppTools",
                         "An AI assistant wants to delete the gallery dashboard \"%1\". This "
                         "cannot be undone.")
                         .arg(name))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        QString error;
        if (!DashboardGallery::remove(name, &error)) {
            reply(AgentToolResult::error(error));
            return;
        }
        reply(AgentToolResult::text(QStringLiteral("Deleted \"%1\".").arg(name)));
    };
    registry->registerTool(removeGallery);

    AgentTool save;
    save.name = "save_project";
    save.title = "Save project";
    save.description =
        "Writes the project (devices and every workspace) to its .tvproj file. Without a path "
        "it saves over the open file; a project that has none yet needs an absolute path.";
    save.inputSchema =
        AgentSchema().string("path", "Absolute path of the .tvproj file to write.").toJson();
    save.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        ProjectStore& store = ProjectStore::instance();
        QString path = args.value("path").toString();
        if (path.isEmpty()) {
            path = store.currentPath();
            if (path.isEmpty() || DashboardGallery::isBuiltInExample(path)) {
                reply(AgentToolResult::error(
                    QStringLiteral("This project has no file yet: pass an absolute path.")));
                return;
            }
        } else if (!QDir::isAbsolutePath(path)) {
            reply(AgentToolResult::error(QStringLiteral("The path must be absolute.")));
            return;
        }
        if (!path.endsWith(QLatin1String(".tvproj"), Qt::CaseInsensitive)) {
            path += QLatin1String(".tvproj");
        }
        window->syncProjectSections();
        const bool ok = path == store.currentPath() ? store.save() : store.saveAs(path);
        if (!ok) {
            reply(AgentToolResult::error(store.lastError()));
            return;
        }
        window->addRecentFile(path);
        window->postStatus(
            QCoreApplication::translate("AgentAppTools", "An AI assistant saved the project to %1.")
                .arg(QDir::toNativeSeparators(path)),
            3000, StatusSeverity::Success);
        reply(AgentToolResult::text(QStringLiteral("Saved %1.").arg(path)));
    };
    registry->registerTool(save);
}

}  // namespace traceview
