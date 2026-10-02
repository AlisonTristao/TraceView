#include "lab/sim/solver.h"

namespace tracelab::sim {

// To write: Euler and RK4 here, CVODE and ARKODE once SUNDIALS is fetched
// (CMake FetchContent with only those two modules).
std::unique_ptr<Solver> createSolver(const SolverSettings& settings) {
    (void)settings;
    return nullptr;
}

}  // namespace tracelab::sim
