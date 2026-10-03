#include "lfdevices.h"

#include <QCoreApplication>
#include <QTimer>

#include "lab/control/controllerhost.h"
#include "lab/plant/endpointtransport.h"
#include "lab/plant/loopbacktransport.h"

namespace tracelab::lf {

namespace {

constexpr int kFindPlantIntervalMs = 500;

}  // namespace

quint32 deviceSourceId(const QString& deviceId, const char* typeId) {
    quint32 hash = 2166136261u;
    const QByteArray bytes = QByteArray(typeId) + '/' + deviceId.toUtf8();
    for (char byte : bytes) {
        hash ^= quint8(byte);
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

// ---------------------------------------------------------------------------

LineControllerDevice::LineControllerDevice(const QString& deviceId,
                                           const ControllerSettings& settings, QObject* parent)
    : QObject(parent), m_deviceId(deviceId) {
    m_host = new ControllerHost(deviceSourceId(deviceId, kControllerDeviceType), this);
    auto adapter = std::make_unique<LineControllerAdapter>(settings);
    m_adapter = adapter.get();
    m_host->setAdapter(std::move(adapter));

    m_findTimer = new QTimer(this);
    m_findTimer->setInterval(kFindPlantIntervalMs);
    connect(m_findTimer, &QTimer::timeout, this, &LineControllerDevice::linkPlant);
    connect(&LineDeviceRegistry::instance(), &LineDeviceRegistry::devicesChanged, this,
            &LineControllerDevice::linkPlant);
    m_findTimer->start();
    linkPlant();
}

LineControllerDevice::~LineControllerDevice() = default;

LineControllerAdapter* LineControllerDevice::adapter() const {
    return m_adapter;
}

const ControllerSettings& LineControllerDevice::settings() const {
    return m_adapter->settings();
}

void LineControllerDevice::setSettings(const ControllerSettings& settings) {
    const QString oldPlant = m_adapter->settings().plantDeviceId;
    m_adapter->setSettings(settings);
    m_host->refreshInfo();
    if (settings.plantDeviceId != oldPlant) {
        m_plant = nullptr;
        linkPlant();
    }
    emit settingsChanged();
}

void LineControllerDevice::attachViewer(LoopbackTransport* peer) {
    if (m_viewerLink && m_viewerLink != peer) {
        m_viewerLink->close();
    }
    m_viewerLink = peer;
    if (peer) {
        peer->setParent(this);
    }
    m_host->setViewerTransport(peer);
}

void LineControllerDevice::linkPlant() {
    LinePlantDevice* plant =
        LineDeviceRegistry::instance().plant(m_adapter->settings().plantDeviceId);
    if (plant == nullptr) {
        // No plant id configured: the first live plant there is.
        const QStringList ids = LineDeviceRegistry::instance().plantIds();
        if (m_adapter->settings().plantDeviceId.isEmpty() && !ids.isEmpty()) {
            plant = LineDeviceRegistry::instance().plant(ids.first());
        }
    }
    if (plant == m_plant && m_ownEnd && m_ownEnd->isConnected()) {
        return;
    }
    if (m_ownEnd) {
        m_host->setPlantTransport(nullptr);
        m_ownEnd->close();
        m_ownEnd->deleteLater();
    }
    m_plant = plant;
    if (!plant) {
        return;
    }
    // A link of its own to the plant's controller port: BTP all the way.
    m_ownEnd = new LoopbackTransport(this);
    auto* plantEnd = new LoopbackTransport(plant);
    m_ownEnd->connectTo(plantEnd);
    plant->attachController(plantEnd);
    m_host->setPlantTransport(m_ownEnd);
}

// ---------------------------------------------------------------------------

LineDeviceRegistry& LineDeviceRegistry::instance() {
    static LineDeviceRegistry registry;
    return registry;
}

LinePlantDevice* LineDeviceRegistry::plant(const QString& deviceId) const {
    return m_plants.value(deviceId);
}

LineControllerDevice* LineDeviceRegistry::controller(const QString& deviceId) const {
    return m_controllers.value(deviceId);
}

QStringList LineDeviceRegistry::plantIds() const {
    QStringList ids;
    for (auto it = m_plants.cbegin(); it != m_plants.cend(); ++it) {
        if (it.value()) {
            ids.append(it.key());
        }
    }
    ids.sort();
    return ids;
}

QStringList LineDeviceRegistry::controllerIds() const {
    QStringList ids;
    for (auto it = m_controllers.cbegin(); it != m_controllers.cend(); ++it) {
        if (it.value()) {
            ids.append(it.key());
        }
    }
    ids.sort();
    return ids;
}

void LineDeviceRegistry::addPlant(const QString& deviceId, LinePlantDevice* plant) {
    m_plants.insert(deviceId, plant);
    connect(plant, &QObject::destroyed, this, [this, deviceId, plant] {
        if (m_plants.value(deviceId) == plant || !m_plants.value(deviceId)) {
            m_plants.remove(deviceId);
        }
        emit devicesChanged();
    });
    emit devicesChanged();
}

void LineDeviceRegistry::addController(const QString& deviceId, LineControllerDevice* controller) {
    m_controllers.insert(deviceId, controller);
    connect(controller, &QObject::destroyed, this, [this, deviceId, controller] {
        if (m_controllers.value(deviceId) == controller || !m_controllers.value(deviceId)) {
            m_controllers.remove(deviceId);
        }
        emit devicesChanged();
    });
    emit devicesChanged();
}

// ---------------------------------------------------------------------------

QVector<traceview::ModuleDeviceType> lineFollowerDeviceTypes() {
    traceview::ModuleDeviceType plant;
    plant.id = QString::fromLatin1(kPlantDeviceType);
    plant.displayName = QCoreApplication::translate("LineFollowerDevices", "TraceLab: Line follower");
    plant.createTransport = [](const QString& deviceId, const QJsonObject& config,
                               QObject* parent) -> traceview::ModuleTransport* {
        const PlantSettings settings = PlantSettings::fromJson(config);
        auto* device = new LinePlantDevice(deviceSourceId(deviceId, kPlantDeviceType), settings);
        auto* transport = new EndpointTransport(
            [device](LoopbackTransport* peer, QString*) {
                device->attachViewer(peer);
                if (device->settings().autoRun && !device->running() && device->tick() == 0) {
                    device->run();
                }
                return true;
            },
            parent);
        device->setParent(transport);
        LineDeviceRegistry::instance().addPlant(deviceId, device);
        return transport;
    };

    traceview::ModuleDeviceType controller;
    controller.id = QString::fromLatin1(kControllerDeviceType);
    controller.displayName =
        QCoreApplication::translate("LineFollowerDevices", "TraceLab: Controller");
    controller.createTransport = [](const QString& deviceId, const QJsonObject& config,
                                    QObject* parent) -> traceview::ModuleTransport* {
        auto* device = new LineControllerDevice(deviceId, ControllerSettings::fromJson(config));
        auto* transport = new EndpointTransport(
            [device](LoopbackTransport* peer, QString*) {
                device->attachViewer(peer);
                return true;
            },
            parent);
        device->setParent(transport);
        LineDeviceRegistry::instance().addController(deviceId, device);
        return transport;
    };
    return {plant, controller};
}

}  // namespace tracelab::lf
