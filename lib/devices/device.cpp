#include "device.h"

namespace traceview {

QJsonObject deviceToJson(const Device& device) {
    QJsonObject object;
    object["id"] = device.id;
    object["name"] = device.name;
    object["commType"] = int(device.commType);
    object["description"] = device.description;
    object["transportType"] = int(device.transportType);
    object["portName"] = device.portName;
    object["baudRate"] = device.baudRate;
    object["lineTerminator"] = device.lineTerminator;
    object["usbPath"] = device.usbPath;
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
    // Missing (a save from before TransportType existed) or an out-of-range
    // stored value both fall back to Serial -- the only transport that
    // existed before, so an older project always loads exactly as it did.
    const int storedTransportType = object.value("transportType").toInt(int(TransportType::Serial));
    device.transportType =
        storedTransportType == int(TransportType::UsbHid) ? TransportType::UsbHid : TransportType::Serial;
    device.portName = object.value("portName").toString();
    device.baudRate = object.value("baudRate").toInt(921600);
    device.lineTerminator = object.value("lineTerminator").toInt(1);
    device.usbPath = object.value("usbPath").toString();

    *ok = !device.id.isEmpty();
    return device;
}

} // namespace traceview
