#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

#include "core/moduledevicetype.h"
#include "lab/code/codemodule.h"

class QFileSystemWatcher;
class QTimer;

namespace tracelab {

class CodeControllerAdapter;
class CodeSignalFilter;
class ControllerHost;
class LabPlantDevice;
class LoopbackTransport;

// Where a code block's source comes from: a file of the user's, or a
// template a benchmark ships (read-only; editing it makes a file first).
// Plus the parameter values set by hand, by name.
struct CodeBlockSpec {
    QString path;
    QString templateId;
    QJsonObject params;

    bool isEmpty() const {
        return path.isEmpty() && templateId.isEmpty();
    }
    static CodeBlockSpec fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

// The controller device of the TraceLab tab: a ControllerHost closing the
// loop on a plant (found by its project Device id among the live plants),
// with a chain of filters and a controller, all written in C. Its
// Device::moduleConfig:
//   {"plantDeviceId": "...",
//    "controller": {"path": "C:/.../pd.c" | "template": "linefollower/pd",
//                   "params": {"kp": 2500}},
//    "filters": [{"path": ..., "params": {...}}, ...]}
// A block's file is watched: saved anywhere, it is compiled again. A version
// that does not compile leaves the previous one running (its messages say
// why); one that compiles replaces it between two ticks.
class LabControllerDevice : public QObject {
    Q_OBJECT

public:
    // Block indices: kController, or a filter's index (0...).
    static constexpr int kController = -1;

    LabControllerDevice(const QString& deviceId, const QJsonObject& config,
                        QObject* parent = nullptr);
    ~LabControllerDevice() override;

    const QString& deviceId() const {
        return m_deviceId;
    }
    QJsonObject config() const;
    ControllerHost* host() const {
        return m_host;
    }

    QString plantDeviceId() const {
        return m_plantDeviceId;
    }
    void setPlantDeviceId(const QString& deviceId);
    // The plant it is linked to, if live.
    LabPlantDevice* plant() const;

    int filterCount() const {
        return int(m_filterSpecs.size());
    }
    CodeBlockSpec spec(int block) const;
    // Replaces a block's source (or, with kController, the controller) and
    // loads it. An empty spec removes the controller.
    void setSpec(int block, const CodeBlockSpec& spec);
    void addFilter(const CodeBlockSpec& spec);
    void removeFilter(int index);
    void moveFilter(int from, int to);
    // Compiles a block again from its source (after its file changed).
    void reload(int block);

    // The running code of a block (null when there is none).
    const code::CodeModule* module(int block) const;
    CodeControllerAdapter* controller() const;
    CodeSignalFilter* filter(int index) const;
    // The messages of the block's last compile, which may be of a version
    // that did not replace the running one.
    QVector<code::CodeMessage> messages(int block) const;
    bool compiled(int block) const;
    // A fault of the running code, "file:line: text", or empty.
    QString runtimeError(int block) const;
    // The block's display name (its file's base name or its template's).
    QString blockName(int block) const;
    // Its source as compiled (the file or the template).
    QString source(int block) const;

    // The lines the block's printf()/tl_log() wrote since the last call.
    QStringList takeLog(int block);

    // A parameter of a block, live; kept in config().
    void setParam(int block, const QString& name, double value);

    // The TraceView link (ctl.diag, ctl.info).
    void attachViewer(LoopbackTransport* peer);

signals:
    // config() changed: the project should store it.
    void configChanged();
    // A block was (re)loaded or its messages changed.
    void blocksChanged();
    // The link's state (plant ready, configured, errors) changed.
    void statusChanged();

private:
    struct Block {
        CodeBlockSpec spec;
        QVector<code::CodeMessage> messages;
        bool compiled = false;
        QString source;
    };

    Block& block(int index);
    const Block& block(int index) const;
    QString loadSource(const CodeBlockSpec& spec, QString* fileName) const;
    void loadController();
    void loadFilters();
    void watchFiles();
    void linkPlant();
    void followPlantClock();

    QString m_deviceId;
    QString m_plantDeviceId;
    ControllerHost* m_host = nullptr;
    Block m_controller;
    QVector<Block> m_filterBlocks;
    QVector<CodeBlockSpec> m_filterSpecs;  // == m_filterBlocks[i].spec
    QPointer<LabPlantDevice> m_plant;
    QPointer<LoopbackTransport> m_ownEnd;
    QPointer<LoopbackTransport> m_viewerLink;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_findTimer = nullptr;
    QTimer* m_reloadTimer = nullptr;
    QStringList m_pendingReloads;
};

// The live TraceLab plants and controller devices of this process, by
// project Device id: how a controller finds its plant, and what the
// TraceLab tab shows and drives. Plants of any kind register here
// (a benchmark's device type does it when it creates one).
class LabDeviceRegistry : public QObject {
    Q_OBJECT

public:
    // A kind of plant the TraceLab tab can add: its ModuleDeviceType id, its
    // translated name and the Device::moduleConfig a new one starts with.
    struct PlantType {
        QString typeId;
        QString name;
        QJsonObject defaultConfig;
    };
    // A controller device that is not a LabControllerDevice (a benchmark's
    // own, in C++): shown so it can be taken out, never edited.
    struct OtherController {
        QPointer<QObject> device;
        QString name;
        QString plantDeviceId;
    };

    static LabDeviceRegistry& instance();

    // Ignored when the type id is taken.
    void registerPlantType(const PlantType& type);
    const QVector<PlantType>& plantTypes() const {
        return m_plantTypes;
    }

    void addOtherController(const QString& deviceId, QObject* device, const QString& name,
                            const QString& plantDeviceId);
    QStringList otherControllerIds() const;
    OtherController otherController(const QString& deviceId) const;

    LabPlantDevice* plant(const QString& deviceId) const;
    LabControllerDevice* controller(const QString& deviceId) const;
    QStringList plantIds() const;
    QStringList controllerIds() const;
    // The id a live device is registered under (empty if it is not).
    QString idOf(const QObject* device) const;
    // The controllers closing the loop on `plantDeviceId`.
    QStringList controllersOf(const QString& plantDeviceId) const;

    void addPlant(const QString& deviceId, LabPlantDevice* plant);
    void addController(const QString& deviceId, LabControllerDevice* controller);

signals:
    void devicesChanged();

private:
    QHash<QString, QPointer<LabPlantDevice>> m_plants;
    QHash<QString, QPointer<LabControllerDevice>> m_controllers;
    QHash<QString, OtherController> m_others;
    QVector<PlantType> m_plantTypes;
};

// The BTP source_id of a TraceLab device: FNV-1a of its project Device id
// and its type, non-zero -- stable across runs, so saved bindings and
// TraceView's manifest cache keep pointing at it.
quint32 labDeviceSourceId(const QString& deviceId, const char* typeId);

inline constexpr char kLabControllerDeviceType[] = "tracelab.controller";

// "TraceLab: Controller": LabControllerDevice as a ModuleDeviceType.
traceview::ModuleDeviceType labControllerDeviceType();

}  // namespace tracelab
