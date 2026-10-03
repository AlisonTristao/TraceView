#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include "core/moduledevicetype.h"
#include "lab/benchmarks/linefollower/device/lfcontrolleradapter.h"
#include "lab/benchmarks/linefollower/device/lfplantdevice.h"

class QTimer;

namespace tracelab {
class ControllerHost;
class LoopbackTransport;
}  // namespace tracelab

namespace tracelab::lf {

// The two in-process device types of the line-follower benchmark
// (ModuleDeviceType ids, stored in projects as Device::moduleType).
inline constexpr char kPlantDeviceType[] = "tracelab.linefollower.plant";
inline constexpr char kControllerDeviceType[] = "tracelab.linefollower.controller";

// The BTP source_id of a TraceLab device: FNV-1a of its project Device id
// and its type, non-zero -- stable across runs, so saved bindings and
// TraceView's manifest cache keep pointing at it.
quint32 deviceSourceId(const QString& deviceId, const char* typeId);

// The controller host as a device: the ControllerHost with the line
// follower's adapter, and the link to its plant -- found by Device id
// (ControllerSettings::plantDeviceId) among the live plants, and re-found
// when that plant is rebuilt.
class LineControllerDevice : public QObject {
    Q_OBJECT

public:
    LineControllerDevice(const QString& deviceId, const ControllerSettings& settings,
                         QObject* parent = nullptr);
    ~LineControllerDevice() override;

    const QString& deviceId() const {
        return m_deviceId;
    }
    ControllerHost* host() const {
        return m_host;
    }
    LineControllerAdapter* adapter() const;
    const ControllerSettings& settings() const;
    void setSettings(const ControllerSettings& settings);

    void attachViewer(LoopbackTransport* peer);

signals:
    void settingsChanged();

private:
    void linkPlant();

    QString m_deviceId;
    ControllerHost* m_host = nullptr;
    LineControllerAdapter* m_adapter = nullptr;  // owned by m_host
    QPointer<LinePlantDevice> m_plant;
    QPointer<LoopbackTransport> m_ownEnd;
    QPointer<LoopbackTransport> m_viewerLink;
    QTimer* m_findTimer = nullptr;
};

// The live TraceLab line-follower devices of this process, by project Device
// id: what the controller host finds its plant through, and what the
// TraceLab tab drives (run, pause, speed, settings).
class LineDeviceRegistry : public QObject {
    Q_OBJECT

public:
    static LineDeviceRegistry& instance();

    LinePlantDevice* plant(const QString& deviceId) const;
    LineControllerDevice* controller(const QString& deviceId) const;
    QStringList plantIds() const;
    QStringList controllerIds() const;

    void addPlant(const QString& deviceId, LinePlantDevice* plant);
    void addController(const QString& deviceId, LineControllerDevice* controller);

signals:
    void devicesChanged();

private:
    QHash<QString, QPointer<LinePlantDevice>> m_plants;
    QHash<QString, QPointer<LineControllerDevice>> m_controllers;
};

// The ModuleDeviceTypes to register (LabModule does, through ModuleHost):
// "TraceLab: Line follower" (the plant) and "TraceLab: Controller".
QVector<traceview::ModuleDeviceType> lineFollowerDeviceTypes();

}  // namespace tracelab::lf
