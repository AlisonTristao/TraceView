#include "device.h"

namespace traceview {

QJsonObject deviceToJson(const Device& device) {
    QJsonObject object;
    object["id"] = device.id;
    object["name"] = device.name;
    object["commType"] = int(device.commType);
    object["description"] = device.description;
    object["btpVersion"] = device.btpVersion;
    object["chipType"] = device.chipType;
    object["btpId"] = device.btpId;
    object["portName"] = device.portName;
    object["baudRate"] = device.baudRate;
    object["lineTerminator"] = device.lineTerminator;
    return object;
}

Device deviceFromJson(const QJsonObject& object, bool* ok) {
    Device device;
    if (!object.contains("id")) {
        *ok = false;
        return device;
    }

    device.id = object["id"].toString();
    device.name = object.value("name").toString();
    // Only CommType::Btp exists today -- any other stored value (a newer
    // save loaded by an older build, or corruption) falls back to it rather
    // than producing an unrepresentable Device.
    device.commType = CommType::Btp;
    device.description = object.value("description").toString();
    device.btpVersion = object.value("btpVersion").toString();
    device.chipType = object.value("chipType").toString();
    device.btpId = object.value("btpId").toString();
    device.portName = object.value("portName").toString();
    device.baudRate = object.value("baudRate").toInt(921600);
    device.lineTerminator = object.value("lineTerminator").toInt(1);

    *ok = !device.id.isEmpty();
    return device;
}

} // namespace traceview
