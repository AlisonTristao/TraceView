#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <array>
#include <btp/node.hpp>
#include <memory>

#include "lab/benchmarks/linefollower/device/lfplantmanifest.h"
#include "lab/benchmarks/linefollower/plant/linefollowerplant.h"

class QTimer;

namespace tracelab {
class BtpProducerPort;
class LoopbackTransport;
}  // namespace tracelab

namespace tracelab::lf {

// How the plant's clock runs (plan 4.2).
enum class RunMode {
    RealTime = 0,         // ticks at Ts / timeScale of wall time
    AsFastAsPossible = 1, // one tick per event-loop turn, never waits
    Lockstep = 2,         // as fast as possible, but tick k+1 waits for the
                          // loop.u answering tick k (in REMOTE mode)
};

// The settings of one run, the device's Device::moduleConfig in a project:
//   {"seed": 1112, "trackType": "lemniscate", "fps": 80, "horizon": 10,
//    "futureSpacing": 5, "sensorNoise": false, "sensorSeed": 1,
//    "runMode": "realtime" | "fast" | "lockstep", "timeScale": 1.0,
//    "autoRun": true}
struct PlantSettings {
    TrackParams track;
    int fps = 80;
    // 10 preview points 5 track points apart: the qLPV MPC solves ~35x faster
    // than with the Python simulator's 45 x 3 and still tracks the line
    // (spacing 10 made it cut the curves by more than the line's width).
    int horizon = 10;
    int futureSpacing = 5;
    bool sensorNoise = false;
    quint64 sensorSeed = 1;
    RunMode runMode = RunMode::RealTime;
    double timeScale = 1.0;
    // Start running as soon as the device comes up.
    bool autoRun = true;

    static PlantSettings fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
    PlantParams plantParams() const;
};

// The line-follower plant as a BTP device -- the "TraceLab: Line follower"
// of the Devices tab. Owns the benchmark's LineFollowerPlant and runs it on
// a fixed-step clock (RunMode), serving lfplantmanifest.h on two links: one
// to TraceView (the viewer: its widgets, at the rates they ask for), one to
// the controller host (every tick). loop.u plans are applied the loop
// profile's way (element t - ref_tick - 1 of the newest plan, the last one
// held when none covers the tick) while the mode is REMOTE.
//
// Tick k: pick u_k from the plans, step the plant (motors, pose, reach rule,
// sensors), then publish sim.preview, loop.state, loop.output, sim.states,
// sim.truth of tick k. Nothing is published to the controller before tick 1,
// so the first answer is to the first step -- exactly the in-process
// harness's order (u = 0, step, controller, step...).
class LinePlantDevice : public QObject {
    Q_OBJECT

public:
    explicit LinePlantDevice(quint32 sourceId, const PlantSettings& settings = {},
                             QObject* parent = nullptr);
    ~LinePlantDevice() override;

    quint32 sourceId() const {
        return m_sourceId;
    }
    const PlantSettings& settings() const {
        return m_settings;
    }
    // Takes effect on the next reset(), except the run mode and time scale.
    void setSettings(const PlantSettings& settings);
    void setRunMode(RunMode mode, double timeScale);

    // The two links. `peer` is the endpoint's end of a connected loopback
    // pair (the other end is TraceView's / the controller's); the device
    // takes it over (reparents it). A new one replaces the old.
    void attachViewer(LoopbackTransport* peer);
    void attachController(LoopbackTransport* peer);

    bool running() const {
        return m_running;
    }
    long long tick() const {
        return m_tick;
    }
    const LineFollowerPlant& plant() const {
        return *m_plant;
    }
    quint8 mode() const {
        return m_mode;
    }
    double perturbation() const;
    // The inputs applied on the last tick.
    const std::array<double, 2>& appliedInput() const {
        return m_applied;
    }

public slots:
    void run();
    void pause();
    // Back to tick 0 with the current settings (a new track when the seed or
    // the type changed); keeps running or paused.
    void reset();
    // Exactly one tick (while paused).
    void step();
    void setPerturbation(double percent);

signals:
    void runningChanged(bool running);
    // After every tick (and after reset(), with tick 0).
    void ticked(long long tick);
    void finished();
    void settingsChanged();

private:
    struct Plan {
        qint64 refTick = -1;
        std::vector<double> uLeft;
        std::vector<double> uRight;
    };

    void rebuildPlant();
    void schedule();
    void pump();
    bool tickOnce();
    void publishTick();
    void publishTruth(BtpProducerPort* port, bool due, quint64 us);
    void publishParams();
    void publishTrack();
    quint64 nextTimestampUs();
    bool waitingForPlan() const;
    int planTimeoutLeftMs() const;
    void onCommand(BtpProducerPort* port, quint16 actionId, btp::ByteView parameters,
                   btp::NodeActionOutcome* outcome, bool stream);
    QString runTextCommand(const QString& line, bool* ok);

    quint32 m_sourceId;
    PlantSettings m_settings;
    PlantCatalog m_catalog;
    std::unique_ptr<LineFollowerPlant> m_plant;
    std::int64_t m_trackSeed = 0;
    TrackType m_trackType = TrackType::Lemniscate;
    int m_trackStride = 1;

    BtpProducerPort* m_viewer = nullptr;
    BtpProducerPort* m_controller = nullptr;
    QPointer<LoopbackTransport> m_viewerLink;
    QPointer<LoopbackTransport> m_controllerLink;

    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    double m_ticksAtClockStart = 0.0;
    QElapsedTimer m_waitClock;
    bool m_running = false;
    bool m_finished = false;
    long long m_tick = 0;
    quint8 m_mode = 0;  // loop profile: OFF until a controller arms REMOTE
    Plan m_plan;
    std::array<double, 2> m_applied{0.0, 0.0};
    qint64 m_appliedRefTick = -1;
    quint8 m_appliedSource = 0;
    quint64 m_lastTimestampUs = 0;
};

}  // namespace tracelab::lf
