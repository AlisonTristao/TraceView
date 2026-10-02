#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "lab/sim/modeldefinition.h"
#include "lab/sim/sampleclock.h"
#include "lab/sim/solver.h"

namespace traceview {
class Transport;
}

namespace tracelab {

// The simulated plant as a BTP device: owns the engine (the Scheduler, and
// through it the compiled model, its environments and the solver), runs it on
// the sample clock and speaks BTP over a Transport -- normally one end of a
// LoopbackTransport pair, the other end being a TraceView DeviceConnection or
// the ControllerHost; TCP later, for tracelab-cli and HIL. What it publishes
// and which actions it answers is in plantmanifest.h.
//
// Per tick it publishes, in this order: sim.preview, loop.state, loop.output,
// sim.states, sim.truth (see plantmanifest.h for why the preview goes
// first). loop.u packets go to Scheduler::submitPlan(); sim.* actions call
// the slots below.
//
// Showing the plant on the Devices tab ("TraceLab: <model name>") means a
// device type TraceView can create in process. That is a later phase (plan
// T2) and needs a hook in lib/ that does not exist yet; nothing is added for
// it here.
class SimulatedPlant : public QObject {
    Q_OBJECT

public:
    enum class RunState { Unloaded, Stopped, Running, Paused };
    Q_ENUM(RunState)

    explicit SimulatedPlant(QObject* parent = nullptr);
    ~SimulatedPlant() override;

    // Compiles `model` with its environments, builds the solver and the
    // scheduler and goes to Stopped at tick 0. False, with `error`, when any
    // of them is missing or invalid -- today always, until the engine exists.
    bool load(const sim::ModelDefinition& model, const sim::SolverSettings& solver,
              const sim::SampleClock& clock, QString* error);
    const sim::ModelDefinition& model() const;
    RunState runState() const;

    // Where the plant's BTP goes. Not owned.
    void setTransport(traceview::Transport* transport);

public slots:
    // sim.run: ticks on the sample clock (RunMode decides the pace).
    void run();
    // sim.pause: no more ticks until run() or step().
    void pause();
    // sim.reset: tick 0, states at x0, environments reset; keeps the run state.
    void reset();
    // sim.step: exactly one tick, while paused or stopped.
    void step();

signals:
    void runStateChanged(tracelab::SimulatedPlant::RunState state);

private:
    sim::ModelDefinition m_model;
    std::unique_ptr<sim::Scheduler> m_scheduler;
    traceview::Transport* m_transport = nullptr;
    RunState m_runState = RunState::Unloaded;
};

}  // namespace tracelab
