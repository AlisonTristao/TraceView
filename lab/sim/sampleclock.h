#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lab/sim/environment.h"
#include "lab/sim/odesystem.h"
#include "lab/sim/solver.h"

namespace tracelab::sim {

// How the scheduler paces ticks against the wall clock.
enum class RunMode {
    // One tick every samplePeriod / timeScale of wall time. A loop.u that
    // misses its tick is applied late (and counted on loop.stats), like on
    // the robot.
    RealTime,
    // Ticks back to back, no waiting: batch runs and controller comparison.
    // Inputs arrive whenever they arrive, as in RealTime.
    AsFastAsPossible,
    // Tick k+1 starts only once the loop.u answering tick k (ref_tick == k)
    // arrived. Deterministic whatever the controller's speed: the result
    // depends on the algorithm, not on the machine.
    Lockstep,
};

// The loop's own clock, separate from the solver step (SolverSettings): tick
// k is at t_k = k * samplePeriod, which is when the plant publishes
// loop.state and from when it applies the matching loop.u.
struct SampleClock {
    double samplePeriod = 0.0125;  // s; 80 Hz
    RunMode mode = RunMode::RealTime;
    // RealTime only: simulated seconds per wall second (2 = twice as fast).
    double timeScale = 1.0;
};

// One loop.u STREAM packet: the controller's plan for the ticks from
// `refTick` on. `inputs[j]` is the input vector (model "inputs" order) for
// tick refTick + j; a simple controller sends one (M = 1), an MPC its whole
// horizon.
struct InputPlan {
    std::uint64_t refTick = 0;
    std::vector<std::vector<double>> inputs;
};

// A measured tick, for the plant to publish.
struct TickResult {
    std::uint64_t tick = 0;
    double t = 0.0;
    std::vector<double> outputs;  // loop.state: y_k
    std::vector<double> states;   // sim.states: x_k
    std::vector<double> applied;  // loop.output: the input held over the last period
    bool inputLate = false;       // no plan covered the last period: the input before was held
    bool solverFailed = false;    // the solver gave up; the run stops
    std::string error;
};

// Runs the loop: owns the tick counter and the input plans, drives the
// solver (or the discrete update) over each sample period and lets the
// environments advance. Qt-free: the plant (lab/plant) asks it when ticks are
// due and publishes what it measures.
//
// Zero-order hold. Tick k is the state x_k at t_k:
//   - measured as y_k = g(t_k, x_k, u_{k-1}) -- the input held over the
//     period that just ended (u_{-1} = 0) -- and published on loop.state with
//     tick = k, which the controller answers with a plan of refTick = k;
//   - then u_k is chosen from the plans received so far: the newest plan
//     with refTick <= k, at index k - refTick (its last entry when the
//     horizon is shorter), saturated to each input's [min, max]. With none,
//     u_{k-1} is held and the tick counts as late;
//   - u_k stays constant while the solver integrates [t_k, t_k+1] (or the
//     discrete model computes x_k+1 = f(k, x_k, u_k) once);
//   - every environment's afterTick(k + 1, t_k+1, x_k+1) runs, and tick k+1
//     is measured.
class Scheduler {
public:
    virtual ~Scheduler() = default;

    // Takes the compiled model, its environments and the solver (nullptr for
    // a discrete model), resets, and returns the measurement of tick 0.
    virtual TickResult setup(std::unique_ptr<OdeSystem> system,
                             std::vector<std::unique_ptr<Environment>> environments,
                             std::unique_ptr<Solver> solver, const SampleClock& clock) = 0;
    // Tick 0 again, every state at x0, plans and environments cleared.
    virtual TickResult reset() = 0;

    // A loop.u packet arrived. The plant calls this from its own thread only.
    virtual void submitPlan(const InputPlan& plan) = 0;

    // How many ticks are due after `wallSeconds` of running wall time since
    // the last start or resume. RealTime: those whose (scaled) time has come;
    // AsFastAsPossible: as many as the caller wants to run in one go;
    // Lockstep: 1 once a plan with refTick == tick() arrived, else 0.
    virtual std::uint64_t ticksDue(double wallSeconds) const = 0;
    // From tick k to k+1 as described above; returns the measurement of k+1.
    virtual TickResult step() = 0;

    virtual std::uint64_t tick() const = 0;
    virtual const SampleClock& clock() const = 0;
};

// The scheduler, or nullptr until it is written. Defined in scheduler.cpp.
std::unique_ptr<Scheduler> createScheduler();

}  // namespace tracelab::sim
