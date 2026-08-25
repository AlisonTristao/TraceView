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
    object["parentDeviceId"] = device.parentDeviceId;
    // Written as a double because QJsonValue has no unsigned integer type and
    // a source_id spans the full uint32 range -- int would wrap the top half
    // into negatives. Doubles carry every uint32 exactly, so this round-trips
    // without loss; deviceFromJson() reads it back the same way.
    object["peerSourceId"] = double(device.peerSourceId);
    object["cachePeerPassword"] = device.cachePeerPassword;
    // The password is written ONLY when this device explicitly opted in.
    // Absent by default, and absent means absent: no empty key is emitted, so
    // a project saved without caching cannot be told apart from one saved
    // before the field existed, and neither leaks anything.
    if (device.cachePeerPassword && !device.peerPassword.isEmpty()) {
        object["peerPassword"] = device.peerPassword;
    }
    // Not a secret, so unlike peerPassword this is always written.
    object["otaAddress"] = device.otaAddress;
    object["cacheOtaPassword"] = device.cacheOtaPassword;
    if (device.cacheOtaPassword && !device.otaPassword.isEmpty()) {
        object["otaPassword"] = device.otaPassword;
    }
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
    switch (storedTransportType) {
        case int(TransportType::UsbHid):
            device.transportType = TransportType::UsbHid;
            break;
        case int(TransportType::HubChannel):
            device.transportType = TransportType::HubChannel;
            break;
        default:
            device.transportType = TransportType::Serial;
            break;
    }
    device.portName = object.value("portName").toString();
    device.baudRate = object.value("baudRate").toInt(921600);
    device.lineTerminator = object.value("lineTerminator").toInt(1);
    device.usbPath = object.value("usbPath").toString();

    device.parentDeviceId = object.value("parentDeviceId").toString();
    // A project written before hub channels existed has no peerSourceId, and
    // the default it falls back to is 0 -- "not configured", which never
    // connects. That is the safe direction: the alternative failure, a child
    // that silently attaches to whatever robot happens to answer, is exactly
    // what storing a real address instead of a display index prevents.
    device.peerSourceId = quint32(object.value("peerSourceId").toDouble(0.0));
    device.cachePeerPassword = object.value("cachePeerPassword").toBool(false);
    // Only read back when the project said it was cached. A stray password
    // key in a project that did not opt in is ignored rather than honored.
    device.peerPassword =
        device.cachePeerPassword ? object.value("peerPassword").toString() : QString();

    device.otaAddress = object.value("otaAddress").toString();
    device.cacheOtaPassword = object.value("cacheOtaPassword").toBool(false);
    device.otaPassword =
        device.cacheOtaPassword ? object.value("otaPassword").toString() : QString();

    *ok = !device.id.isEmpty();
    return device;
}

}  // namespace traceview
