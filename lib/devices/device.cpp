#include "device.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace traceview {

namespace {

// Missing (a save from before TransportType existed) or an out-of-range
// stored value both fall back to Serial -- the only transport that existed
// before, so an older project always loads exactly as it did.
TransportType transportTypeFromJson(const QJsonValue& value) {
    switch (value.toInt(int(TransportType::Serial))) {
        case int(TransportType::UsbHid):
            return TransportType::UsbHid;
        case int(TransportType::HubChannel):
            return TransportType::HubChannel;
        case int(TransportType::Tcp):
            return TransportType::Tcp;
        case int(TransportType::Ble):
            return TransportType::Ble;
        default:
            return TransportType::Serial;
    }
}

quint16 tcpPortFromJson(const QJsonValue& value) {
    const int stored = value.toInt(44300);
    return stored >= 1 && stored <= 65535 ? quint16(stored) : quint16(44300);
}

QJsonObject linkToJson(const DeviceLink& link) {
    QJsonObject object;
    object["transportType"] = int(link.transportType);
    object["portName"] = link.portName;
    object["baudRate"] = link.baudRate;
    object["usbPath"] = link.usbPath;
    object["tcpHost"] = link.tcpHost;
    object["tcpPort"] = link.tcpPort;
    object["bleAddress"] = link.bleAddress;
    object["parentDeviceId"] = link.parentDeviceId;
    object["peerSourceId"] = double(link.peerSourceId);  // see deviceToJson()
    object["enabled"] = link.enabled;
    // Only when set, so a link saved before automatic links existed (and
    // every manual one since) is written exactly as before.
    if (link.autoTarget) {
        object["auto"] = true;
    }
    return object;
}

DeviceLink linkFromJson(const QJsonObject& object) {
    DeviceLink link;
    link.transportType = transportTypeFromJson(object.value("transportType"));
    link.portName = object.value("portName").toString();
    link.baudRate = object.value("baudRate").toInt(kDefaultSerialBaudRate);
    link.usbPath = object.value("usbPath").toString();
    link.tcpHost = object.value("tcpHost").toString();
    link.tcpPort = tcpPortFromJson(object.value("tcpPort"));
    link.bleAddress = object.value("bleAddress").toString();
    link.parentDeviceId = object.value("parentDeviceId").toString();
    link.peerSourceId = quint32(object.value("peerSourceId").toDouble(0.0));
    link.enabled = object.value("enabled").toBool(true);
    link.autoTarget = object.value("auto").toBool(false);
    return link;
}

}  // namespace

int deviceLinkCount(const Device& device) {
    return 1 + device.extraLinks.size();
}

DeviceLink deviceLinkAt(const Device& device, int index) {
    if (index > 0 && index <= device.extraLinks.size()) {
        return device.extraLinks.at(index - 1);
    }
    DeviceLink link;
    link.transportType = device.transportType;
    link.portName = device.portName;
    link.baudRate = device.baudRate;
    link.usbPath = device.usbPath;
    link.tcpHost = device.tcpHost;
    link.tcpPort = device.tcpPort;
    link.bleAddress = device.bleAddress;
    link.parentDeviceId = device.parentDeviceId;
    link.peerSourceId = device.peerSourceId;
    link.enabled = true;  // the primary link is always a candidate
    link.autoTarget = device.autoTarget;
    return link;
}

Device deviceWithLink(const Device& device, const DeviceLink& link) {
    Device out = device;
    out.transportType = link.transportType;
    out.portName = link.portName;
    out.baudRate = link.baudRate;
    out.usbPath = link.usbPath;
    out.tcpHost = link.tcpHost;
    out.tcpPort = link.tcpPort;
    out.bleAddress = link.bleAddress;
    out.parentDeviceId = link.parentDeviceId;
    out.peerSourceId = link.peerSourceId;
    out.autoTarget = link.autoTarget;
    return out;
}

Device effectiveDevice(const Device& device) {
    if (device.activeLink <= 0 || device.activeLink > device.extraLinks.size()) {
        return device;
    }
    return deviceWithLink(device, device.extraLinks.at(device.activeLink - 1));
}

QByteArray deviceLinksSignature(const Device& device) {
    QJsonArray links;
    // Automatic links are reached through the name, so renaming the robot is
    // an edit of how to reach it.
    links.append(device.robotName);
    for (int i = 0; i < deviceLinkCount(device); ++i) {
        links.append(linkToJson(deviceLinkAt(device, i)));
    }
    return QJsonDocument(links).toJson(QJsonDocument::Compact);
}

bool deviceLinkConfigured(const DeviceLink& link) {
    switch (link.transportType) {
        case TransportType::Serial:
            return !link.portName.isEmpty();
        case TransportType::UsbHid:
            return !link.usbPath.isEmpty();
        case TransportType::Tcp:
            return !link.tcpHost.isEmpty();
        case TransportType::Ble:
            return !link.bleAddress.isEmpty();
        case TransportType::HubChannel:
            return !link.parentDeviceId.isEmpty() && link.peerSourceId != 0;
    }
    return false;
}

bool sameLinkTarget(const DeviceLink& a, const DeviceLink& b) {
    return a.transportType == b.transportType && a.portName == b.portName &&
           a.usbPath == b.usbPath && a.tcpHost == b.tcpHost && a.tcpPort == b.tcpPort &&
           a.bleAddress == b.bleAddress && a.parentDeviceId == b.parentDeviceId &&
           a.peerSourceId == b.peerSourceId;
}

bool deviceLinkUsable(const Device& device, const DeviceLink& link) {
    if (!link.enabled) {
        return false;
    }
    return link.autoTarget ? !device.robotName.trimmed().isEmpty() : deviceLinkConfigured(link);
}

QString mdnsHostFromName(const QString& name) {
    QString host;
    bool lastWasDash = true;  // no leading '-'
    for (const QChar c : name.trimmed().toLower()) {
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
            (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
            host += c;
            lastWasDash = false;
        } else if (!lastWasDash) {
            host += QLatin1Char('-');
            lastWasDash = true;
        }
    }
    host = host.left(63);
    while (host.endsWith(QLatin1Char('-'))) {
        host.chop(1);
    }
    return host;
}

namespace {

bool sameRobotName(const QString& a, const QString& b) {
    const QString left = a.trimmed();
    return !left.isEmpty() && left.compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

}  // namespace

bool linkNeedsDirectory(const DeviceLink& link) {
    return link.autoTarget && (link.transportType == TransportType::Serial ||
                               link.transportType == TransportType::HubChannel);
}

DeviceLink resolveLinkByName(const QString& robotName, const DeviceLink& link,
                             const LinkDirectory& directory) {
    if (!link.autoTarget) {
        return link;
    }
    DeviceLink out = link;
    const QString name = robotName.trimmed();
    switch (link.transportType) {
        case TransportType::Serial: {
            out.portName.clear();
            int matches = 0;
            bool lastPortPresent = false;
            for (const SerialPortOption& port : directory.ports) {
                if (sameRobotName(port.productName, name)) {
                    out.portName = port.name;
                    ++matches;
                }
                lastPortPresent = lastPortPresent || port.name == link.portName;
            }
            if (matches == 0 && lastPortPresent) {
                // No port reports the name -- firmware without it in the USB
                // descriptor, a USB-UART bridge, or Windows still showing the
                // old one. The port this link was last set to is the best
                // remaining guess, and it is one the user picked.
                out.portName = link.portName;
            } else if (matches != 1) {
                out.portName.clear();
            }
            break;
        }
        case TransportType::Tcp: {
            const QString host = mdnsHostFromName(name);
            out.tcpHost = host.isEmpty() ? QString() : host + QStringLiteral(".local");
            break;
        }
        case TransportType::Ble:
            out.bleAddress = name;
            break;
        case TransportType::HubChannel: {
            // A chosen hub narrows the search; none means any hub.
            QString parent;
            quint32 sourceId = 0;
            bool ambiguous = false;
            for (const HubPeerSighting& seen : directory.hubPeers) {
                if (!link.parentDeviceId.isEmpty() &&
                    seen.parentDeviceId != link.parentDeviceId) {
                    continue;
                }
                if (!sameRobotName(seen.peer.name, name)) {
                    continue;
                }
                if (sourceId != 0 && sourceId != seen.peer.sourceId) {
                    ambiguous = true;  // two robots answer to this name
                    break;
                }
                // The same robot heard by two hubs: keep the first hub.
                if (sourceId == 0) {
                    sourceId = seen.peer.sourceId;
                    parent = seen.parentDeviceId;
                }
            }
            out.parentDeviceId = ambiguous ? QString() : parent;
            out.peerSourceId = ambiguous ? 0 : sourceId;
            if (out.parentDeviceId.isEmpty()) {
                out.parentDeviceId = link.parentDeviceId;
            }
            break;
        }
        case TransportType::UsbHid:
            break;  // no name to match a HID path against: stays as set
    }
    return out;
}

QString serialPortLabel(const QString& portName, const QString& productName) {
    const QString product = productName.trimmed();
    if (product.isEmpty() || product.compare(QLatin1String("n/a"), Qt::CaseInsensitive) == 0 ||
        product == portName) {
        return portName;
    }
    return portName + QStringLiteral(" ") + QChar(0x2014) + QStringLiteral(" ") + product;  // em dash
}

QString deviceLinkSummary(const DeviceLink& link, const QString& robotName) {
    const QString type = transportTypeLabel(link.transportType);
    QString target;
    if (link.autoTarget) {
        const QString name = robotName.trimmed();
        switch (link.transportType) {
            case TransportType::Tcp:
                target = mdnsHostFromName(name);
                target = target.isEmpty() ? QString()
                                          : QStringLiteral("%1.local:%2").arg(target).arg(link.tcpPort);
                break;
            case TransportType::UsbHid:
                target = link.usbPath;
                break;
            default:
                target = name;
                break;
        }
        const QString automatic = QCoreApplication::translate("Device", "auto");
        return target.isEmpty() ? QStringLiteral("%1 (%2)").arg(type, automatic)
                                : QStringLiteral("%1 %2 (%3)").arg(type, target, automatic);
    }
    switch (link.transportType) {
        case TransportType::Serial:
            target = QStringLiteral("%1 @ %2").arg(link.portName).arg(link.baudRate);
            break;
        case TransportType::UsbHid:
            target = link.usbPath;
            break;
        case TransportType::Tcp:
            target = QStringLiteral("%1:%2").arg(link.tcpHost).arg(link.tcpPort);
            break;
        case TransportType::Ble:
            target = link.bleAddress;
            break;
        case TransportType::HubChannel:
            target = QStringLiteral("0x") +
                     QStringLiteral("%1").arg(link.peerSourceId, 8, 16, QChar('0')).toUpper();
            break;
    }
    return target.isEmpty() ? type : type + QLatin1Char(' ') + target;
}

QJsonObject deviceToJson(const Device& device) {
    QJsonObject object;
    object["id"] = device.id;
    object["name"] = device.name;
    object["commType"] = int(device.commType);
    object["description"] = device.description;
    object["script"] = device.script;
    object["transportType"] = int(device.transportType);
    object["portName"] = device.portName;
    object["baudRate"] = device.baudRate;
    object["lineTerminator"] = device.lineTerminator;
    object["usbPath"] = device.usbPath;
    object["tcpHost"] = device.tcpHost;
    object["tcpPort"] = device.tcpPort;
    object["bleAddress"] = device.bleAddress;
    object["blePeerUuid"] = device.blePeerUuid;
    // Only when set, like extraLinks below: a device configured by hand saves
    // exactly as it did before.
    if (!device.robotName.isEmpty()) {
        object["robotName"] = device.robotName;
    }
    if (device.autoTarget) {
        object["autoTarget"] = true;
    }
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
    // Only written when there is something to write, so a single-link device
    // saves exactly as it did before extraLinks existed. activeLink is live
    // state and never saved.
    if (!device.extraLinks.isEmpty()) {
        QJsonArray links;
        for (const DeviceLink& link : device.extraLinks) {
            links.append(linkToJson(link));
        }
        object["extraLinks"] = links;
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
    device.script = object.value("script").toString();
    device.transportType = transportTypeFromJson(object.value("transportType"));
    device.portName = object.value("portName").toString();
    device.baudRate = object.value("baudRate").toInt(kDefaultSerialBaudRate);
    device.lineTerminator = object.value("lineTerminator").toInt(1);
    device.usbPath = object.value("usbPath").toString();
    device.tcpHost = object.value("tcpHost").toString();
    device.tcpPort = tcpPortFromJson(object.value("tcpPort"));
    device.bleAddress = object.value("bleAddress").toString();
    device.blePeerUuid = object.value("blePeerUuid").toString();
    device.robotName = object.value("robotName").toString();
    device.autoTarget = object.value("autoTarget").toBool(false);

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

    const QJsonArray links = object.value("extraLinks").toArray();
    for (const QJsonValue& value : links) {
        device.extraLinks.append(linkFromJson(value.toObject()));
    }

    *ok = !device.id.isEmpty();
    return device;
}

}  // namespace traceview
