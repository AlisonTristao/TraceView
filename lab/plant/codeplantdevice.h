#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QPointer>
#include <QStringList>
#include <btp/catalog.hpp>
#include <btp/node.hpp>
#include <deque>
#include <memory>
#include <vector>

#include "lab/code/codeblockowner.h"
#include "lab/plant/codeplant.h"
#include "lab/plant/labplantdevice.h"
#include "lab/plant/plantenvironment.h"

class QFileSystemWatcher;
class QTimer;

namespace tracelab {

class BtpProducerPort;
class LoopbackTransport;

// A plant written in C (CodePlant) as a BTP device: "TraceLab: Plant" on the
// Devices tab. Its manifest comes from the code's declarations -- loop.state
// carries tick, mode and the TL_OUTPUTs, loop.u takes the TL_INPUTs (whole
// plans), sim.states the TL_STATEs and TL_DIAGs, sim.truth the environment's
// fields (or the states), plus the environment's sim.preview and document
// topic -- and is rebuilt when a new version of the code declares something
// else (the links then start new sessions to read it). The clock, the run
// modes, the plans and the sim.* actions behave as the line follower's own
// plant (lfplantdevice.h). Its Device::moduleConfig:
//   {"plant": {"path": ... | "template": ..., "params": {...}},
//    "environment": {"id": "linefollower.track", ...its settings...},
//    "fps": 80, "runMode": "realtime" | "fast" | "lockstep", "timeScale": 1,
//    "autoRun": true, "solver": "rk4" | "euler", "substeps": 4}
class CodePlantDevice : public LabPlantDevice {
    Q_OBJECT

public:
    CodePlantDevice(const QString& deviceId, quint32 sourceId, const QJsonObject& config,
                    QObject* parent = nullptr);
    ~CodePlantDevice() override;

    const QString& deviceId() const {
        return m_deviceId;
    }
    // The plant's code as a block (number 0) for the TraceLab tab's editor.
    CodeBlockOwner* codeBlocks() const override;
    const CodePlant* plant() const {
        return m_plant.get();
    }
    PlantEnvironment* environment() const {
        return m_environment.get();
    }
    quint8 mode() const {
        return m_mode;
    }
    const QVector<double>& appliedInput() const {
        return m_applied;
    }
    double perturbation() const {
        return m_perturbation;
    }
    void setPerturbation(double percent);

    void attachViewer(LoopbackTransport* peer);

    // LabPlantDevice
    QString typeName() const override;
    QString worldName() const override;
    void attachController(LoopbackTransport* peer) override;
    bool running() const override {
        return m_running;
    }
    long long tick() const override {
        return m_tick;
    }
    double time() const override;
    int sampleRate() const override {
        return m_fps;
    }
    void setSampleRate(int hz) override;
    RunMode runMode() const override {
        return m_runMode;
    }
    double timeScale() const override {
        return m_timeScale;
    }
    void setRunMode(RunMode mode, double timeScale) override;
    QVector<PlantSetting> settingsSchema() const override;
    void applySettings(const QVariantMap& values) override;
    QVector<QPair<QString, QString>> statusItems() const override;
    QJsonObject config() const override;

public slots:
    void run() override;
    void pause() override;
    void reset() override;
    void step() override;

signals:
    void finished();

private:
    class Blocks;
    friend class Blocks;
    using Catalog = btp::StaticCatalog</*Topics=*/8, /*Fields=*/256, /*StringBytes=*/8192,
                                       /*SourceInfoEntries=*/0, /*Actions=*/12>;
    struct Plan {
        qint64 refTick = -1;
        std::vector<std::vector<double>> inputs;  // per input, over the horizon
    };

    void loadPlant();
    QString loadSource(QString* fileName) const;
    void rebuildCatalog();
    void restartSessions();
    void schedule();
    void pump();
    bool tickOnce();
    void publishTick();
    void publishTruth(BtpProducerPort* port, bool due, quint64 us);
    void publishParams();
    void publishDocument();
    quint64 nextTimestampUs();
    bool waitingForPlan() const;
    int planTimeoutLeftMs() const;
    void onCommand(quint16 actionId, btp::ByteView parameters, btp::NodeActionOutcome* outcome,
                   bool stream);
    QString runTextCommand(const QString& line, bool* ok);
    const char* keep(const QString& text);

    QString m_deviceId;
    quint32 m_sourceId;
    Blocks* m_blocks = nullptr;
    std::unique_ptr<CodePlant> m_plant;
    std::unique_ptr<PlantEnvironment> m_environment;
    CodeBlockSpec m_spec;
    QVector<code::CodeMessage> m_messages;
    QString m_source;
    bool m_compiled = false;
    QStringList m_declarationSignature;

    int m_fps = 80;
    RunMode m_runMode = RunMode::RealTime;
    double m_timeScale = 1.0;
    bool m_autoRun = true;
    CodePlant::Solver m_solver = CodePlant::Solver::Rk4;
    int m_substeps = 4;

    Catalog m_catalog;
    std::deque<QByteArray> m_strings;  // the catalogue's names and units
    BtpProducerPort* m_viewer = nullptr;
    BtpProducerPort* m_controller = nullptr;
    QPointer<LoopbackTransport> m_viewerLink;
    QPointer<LoopbackTransport> m_controllerLink;
    QFileSystemWatcher* m_watcher = nullptr;
    QTimer* m_reloadTimer = nullptr;

    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    double m_ticksAtClockStart = 0.0;
    QElapsedTimer m_waitClock;
    bool m_running = false;
    bool m_finished = false;
    long long m_tick = 0;
    quint8 m_mode = 0;  // loop profile: OFF until a controller arms REMOTE
    Plan m_plan;
    QVector<double> m_applied;
    qint64 m_appliedRefTick = -1;
    quint8 m_appliedSource = 0;
    double m_perturbation = 0.0;
    quint64 m_lastTimestampUs = 0;
};

inline constexpr char kCodePlantDeviceType[] = "tracelab.plant";

}  // namespace tracelab
