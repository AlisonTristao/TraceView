// AgentAppTools, devices: the device list, links, connecting, commands and the
// robot's answers, device scripts, subscriptions and OTA (see agentapptools.h).

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>

#include "agent/toolregistry.h"
#include "backend/backend.h"
#include "core/agentapptools.h"
#include "core/deviceconnection.h"
#include "core/mainwindow.h"
#include "core/serialwidgetbridge.h"
#include "dashboard/widgetconfigeditor.h"
#include "devices/devicesgrid.h"
#include "devices/tracekitdialog.h"
#include "diagram/diagramscriptruntime.h"
#include "ota/otaclient.h"

#ifdef TRACEVIEW_ENABLE_USB_HID
#include "core/usbhidmanager.h"
#endif

namespace traceview {

namespace {

constexpr int kMaxReplyWaitMs = 10000;
constexpr int kDefaultTerminalBytes = 4000;
constexpr int kDefaultLogLines = 100;
constexpr int kMaxBleScanSeconds = 15;
constexpr int kOtaStatusTimeoutMs = 8000;
constexpr int kOtaUploadTimeoutMs = 10 * 60 * 1000;

// Fields deviceToJson() leaves out or only sometimes includes, kept from the
// stored device unless the caller sets them: live session state and the
// passwords (stored in the project only when their cache flag is on).
Device withLiveState(Device merged, const Device& stored, const QJsonObject& changes) {
    merged.connected = stored.connected;
    merged.btpVersion = stored.btpVersion;
    merged.btpId = stored.btpId;
    merged.reportedInfo = stored.reportedInfo;
    merged.peerOnline = stored.peerOnline;
    merged.peerPresenceKnown = stored.peerPresenceKnown;
    merged.peerBootId = stored.peerBootId;
    merged.peerRssi = stored.peerRssi;
    merged.peerRttMs = stored.peerRttMs;
    merged.activeLink = stored.activeLink;
    if (!changes.contains("peerPassword")) {
        merged.peerPassword = stored.peerPassword;
    }
    return merged;
}

// The device as an assistant sees it: its project JSON without the
// passwords, plus what the live connection knows.
QJsonObject deviceJson(const Device& device, DeviceConnection* connection,
                       const QStringList& children) {
    QJsonObject settings = deviceToJson(device);
    settings.remove("peerPassword");
    QJsonArray reported;
    for (const DeviceInfoRecord& record : device.reportedInfo) {
        reported.append(
            QJsonObject{{"key", record.key}, {"label", record.label}, {"value", record.value}});
    }
    return QJsonObject{
        {"id", device.id},
        {"settings", settings},
        {"peerPasswordSet", !device.peerPassword.isEmpty()},
        {"connected", connection && connection->isConnected()},
        {"connecting", connection && connection->wantsConnection() && !connection->isConnected()},
        {"transport", transportTypeLabel(device.transportType)},
        {"btpVersion", device.btpVersion},
        {"btpId", device.btpId},
        {"reportedInfo", reported},
        {"activeLink", device.activeLink},
        {"hubChildren", QJsonArray::fromStringList(children)},
        {"topicCount", connection && connection->backend()
                           ? int(connection->backend()->catalogTopics().size())
                           : 0},
    };
}

// Whether `changes` touch how the device is reached -- configuring a link
// makes TraceView dial it on its own (auto-reconnect), so it counts as
// connecting.
bool touchesLinks(const QJsonObject& changes) {
    static const QStringList kLinkKeys = {
        "transportType", "portName",   "baudRate",    "usbPath",        "tcpHost",
        "tcpPort",       "bleAddress", "blePeerUuid", "parentDeviceId", "peerSourceId",
        "robotName",     "autoTarget", "extraLinks"};
    for (const QString& key : kLinkKeys) {
        if (changes.contains(key)) {
            return true;
        }
    }
    return false;
}

QString settingsHelp() {
    return QStringLiteral(
        "Device settings, the same JSON as the project file: name, description, transportType "
        "(0 serial, 1 USB HID, 2 hub channel = a robot reached through a dongle, 3 TCP, 4 BLE), "
        "portName + baudRate + lineTerminator (0 none, 1 LF, 2 CR, 3 CRLF) for serial, usbPath "
        "for USB HID, tcpHost + tcpPort for TCP, bleAddress (advertised name or address) for "
        "BLE, parentDeviceId + peerSourceId for a hub channel, robotName + autoTarget to find "
        "the link by the robot's name, otaAddress (only when the recovery is not at the address "
        "the device reports or its TCP host), peerPassword for the device's key (password E; "
        "cachePeerPassword keeps it in the project), extraLinks (alternative links with the same transport fields, tried in "
        "order). list_ports gives the values to use.");
}

DeviceConnection* connectionFor(const QHash<QString, DeviceConnection*>& connections,
                                const QString& id, const AgentToolReply& reply) {
    DeviceConnection* connection = connections.value(id);
    if (!connection) {
        reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
    }
    return connection;
}

const Device* findDevice(const QVector<Device>& devices, const QString& id) {
    for (const Device& device : devices) {
        if (device.id == id) {
            return &device;
        }
    }
    return nullptr;
}

QJsonArray logJson(const QVector<DeviceFeedBuffer::LogLine>& lines) {
    QJsonArray list;
    for (const DeviceFeedBuffer::LogLine& line : lines) {
        list.append(QJsonObject{{"received", line.received.toString(Qt::ISODateWithMs)},
                                {"severity", line.severity},
                                {"message", line.message}});
    }
    return list;
}

}  // namespace

void AgentAppTools::registerDeviceTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool get;
    get.name = "get_device";
    get.title = "Get device";
    get.readOnly = true;
    get.description =
        "One device's full settings (project JSON, passwords left out), whether it is connected, "
        "what it reported about itself (firmware, chip...) and its hub children.";
    get.inputSchema = AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    get.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        reply(AgentToolResult::json(deviceJson(*device, window->m_deviceConnections.value(id),
                                               window->m_devicesGrid->childDeviceIds(id))));
    };
    registry->registerTool(get);

    AgentTool add;
    add.name = "add_device";
    add.title = "Add device";
    add.description =
        "Adds a device to the project (one undo step on the devices history). With a link "
        "configured TraceView starts dialing it, so the first time the user is asked to allow "
        "hardware access. Returns it as get_device does.";
    add.inputSchema = AgentSchema()
                          .string("name", "Device name.", true)
                          .object("settings", settingsHelp())
                          .toJson();
    add.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        Device base;
        base.name = args.value("name").toString();
        QJsonObject json = deviceToJson(base);
        const QJsonObject changes = args.value("settings").toObject();
        if (touchesLinks(changes) &&
            !allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                               "set up how a device is reached"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
            json[it.key()] = it.value();
        }
        json["name"] = base.name;
        // deviceFromJson() wants an id; cleared below so DevicesGrid::
        // addDevice() generates the real one.
        json["id"] = QStringLiteral("new");
        bool ok = false;
        Device device = deviceFromJson(json, &ok);
        if (!ok) {
            reply(
                AgentToolResult::error(QStringLiteral("Those settings don't describe a device.")));
            return;
        }
        device.id.clear();
        device.peerPassword = changes.value("peerPassword").toString();
        const QString id = window->m_devicesGrid->addDevice(device);
        const QVector<Device> devices = window->m_devicesGrid->devices();
        reply(AgentToolResult::json(
            deviceJson(*findDevice(devices, id), window->m_deviceConnections.value(id), {})));
    };
    registry->registerTool(add);

    AgentTool update;
    update.name = "update_device";
    update.title = "Update device";
    update.description =
        "Changes a device's settings: the keys given replace the current ones (one undo step "
        "on the devices history). Changing how it is reached starts over from its first link "
        "and, the first time, asks the user to allow hardware access.";
    update.inputSchema = AgentSchema()
                             .string("deviceId", "Device id (get_app_state).", true)
                             .object("settings", settingsHelp(), true)
                             .toJson();
    update.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* stored = findDevice(devices, id);
        if (!stored) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        const QJsonObject changes = args.value("settings").toObject();
        if (touchesLinks(changes) &&
            !allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                               "set up how a device is reached"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        QJsonObject json = deviceToJson(*stored);
        for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
            json[it.key()] = it.value();
        }
        json["id"] = id;
        bool ok = false;
        const Device parsed = deviceFromJson(json, &ok);
        if (!ok) {
            reply(
                AgentToolResult::error(QStringLiteral("Those settings don't describe a device.")));
            return;
        }
        window->m_devicesGrid->updateDevice(withLiveState(parsed, *stored, changes));
        const QVector<Device> after = window->m_devicesGrid->devices();
        reply(AgentToolResult::json(deviceJson(*findDevice(after, id),
                                               window->m_deviceConnections.value(id),
                                               window->m_devicesGrid->childDeviceIds(id))));
    };
    registry->registerTool(update);

    AgentTool remove;
    remove.name = "remove_device";
    remove.title = "Remove device";
    remove.description =
        "Removes a device from the project (undo with target \"devices\" brings it back). A "
        "hub device with robots riding on it can't be removed before them.";
    remove.inputSchema =
        AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    remove.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        if (!findDevice(devices, id)) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        const QStringList children = window->m_devicesGrid->childDeviceIds(id);
        if (!children.isEmpty()) {
            reply(AgentToolResult::error(
                QStringLiteral("Remove its hub children first: %1.").arg(children.join(", "))));
            return;
        }
        window->m_devicesGrid->removeDevice(id);
        reply(AgentToolResult::text(QStringLiteral("Removed.")));
    };
    registry->registerTool(remove);

    AgentTool connect;
    connect.name = "connect_device";
    connect.title = "Connect device";
    connect.description =
        "Connects (dialing its links in order) or disconnects a device. Connecting touches real "
        "hardware, so the first time the user is asked to allow it. Returns once connected, or "
        "after waitMs (default 5000) with the state so far.";
    connect.inputSchema = AgentSchema()
                              .string("deviceId", "Device id (get_app_state).", true)
                              .boolean("connected", "true connects, false disconnects.", true)
                              .integer("waitMs", "How long to wait for the link (max 10000).")
                              .toJson();
    connect.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        DeviceConnection* connection = connectionFor(window->m_deviceConnections, id, reply);
        if (!connection) {
            return;
        }
        const bool wanted = args.value("connected").toBool();
        if (wanted && !allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                                         "connect a device"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        if (connection->wantsConnection() != wanted) {
            window->onDeviceConnectToggleRequested(id);
        }
        if (!wanted || connection->isConnected()) {
            reply(AgentToolResult::json(QJsonObject{{"connected", connection->isConnected()}}));
            return;
        }
        // Answered on whichever comes first: the link coming up or the wait.
        // `waiter` scopes both, so neither outlives the answer.
        auto* waiter = new QObject(window);
        QPointer<DeviceConnection> guard(connection);
        auto done = std::make_shared<bool>(false);
        const auto finish = [waiter, guard, reply, done] {
            if (*done) {
                return;
            }
            *done = true;
            reply(AgentToolResult::json(QJsonObject{{"connected", guard && guard->isConnected()}}));
            waiter->deleteLater();
        };
        QObject::connect(connection, &DeviceConnection::connectionStateChanged, waiter,
                         [finish](bool up) {
                             if (up) {
                                 finish();
                             }
                         });
        const int waitMs = qBound(0, args.contains("waitMs") ? args.value("waitMs").toInt() : 5000,
                                  kMaxReplyWaitMs);
        QTimer::singleShot(waitMs, waiter, finish);
    };
    registry->registerTool(connect);

    AgentTool ports;
    ports.name = "list_ports";
    ports.title = "List ports";
    ports.readOnly = true;
    ports.description =
        "What devices can be reached through right now: serial ports (portName, with the USB "
        "product name a robot reports), USB HID devices (usbPath), robots each hub device sees "
        "(peerSourceId, for a hub channel), and, with bleScanSeconds > 0, BLE devices found by "
        "a scan of that length (max 15).";
    ports.inputSchema =
        AgentSchema()
            .integer("bleScanSeconds", "Scan for BLE devices this long (0 = no scan).")
            .toJson();
    ports.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        QJsonArray serial;
        for (const SerialPortOption& port : MainWindow::serialPortOptions()) {
            serial.append(QJsonObject{
                {"portName", port.name}, {"label", port.label}, {"productName", port.productName}});
        }
        QJsonArray usb;
#ifdef TRACEVIEW_ENABLE_USB_HID
        for (const UsbHidManager::DeviceInfo& device : UsbHidManager::availableDevices()) {
            usb.append(QJsonObject{{"usbPath", device.path}, {"label", device.label}});
        }
#endif
        QJsonArray hubs;
        for (const Device& device : window->m_devicesGrid->devices()) {
            DeviceConnection* connection = window->m_deviceConnections.value(device.id);
            if (!connection || !connection->isConnected() ||
                device.transportType == TransportType::HubChannel) {
                continue;
            }
            QJsonArray peers;
            for (const HubPeer& peer : window->hubPeersFor(device.id)) {
                peers.append(QJsonObject{{"peerSourceId", formatHexId(peer.sourceId, 8)},
                                         {"name", peer.name},
                                         {"channel", peer.channel},
                                         {"online", peer.online}});
            }
            if (!peers.isEmpty()) {
                hubs.append(QJsonObject{
                    {"parentDeviceId", device.id}, {"hubName", device.name}, {"robots", peers}});
            }
        }
        QJsonObject result{{"serial", serial}, {"usbHid", usb}, {"hubs", hubs}};
        const int scan = qBound(0, args.value("bleScanSeconds").toInt(), kMaxBleScanSeconds);
#ifdef TRACEVIEW_ENABLE_BLE
        if (scan > 0) {
            window->onBleScanToggled(true);
            QTimer::singleShot(scan * 1000, window, [window, result, reply]() mutable {
                QJsonArray ble;
                for (const auto& [name, address] : window->m_bleDiscoveredDevices) {
                    ble.append(QJsonObject{{"name", name}, {"bleAddress", address}});
                }
                window->onBleScanToggled(false);
                result["ble"] = ble;
                reply(AgentToolResult::json(result));
            });
            return;
        }
#else
        if (scan > 0) {
            result["ble"] = QStringLiteral("This build has no BLE support.");
        }
#endif
        reply(AgentToolResult::json(result));
    };
    registry->registerTool(ports);
}

void AgentAppTools::registerDeviceIoTools(MainWindow* window, ToolRegistry* registry) {
    // Sends through `send`, then answers with what the device said within
    // waitMs: the terminal output and log lines that arrived meanwhile.
    const auto sendAndCollect = [window](const QString& id, int waitMs,
                                         const std::function<void()>& send,
                                         const AgentToolReply& reply) {
        const qint64 mark = window->m_agentFeed.terminalReceived(id);
        const int logsBefore = window->m_agentFeed.logs(id).size();
        send();
        QTimer::singleShot(
            qBound(0, waitMs, kMaxReplyWaitMs), window, [window, id, mark, logsBefore, reply] {
                const QVector<DeviceFeedBuffer::LogLine> logs = window->m_agentFeed.logs(id);
                reply(AgentToolResult::json(QJsonObject{
                    {"sent", true},
                    {"terminal", window->m_agentFeed.terminalSince(id, mark)},
                    {"log", logJson(logs.mid(qMin(logsBefore, int(logs.size()))))},
                }));
            });
    };

    AgentTool command;
    command.name = "send_command";
    command.title = "Send command";
    command.description =
        "Sends a text command to a device exactly as a dashboard button would (a COMMAND "
        "request to the robot; on a dongle's serial port, the line plus its terminator into "
        "the dongle's terminal). Real hardware: the first "
        "time the user is asked to allow it. Returns the terminal output and log lines that "
        "arrived within waitMs (default 1000).";
    command.inputSchema = AgentSchema()
                              .string("deviceId", "Device id (get_app_state).", true)
                              .string("command", "The command text.", true)
                              .integer("waitMs", "How long to collect the answer (max 10000).")
                              .toJson();
    command.handler = [window, sendAndCollect](const QJsonObject& args,
                                               const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        DeviceConnection* connection = connectionFor(window->m_deviceConnections, id, reply);
        if (!connection) {
            return;
        }
        if (!connection->isConnected()) {
            reply(AgentToolResult::error(QStringLiteral("The device isn't connected.")));
            return;
        }
        if (!allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                               "send commands to a device"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        const QByteArray text = args.value("command").toString().toUtf8();
        QPointer<DeviceConnection> guard(connection);
        sendAndCollect(
            id, args.contains("waitMs") ? args.value("waitMs").toInt() : 1000,
            [guard, text] {
                if (guard) {
                    SerialWidgetBridge::sendControlCommand(guard, text);
                }
            },
            reply);
    };
    registry->registerTool(command);

    AgentTool terminal;
    terminal.name = "send_terminal";
    terminal.title = "Send to terminal";
    terminal.description =
        "Types text into a device's terminal (what the serial monitor's input does), with a "
        "newline unless newline is false. Same permission and answer as send_command.";
    terminal.inputSchema = AgentSchema()
                               .string("deviceId", "Device id (get_app_state).", true)
                               .string("text", "Text to send.", true)
                               .boolean("newline", "Append a newline (default true).")
                               .integer("waitMs", "How long to collect the answer (max 10000).")
                               .toJson();
    terminal.handler = [window, sendAndCollect](const QJsonObject& args,
                                                const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        DeviceConnection* connection = connectionFor(window->m_deviceConnections, id, reply);
        if (!connection) {
            return;
        }
        if (!connection->isConnected()) {
            reply(AgentToolResult::error(QStringLiteral("The device isn't connected.")));
            return;
        }
        if (!allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                               "send commands to a device"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        QByteArray bytes = args.value("text").toString().toUtf8();
        if (args.value("newline").toBool(true)) {
            bytes.append('\n');
        }
        QPointer<DeviceConnection> guard(connection);
        sendAndCollect(
            id, args.contains("waitMs") ? args.value("waitMs").toInt() : 1000,
            [guard, bytes] {
                if (guard && guard->backend()) {
                    guard->backend()->sendTerminalIn(bytes);
                }
            },
            reply);
    };
    registry->registerTool(terminal);

    AgentTool readTerminal;
    readTerminal.name = "read_terminal";
    readTerminal.title = "Read terminal";
    readTerminal.readOnly = true;
    readTerminal.description =
        "The latest text a device printed to its terminal (serial console), kept by TraceView "
        "whether or not a serial monitor is open.";
    readTerminal.inputSchema =
        AgentSchema()
            .string("deviceId", "Device id (get_app_state).", true)
            .integer("maxBytes", "How much of the end to return (default 4000).")
            .toJson();
    readTerminal.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        if (!connectionFor(window->m_deviceConnections, id, reply)) {
            return;
        }
        const int maxBytes =
            args.contains("maxBytes") ? args.value("maxBytes").toInt() : kDefaultTerminalBytes;
        reply(AgentToolResult::text(window->m_agentFeed.terminal(id, qMax(1, maxBytes))));
    };
    registry->registerTool(readTerminal);

    AgentTool readLog;
    readLog.name = "read_device_log";
    readLog.title = "Read device log";
    readLog.readOnly = true;
    readLog.description =
        "The latest log records a robot sent over BTP (what the robot log widget shows), "
        "oldest first, with their severity.";
    readLog.inputSchema = AgentSchema()
                              .string("deviceId", "Device id (get_app_state).", true)
                              .integer("maxLines", "How many of the latest (default 100).")
                              .toJson();
    readLog.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        if (!connectionFor(window->m_deviceConnections, id, reply)) {
            return;
        }
        const int maxLines =
            args.contains("maxLines") ? args.value("maxLines").toInt() : kDefaultLogLines;
        reply(AgentToolResult::json(
            QJsonObject{{"log", logJson(window->m_agentFeed.logs(id, qMax(1, maxLines)))}}));
    };
    registry->registerTool(readLog);

    AgentTool subscriptions;
    subscriptions.name = "get_subscriptions";
    subscriptions.title = "Get subscriptions";
    subscriptions.readOnly = true;
    subscriptions.description =
        "Per device, the telemetry topics TraceView subscribes to (requested vs granted rate, "
        "errors) and what the device reports publishing (rate, bytes, dropped samples). Rates "
        "in Hz.";
    subscriptions.inputSchema =
        AgentSchema().string("deviceId", "Only this device (get_app_state).").toJson();
    subscriptions.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString only = args.value("deviceId").toString();
        QJsonArray devices;
        for (const Device& device : window->m_devicesGrid->devices()) {
            if (!only.isEmpty() && device.id != only) {
                continue;
            }
            DeviceConnection* connection = window->m_deviceConnections.value(device.id);
            if (!connection || !connection->backend()) {
                continue;
            }
            Backend* backend = connection->backend();
            QJsonArray subs;
            for (const TopicSubscriptionState& state : backend->subscriptions()) {
                subs.append(QJsonObject{
                    {"sourceId", formatHexId(state.sourceId, 8)},
                    {"topicId", formatHexId(state.topicId, 4)},
                    {"widgets", state.subscriberCount},
                    {"requestedHz", state.requestedRateMillihz / 1000.0},
                    {"grantedHz", state.effectiveRateMillihz / 1000.0},
                    {"rateLimited", state.rateLimited()},
                    {"awaitingResult", state.awaitingResult},
                    {"lastErrorCode", state.lastErrorCode},
                });
            }
            QJsonArray published;
            for (const StatusTopicRecord& status : backend->topicStatuses()) {
                published.append(QJsonObject{
                    {"sourceId", formatHexId(status.sourceId, 8)},
                    {"topicId", formatHexId(status.topicId, 4)},
                    {"subscribers", status.subscriberCount},
                    {"rateHz", status.effectiveRateMillihz / 1000.0},
                    {"bytesTotal", double(status.bytesTotal)},
                    {"samplesDropped", double(status.samplesDroppedTotal)},
                });
            }
            devices.append(QJsonObject{{"deviceId", device.id},
                                       {"name", device.name},
                                       {"connected", connection->isConnected()},
                                       {"subscriptions", subs},
                                       {"published", published}});
        }
        reply(AgentToolResult::json(QJsonObject{{"devices", devices}}));
    };
    registry->registerTool(subscriptions);

    AgentTool getScript;
    getScript.name = "get_device_script";
    getScript.title = "Get device script";
    getScript.readOnly = true;
    getScript.description =
        "A device's JavaScript automation and its recent output (device.log() lines and "
        "errors). The script may define onTelemetry(sample) {topicId, fieldId, elementIndex, "
        "value, timestampUs}, onTerminal(text), onConnectionChange(connected), "
        "onStatus(text, severity), onDeviceInfo(info); it can call device.sendCommand(text), "
        "device.sendTerminal(text), device.log(text), setInterval/setTimeout and their clear "
        "functions.";
    getScript.inputSchema =
        AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    getScript.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        DiagramScriptRuntime* runtime = window->m_scriptRuntimes.value(id);
        reply(AgentToolResult::json(QJsonObject{
            {"script", device->script},
            {"recentOutput",
             QJsonArray::fromStringList(runtime ? runtime->recentLog() : QStringList())},
        }));
    };
    registry->registerTool(getScript);

    AgentTool setScript;
    setScript.name = "set_device_script";
    setScript.title = "Set device script";
    setScript.description =
        "Replaces a device's JavaScript automation (see get_device_script for its API) and "
        "starts it right away; one undo step on the devices history. A script can send "
        "commands, so the first time the user is asked to allow it. Returns whether it "
        "evaluated, with any error.";
    setScript.inputSchema = AgentSchema()
                                .string("deviceId", "Device id (get_app_state).", true)
                                .string("script", "The whole script; empty removes it.", true)
                                .toJson();
    setScript.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        DiagramScriptRuntime* runtime = window->m_scriptRuntimes.value(id);
        if (!device || !runtime) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        const QString script = args.value("script").toString();
        if (!script.trimmed().isEmpty() &&
            !allowHardware(window, QCoreApplication::translate(
                                       "AgentAppTools", "run a script that controls a device"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        Device updated = *device;
        updated.script = script;
        window->m_devicesGrid->updateDevice(updated);
        const QStringList before = runtime->recentLog();
        const bool ok = runtime->setScript(script);
        const QStringList after = runtime->recentLog();
        reply(AgentToolResult::json(QJsonObject{
            {"evaluated", ok},
            {"output", QJsonArray::fromStringList(after.mid(qMin(before.size(), after.size())))},
        }));
    };
    registry->registerTool(setScript);
}

void AgentAppTools::registerOtaTools(MainWindow* window, ToolRegistry* registry) {
    AgentTool status;
    status.name = "ota_status";
    status.title = "OTA status";
    status.readOnly = true;
    status.description =
        "Asks a device's TraceKit recovery (Wi-Fi firmware update) whether it answers: it does "
        "only after `btp -recovery` (send_command), a crash loop or a failed update. Reports the "
        "application and recovery versions and why it is in the recovery.";
    status.inputSchema =
        AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    status.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        const QString address = otaAddressFor(*device);
        if (address.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral(
                "No address for the device: connect it over TCP, or set otaAddress.")));
            return;
        }
        auto* client = new OtaClient(window);
        auto done = std::make_shared<bool>(false);
        QObject::connect(
            client, &OtaClient::statusChecked, client,
            [client, done, reply](const QString&, bool reachable, const RecoveryStatus& status,
                                  const QString& error) {
                *done = true;
                reply(AgentToolResult::json(QJsonObject{
                    {"inRecovery", reachable},
                    {"otaReady", status.otaReady},
                    {"reason", status.reason},
                    {"application", status.appValid ? status.appProject + QLatin1Char(' ') +
                                                          status.appVersion
                                                    : QString()},
                    {"recoveryVersion", status.recoveryVersion},
                    {"chip", status.chip},
                    {"maxSize", status.maxSize},
                    {"error", error}}));
                client->deleteLater();
            });
        QTimer::singleShot(kOtaStatusTimeoutMs, client, [client, done, reply] {
            if (!*done) {
                *done = true;
                reply(AgentToolResult::error(QStringLiteral("No answer from the device.")));
                client->deleteLater();
            }
        });
        client->checkStatus(id, address);
    };
    registry->registerTool(status);

    AgentTool upload;
    upload.name = "ota_upload";
    upload.title = "OTA upload";
    upload.destructive = true;
    upload.description =
        "Flashes a firmware .bin onto a device in its TraceKit recovery (ota_status says "
        "inRecovery; send `btp -recovery` first otherwise), signed with the device's password "
        "(peerPassword). The user confirms every upload in a dialog. Returns when the upload "
        "finishes, with the device's answer; the device then starts the new firmware.";
    upload.inputSchema = AgentSchema()
                             .string("deviceId", "Device id (get_app_state).", true)
                             .string("firmwarePath", "Absolute path of the firmware .bin.", true)
                             .toJson();
    upload.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QString path = args.value("firmwarePath").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        const QString address = otaAddressFor(*device);
        if (address.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral(
                "No address for the device: connect it over TCP, or set otaAddress.")));
            return;
        }
        if (!QFileInfo(path).isFile()) {
            reply(AgentToolResult::error(QStringLiteral("No file at %1.").arg(path)));
            return;
        }
        if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Firmware Update"),
                     QCoreApplication::translate(
                         "AgentAppTools",
                         "An AI assistant wants to flash \"%1\" onto %2. The device restarts "
                         "with the new firmware.")
                         .arg(QFileInfo(path).fileName(), device->name))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        auto* client = new OtaClient(window);
        auto done = std::make_shared<bool>(false);
        const QString name = device->name;
        QObject::connect(client, &OtaClient::uploadProgress, window,
                         [window, name](const QString&, qint64 sent, qint64 total) {
                             if (total > 0) {
                                 window->postStatus(
                                     QCoreApplication::translate("AgentAppTools",
                                                                 "Uploading firmware to %1: %2%")
                                         .arg(name)
                                         .arg(sent * 100 / total),
                                     2000);
                             }
                         });
        QObject::connect(
            client, &OtaClient::uploadFinished, client,
            [client, done, reply](const QString&, bool success, const QString& message) {
                *done = true;
                reply(success ? AgentToolResult::text(message) : AgentToolResult::error(message));
                client->deleteLater();
            });
        QTimer::singleShot(kOtaUploadTimeoutMs, client, [client, done, reply] {
            if (!*done) {
                *done = true;
                reply(AgentToolResult::error(QStringLiteral("The upload timed out.")));
                client->deleteLater();
            }
        });
        const QByteArray keyE = device->peerPassword.isEmpty()
                                    ? QByteArray()
                                    : window->channelKeyFor(device->peerPassword);
        client->uploadFirmware(id, address, keyE, path);
    };
    registry->registerTool(upload);

    AgentTool toApp;
    toApp.name = "ota_reboot_to_app";
    toApp.title = "Leave the recovery";
    toApp.description =
        "Restarts a device that is in its TraceKit recovery back into the application already "
        "installed, without flashing anything (the OTA page's \"To app\"). Use it after "
        "`btp -recovery` when no upload follows. Signed with the device's password "
        "(peerPassword). Real hardware: the first time the user is asked to allow it.";
    toApp.inputSchema =
        AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    toApp.handler = [window](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        const QString address = otaAddressFor(*device);
        if (address.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral(
                "No address for the device: connect it over TCP, or set otaAddress.")));
            return;
        }
        if (!allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                               "send commands to a device"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        auto* client = new OtaClient(window);
        auto done = std::make_shared<bool>(false);
        QObject::connect(
            client, &OtaClient::rebootFinished, client,
            [client, done, reply](const QString&, bool success, const QString& message) {
                *done = true;
                reply(success ? AgentToolResult::text(
                                    message.isEmpty()
                                        ? QStringLiteral("The device is starting its application.")
                                        : message)
                              : AgentToolResult::error(message));
                client->deleteLater();
            });
        QTimer::singleShot(kOtaStatusTimeoutMs, client, [client, done, reply] {
            if (!*done) {
                *done = true;
                reply(AgentToolResult::error(QStringLiteral("No answer from the device.")));
                client->deleteLater();
            }
        });
        const QByteArray keyE = device->peerPassword.isEmpty()
                                    ? QByteArray()
                                    : window->channelKeyFor(device->peerPassword);
        client->rebootToApp(id, address, keyE);
    };
    registry->registerTool(toApp);
}

void AgentAppTools::registerTraceKitTools(MainWindow* window, ToolRegistry* registry) {
    // The TraceKit panel's shell: checks the device is a connected TraceKit
    // one and asks once for hardware access, then runs `line` and hands
    // `done` the output -- the same Backend::runCommand() the panel uses, so
    // nothing reaches the terminal or the log panel (a Wi-Fi password
    // included).
    using Done = std::function<void(bool ok, const QString& output)>;
    const auto run = [window](const QString& id, const QString& line, const AgentToolReply& reply,
                              Done done) {
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        if (!isTraceKitDevice(device->reportedInfo)) {
            reply(AgentToolResult::error(
                QStringLiteral("%1 doesn't run TraceKit (or hasn't said so yet: connect it).")
                    .arg(device->name)));
            return;
        }
        DeviceConnection* connection = window->m_deviceConnections.value(id);
        if (!connection || !connection->isConnected() || !connection->backend()) {
            reply(AgentToolResult::error(QStringLiteral("The device isn't connected.")));
            return;
        }
        if (!allowHardware(window, QCoreApplication::translate("AgentAppTools",
                                                               "send commands to a device"))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        connection->backend()->runCommand(line, std::move(done));
    };
    // The device's answer may carry a prompt or blank lines around the JSON.
    const auto jsonIn = [](const QString& output) {
        const int start = output.indexOf(QLatin1Char('{'));
        const int end = output.lastIndexOf(QLatin1Char('}'));
        if (start < 0 || end <= start) return QJsonObject();
        return QJsonDocument::fromJson(output.mid(start, end - start + 1).toUtf8()).object();
    };
    const auto valueText = [](const QJsonValue& value) {
        if (value.isBool()) return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        if (value.isDouble()) return QString::number(value.toDouble(), 'g', 9);
        return value.toString();
    };

    AgentTool getSettings;
    getSettings.name = "tracekit_get_settings";
    getSettings.title = "TraceKit settings";
    getSettings.readOnly = true;
    getSettings.description =
        "Reads every setting a TraceKit device declares (`cfg -json`), as the TraceKit panel's "
        "Settings page shows them: name, type (bool, u8/u16/u32/i32, f32, text), value, "
        "default, min/max, unit, help, and reboot (true when it only takes effect after a "
        "reboot). Values live on the device.";
    getSettings.inputSchema =
        AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    getSettings.handler = [run, jsonIn](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        auto settings = std::make_shared<QJsonArray>();
        // One `cfg -json <i>` per setting, in turn. The step holds itself
        // only weakly; each pending command holds it strongly, so it goes
        // away once the last answer has been handled.
        auto next = std::make_shared<std::function<void(int)>>();
        std::weak_ptr<std::function<void(int)>> weakNext = next;
        *next = [run, jsonIn, id, reply, settings, weakNext](int index) {
            auto self = weakNext.lock();
            run(id, QStringLiteral("cfg -json %1").arg(index), reply,
                [jsonIn, reply, settings, self, index](bool ok, const QString& output) {
                    if (!ok) {
                        reply(AgentToolResult::error(
                            QStringLiteral("Could not read the settings: %1").arg(output.trimmed())));
                        return;
                    }
                    QJsonObject decl = jsonIn(output);
                    const int count = decl.value("count").toInt(-1);
                    if (count < 0) {
                        reply(AgentToolResult::error(QStringLiteral(
                            "The device did not describe its settings (TraceKit too old?).")));
                        return;
                    }
                    if (count > 0) {
                        decl.remove("count");
                        settings->append(decl);
                    }
                    if (index + 1 < count) {
                        (*self)(index + 1);
                        return;
                    }
                    reply(AgentToolResult::json(QJsonObject{{"settings", *settings}}));
                });
        };
        (*next)(0);
    };
    registry->registerTool(getSettings);

    AgentTool setSettings;
    setSettings.name = "tracekit_set_settings";
    setSettings.title = "Set TraceKit settings";
    setSettings.description =
        "Changes settings on a TraceKit device (`cfg -set`, what the panel's Apply sends), one "
        "after the other: values maps setting names (tracekit_get_settings) to new values. "
        "The device checks each against its declared range. Settings marked reboot take "
        "effect after tracekit_action reboot. Returns the device's answer to each.";
    setSettings.inputSchema = AgentSchema()
                                  .string("deviceId", "Device id (get_app_state).", true)
                                  .object("values", "Setting name -> new value.", true)
                                  .toJson();
    setSettings.handler = [run, valueText](const QJsonObject& args,
                                           const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QJsonObject values = args.value("values").toObject();
        if (values.isEmpty()) {
            reply(AgentToolResult::error(QStringLiteral("values is empty.")));
            return;
        }
        auto pending = std::make_shared<QStringList>(values.keys());
        auto results = std::make_shared<QJsonObject>();
        // One `cfg -set` at a time; held the same way as in tracekit_get_settings.
        auto next = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weakNext = next;
        *next = [run, valueText, id, values, reply, pending, results, weakNext] {
            auto self = weakNext.lock();
            while (!pending->isEmpty()) {
                const QString name = pending->takeFirst();
                if (name.contains(QLatin1Char(',')) || name.contains(QLatin1Char(' '))) {
                    results->insert(name, QStringLiteral("not a setting name"));
                    continue;
                }
                const QString text = valueText(values.value(name));
                run(id, QStringLiteral("cfg -set %1, %2").arg(name, text), reply,
                    [name, results, self](bool ok, const QString& output) {
                        const QString answer = output.trimmed();
                        results->insert(
                            name, answer.isEmpty()
                                      ? (ok ? QStringLiteral("ok") : QStringLiteral("failed"))
                                      : answer);
                        (*self)();
                    });
                return;
            }
            reply(AgentToolResult::json(QJsonObject{{"results", *results}}));
        };
        (*next)();
    };
    registry->registerTool(setSettings);

    AgentTool resetSetting;
    resetSetting.name = "tracekit_reset_setting";
    resetSetting.title = "Reset TraceKit setting";
    resetSetting.description =
        "Puts one setting of a TraceKit device back to its default (`cfg -reset`).";
    resetSetting.inputSchema =
        AgentSchema()
            .string("deviceId", "Device id (get_app_state).", true)
            .string("name", "Setting name (tracekit_get_settings).", true)
            .toJson();
    resetSetting.handler = [run](const QJsonObject& args, const AgentToolReply& reply) {
        const QString name = args.value("name").toString().trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char(' '))) {
            reply(AgentToolResult::error(QStringLiteral("Not a setting name.")));
            return;
        }
        run(args.value("deviceId").toString(), QStringLiteral("cfg -reset %1").arg(name), reply,
            [reply](bool ok, const QString& output) {
                reply(ok ? AgentToolResult::text(output.trimmed())
                         : AgentToolResult::error(output.trimmed()));
            });
    };
    registry->registerTool(resetSetting);

    AgentTool wifiList;
    wifiList.name = "tracekit_wifi_list";
    wifiList.title = "TraceKit Wi-Fi networks";
    wifiList.readOnly = true;
    wifiList.description =
        "The Wi-Fi networks a TraceKit device knows, in the order it tries them (names only; "
        "passwords are never read back). source \"config\" is the one built into the firmware, "
        "which can't be removed. max is how many it can keep.";
    wifiList.inputSchema =
        AgentSchema().string("deviceId", "Device id (get_app_state).", true).toJson();
    wifiList.handler = [run, jsonIn](const QJsonObject& args, const AgentToolReply& reply) {
        run(args.value("deviceId").toString(), QStringLiteral("btp -wifi_json"), reply,
            [reply, jsonIn](bool ok, const QString& output) {
                const QJsonObject answer = jsonIn(output);
                if (!ok || !answer.contains("networks")) {
                    reply(AgentToolResult::error(
                        QStringLiteral("Could not read the networks: %1").arg(output.trimmed())));
                    return;
                }
                reply(AgentToolResult::json(answer));
            });
    };
    registry->registerTool(wifiList);

    AgentTool wifiAdd;
    wifiAdd.name = "tracekit_wifi_add";
    wifiAdd.title = "Add TraceKit Wi-Fi network";
    wifiAdd.description =
        "Adds a Wi-Fi network to a TraceKit device (it takes effect after a reboot; the "
        "recovery uses the same networks). The password goes straight to the device and is "
        "not shown in the terminal or the log. Empty password: an open network.";
    wifiAdd.inputSchema = AgentSchema()
                              .string("deviceId", "Device id (get_app_state).", true)
                              .string("ssid", "Network name (no commas).", true)
                              .string("password", "Network password; empty for an open one.")
                              .toJson();
    wifiAdd.handler = [run](const QJsonObject& args, const AgentToolReply& reply) {
        const QString ssid = args.value("ssid").toString().trimmed();
        const QString password = args.value("password").toString();
        if (ssid.isEmpty() || ssid.contains(QLatin1Char(','))) {
            reply(AgentToolResult::error(
                QStringLiteral("A network name is needed, and it can't contain a comma.")));
            return;
        }
        if (password.contains(QLatin1Char(','))) {
            reply(AgentToolResult::error(QStringLiteral("A password with a comma can't be sent.")));
            return;
        }
        run(args.value("deviceId").toString(),
            QStringLiteral("btp -wifi_add %1, %2").arg(ssid, password), reply,
            [reply](bool ok, const QString& output) {
                reply(ok ? AgentToolResult::text(output.trimmed())
                         : AgentToolResult::error(output.trimmed()));
            });
    };
    registry->registerTool(wifiAdd);

    AgentTool wifiRemove;
    wifiRemove.name = "tracekit_wifi_remove";
    wifiRemove.title = "Remove TraceKit Wi-Fi network";
    wifiRemove.destructive = true;
    wifiRemove.description =
        "Removes a Wi-Fi network from a TraceKit device after the user confirms: every entry "
        "with that name goes (the firmware's built-in one stays). A device left without "
        "networks only works over a cable.";
    wifiRemove.inputSchema = AgentSchema()
                                 .string("deviceId", "Device id (get_app_state).", true)
                                 .string("ssid", "Network name (tracekit_wifi_list).", true)
                                 .toJson();
    wifiRemove.handler = [window, run](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QString ssid = args.value("ssid").toString().trimmed();
        if (ssid.isEmpty() || ssid.contains(QLatin1Char(','))) {
            reply(AgentToolResult::error(QStringLiteral("Not a network name.")));
            return;
        }
        const QVector<Device> devices = window->m_devicesGrid->devices();
        const Device* device = findDevice(devices, id);
        if (!device) {
            reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
            return;
        }
        if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Remove a network"),
                     QCoreApplication::translate(
                         "AgentAppTools",
                         "An AI assistant wants to remove the Wi-Fi network \"%1\" from %2.")
                         .arg(ssid, device->name))) {
            reply(AgentToolResult::error(QStringLiteral("The user declined.")));
            return;
        }
        run(id, QStringLiteral("btp -wifi_remove %1").arg(ssid), reply,
            [reply](bool ok, const QString& output) {
                reply(ok ? AgentToolResult::text(output.trimmed())
                         : AgentToolResult::error(output.trimmed()));
            });
    };
    registry->registerTool(wifiRemove);

    AgentTool action;
    action.name = "tracekit_action";
    action.title = "TraceKit action";
    action.description =
        "One of the TraceKit panel's actions: set_clock (from this computer, unless the device "
        "already has internet time), status (link, memory, time, power and sleep), keep_awake "
        "(a sleeping sensor stays up 10 min), reboot (restarts the application), recovery "
        "(restarts into the recovery to wait for a firmware: ota_upload, or "
        "ota_reboot_to_app to go back), usb_bootloader (ROM download mode for 30 s), "
        "factory_reset (erases the application and files, keeps key, Wi-Fi and settings; the "
        "user confirms). Returns the device's answer.";
    action.inputSchema =
        AgentSchema()
            .string("deviceId", "Device id (get_app_state).", true)
            .string("action", "What to do.", true,
                    {"set_clock", "status", "keep_awake", "reboot", "recovery", "usb_bootloader",
                     "factory_reset"})
            .toJson();
    action.handler = [window, run](const QJsonObject& args, const AgentToolReply& reply) {
        const QString id = args.value("deviceId").toString();
        const QString what = args.value("action").toString();
        QStringList lines;
        if (what == QLatin1String("set_clock")) {
            lines << QStringLiteral("btp -time_sync %1").arg(QDateTime::currentSecsSinceEpoch());
        } else if (what == QLatin1String("status")) {
            lines << QStringLiteral("btp -status") << QStringLiteral("btp -time")
                  << QStringLiteral("btp -power") << QStringLiteral("btp -sleep");
        } else if (what == QLatin1String("keep_awake")) {
            lines << QStringLiteral("btp -awake 10");
        } else if (what == QLatin1String("reboot")) {
            lines << QStringLiteral("btp -reboot");
        } else if (what == QLatin1String("recovery")) {
            lines << QStringLiteral("btp -recovery");
        } else if (what == QLatin1String("usb_bootloader")) {
            lines << QStringLiteral("btp -bootloader");
        } else if (what == QLatin1String("factory_reset")) {
            const QVector<Device> devices = window->m_devicesGrid->devices();
            const Device* device = findDevice(devices, id);
            if (!device) {
                reply(AgentToolResult::error(QStringLiteral("Unknown device \"%1\".").arg(id)));
                return;
            }
            if (!confirm(window, QCoreApplication::translate("AgentAppTools", "Factory reset"),
                         QCoreApplication::translate(
                             "AgentAppTools",
                             "An AI assistant wants to erase the application and the files of "
                             "%1. It then waits in the recovery for a new firmware.")
                             .arg(device->name))) {
                reply(AgentToolResult::error(QStringLiteral("The user declined.")));
                return;
            }
            lines << QStringLiteral("btp -factory_reset yes");
        } else {
            reply(AgentToolResult::error(QStringLiteral("Unknown action \"%1\".").arg(what)));
            return;
        }
        auto outputs = std::make_shared<QStringList>();
        // The lines in turn; held the same way as in tracekit_get_settings.
        auto next = std::make_shared<std::function<void()>>();
        std::weak_ptr<std::function<void()>> weakNext = next;
        *next = [run, id, reply, lines, outputs, weakNext] {
            if (outputs->size() == lines.size()) {
                reply(AgentToolResult::text(outputs->join(QLatin1Char('\n'))));
                return;
            }
            auto self = weakNext.lock();
            const QString line = lines.at(outputs->size());
            run(id, line, reply, [line, outputs, self](bool ok, const QString& output) {
                const QString answer = output.trimmed();
                outputs->append(QStringLiteral("> %1\n%2").arg(
                    line, answer.isEmpty() ? (ok ? QStringLiteral("ok") : QStringLiteral("failed"))
                                           : answer));
                (*self)();
            });
        };
        (*next)();
    };
    registry->registerTool(action);
}

}  // namespace traceview
