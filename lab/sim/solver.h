#pragma once

#include <memory>
#include <string>

#include "lab/sim/odesystem.h"

namespace tracelab::sim {

// The integration methods offered for a continuous model. A discrete model
// has no solver (see TimeDomain).
enum class SolverKind {
    // Fixed step, own implementation (or ARKODE's tables), validated against
    // ARKODE: reproduce the firmware and the Python simulator.
    Euler,
    Rk4,
    // ARKODE (SUNDIALS): explicit Runge-Kutta (ERKStep) and implicit/IMEX
    // (ARKStep), fixed step or adaptive.
    ArkodeExplicit,
    ArkodeImplicit,
    // CVODE (SUNDIALS): variable step and order, with root finding for
    // events ("left the track", "lap completed").
    CvodeAdams,
    CvodeBdf,
};

// The solver step, independent of the sample period (SampleClock): a sample
// period of 12.5 ms can be integrated with RK4 at 1 ms, or with BDF at
// rtol 1e-6. Fixed-step methods use `fixedStep` (the last step of a sample
// period is shortened to land exactly on it); adaptive ones use the
// tolerances and never step past `maxStep` when it is > 0. ARKODE is
// adaptive unless `adaptive` is false, then it steps by `fixedStep` too.
struct SolverSettings {
    SolverKind kind = SolverKind::Rk4;
    double fixedStep = 1e-3;
    bool adaptive = true;
    double relTol = 1e-6;
    double absTol = 1e-8;
    double maxStep = 0.0;
};

// Integrates an OdeSystem over one sample period at a time, with the input
// held constant across it (zero-order hold, see sampleclock.h).
class Solver {
public:
    virtual ~Solver() = default;

    // Binds `system` (which must outlive the solver) and starts at (t0, x0).
    // Returns false, with lastError(), if the method cannot run this system.
    virtual bool initialize(const OdeSystem& system, double t0, const double* x0) = 0;
    // Back to (t0, x0) without rebinding: sim.reset.
    virtual void reset(double t0, const double* x0) = 0;

    // Integrates from time() to `tOut` with `u` constant, writing the state
    // at tOut to `x` (stateCount() values). Returns false, leaving time()
    // where the failure happened and lastError() set, when the method gives
    // up (step too small, convergence failure).
    virtual bool advance(double tOut, const double* u, double* x) = 0;
    virtual double time() const = 0;

    virtual std::string lastError() const = 0;
};

// The solver for `settings.kind`, or nullptr when this build lacks it (no
// SUNDIALS yet). Defined in solver.cpp.
std::unique_ptr<Solver> createSolver(const SolverSettings& settings);

}  // namespace tracelab::sim
