#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "lab/sim/environment.h"
#include "lab/sim/modeldefinition.h"

namespace tracelab::sim {

// A compiled model: f and g of the .tlmodel, ready to evaluate. Produced by
// compileModel() below (ExprTk), consumed by a Solver and by the Scheduler.
//
// Vectors are plain arrays in model order, sized by the counts below:
//   x  states, in "states" order
//   u  inputs, in "inputs" order (already saturated to [min, max])
//   y  outputs, in "outputs" order, each OutputDef::size values wide
// Raw pointers on purpose: a SUNDIALS N_Vector hands its data the same way,
// so a solver passes it through without copying.
//
// Both functions are pure: same arguments, same result, no state changed
// (an Environment function they call is pure too, see environment.h). A
// solver may call them any number of times per step, at any t inside it.
class OdeSystem {
public:
    virtual ~OdeSystem() = default;

    virtual TimeDomain timeDomain() const = 0;
    virtual std::size_t stateCount() const = 0;
    virtual std::size_t inputCount() const = 0;
    // Total values in y[] (the sum of every OutputDef::size).
    virtual std::size_t outputCount() const = 0;

    // Continuous: dxdt = f(t, x, u, p). Discrete: dxdt receives the NEXT
    // state x[k+1] = f(k, x, u, p), with t the tick index k.
    virtual void derivatives(double t, const double* x, const double* u, double* dxdt) const = 0;
    // y = g(t, x, u, p).
    virtual void outputs(double t, const double* x, const double* u, double* y) const = 0;

    // Parameters in "parameters" order. A change applies from the next call
    // on (loop.config between two ticks); it never restarts the solver.
    virtual std::size_t parameterCount() const = 0;
    virtual void setParameter(std::size_t index, double value) = 0;
    virtual double parameter(std::size_t index) const = 0;
};

// Compiles `model` against `environments` (bound by EnvironmentConfig::type).
// Returns nullptr and fills `error` (first problem found: unknown symbol,
// missing equation, a state with two...) when the model is not valid.
// Defined in modelcompiler.cpp, where ExprTk stays isolated in one
// translation unit.
std::unique_ptr<OdeSystem> compileModel(const ModelDefinition& model,
                                        const std::vector<Environment*>& environments,
                                        std::string* error);

}  // namespace tracelab::sim
