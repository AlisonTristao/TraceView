#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

#include "core/transport.h"

namespace traceview {

// The Transport of a device that lives inside this process -- an endpoint an
// optional module (docs/MODULES.md) provides, such as TraceLab's simulated
// plant. It carries the device's BTP like any cable does; open() is what
// "plugging it in" means for it (start the endpoint and announce
// connectionStateChanged(true)), so DeviceConnection can dial it with the
// same ambient retry loop as every other transport. Implementations must
// never deliver bytes from inside write(): the same re-entrancy a real port
// gives (queue them).
class ModuleTransport : public Transport {
    Q_OBJECT

public:
    using Transport::Transport;

    // Brings the in-process endpoint up. A failure is reported through
    // errorOccurred(); DeviceConnection retries on its timer.
    virtual void open() = 0;
};

// One kind of in-process device a module registers (ModuleHost::
// registerDeviceType()): shown in the Devices tab's Add Device menu and
// stored in a project as TransportType::Module plus `id` (Device::
// moduleType), with Device::moduleConfig handed back to the factory.
struct ModuleDeviceType {
    // Stable, never translated: it is what the .tvproj stores.
    QString id;
    // Translated by the module; the Add Device menu entry and the default
    // card title of a new device of this type.
    QString displayName;
    // Builds a fresh, closed transport for the device `deviceId` (Device::id)
    // configured with `config` (Device::moduleConfig). Owned by `parent`.
    std::function<ModuleTransport*(const QString& deviceId, const QJsonObject& config,
                                   QObject* parent)>
        createTransport;
};

// The device types registered so far, process-wide (the modules register
// theirs while the main window is built, before any project loads).
class ModuleDeviceTypeRegistry {
public:
    static ModuleDeviceTypeRegistry& instance();

    // False (and nothing registered) for an empty id, a missing factory or an
    // id that is already taken.
    bool registerType(const ModuleDeviceType& type);
    // For tests and module teardown.
    void unregisterType(const QString& id);
    // nullptr when `id` is not registered (its module is off or absent).
    const ModuleDeviceType* find(const QString& id) const;
    // In registration order.
    const QVector<ModuleDeviceType>& types() const {
        return m_types;
    }

private:
    QVector<ModuleDeviceType> m_types;
};

}  // namespace traceview
