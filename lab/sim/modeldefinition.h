#pragma once

#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

// tracelab_sim is plain C++17 with no Qt, so the engine can be tested on its
// own and reused by the command-line runner. Everything in this folder is a
// contract to implement against; see lab/README.md.

namespace tracelab::sim {

// "time" in a .tlmodel. Continuous: the equations are derivatives,
// dx/dt = f(t, x, u, p), integrated by a Solver. Discrete: they are the next
// state, x[k+1] = f(k, x[k], u[k], p), applied once per sample period with no
// solver -- which reproduces the Python simulator's own update exactly.
enum class TimeDomain { Continuous, Discrete };

// One entry of "parameters": a constant the equations read by name, adjustable
// live through the loop.config action (published back on sim.params).
struct ParameterDef {
    std::string name;
    double value = 0.0;
    std::string unit;
};

// One entry of "inputs", in declaration order: that order is the position of
// each value in loop.u (u[0], u[1]...). The plant saturates an applied input
// to [min, max] and publishes what it really applied on loop.output. An
// absent bound is infinite.
struct InputDef {
    std::string name;
    std::string unit;
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();
};

// One entry of "states", in declaration order (the position in x[]), with its
// initial value. reset() puts every state back to x0.
struct StateDef {
    std::string name;
    std::string unit;
    double x0 = 0.0;
};

// "equations": the right-hand side for one state, as written in the file. In
// a continuous model it is that state's derivative, in a discrete one its next
// value. Every state has exactly one.
struct EquationDef {
    std::string state;
    std::string expression;
};

// One entry of "outputs", in declaration order: the measurements y[] the
// controller sees on loop.state. `expression` may call an Environment
// function, which can return several values (track.sensors returns one per
// sensor); `size` is how many values this output contributes to y[], filled
// in by the model compiler (1 for a plain expression).
struct OutputDef {
    std::string name;
    std::string expression;
    std::size_t size = 1;
};

// "scene": which SceneRenderer type (lab/scenes) shows this plant best, and
// the scene parameters to start it with. A suggestion only -- the user can
// pick another scene.
struct SceneHint {
    std::string type;
    std::map<std::string, double> params;
};

// One native block the model uses (see environment.h), named as the
// expressions call it ("track" for track.sensors(...)), plus its settings as
// written in the file (for the track: the .tfg path, sensor count and
// spacing, preview spacing and count...). The model compiler hands these to
// the matching Environment.
struct EnvironmentConfig {
    std::string type;
    std::map<std::string, std::string> settings;
};

// A whole .tlmodel ("format": "tlmodel/1", JSON). Parsing the file into this
// struct is the loader's job (yours: the JSON reader is a free choice since
// this target has no Qt); this struct is what every other part reads.
struct ModelDefinition {
    std::string format = "tlmodel/1";
    std::string name;
    TimeDomain time = TimeDomain::Continuous;
    std::vector<ParameterDef> parameters;
    std::vector<InputDef> inputs;
    std::vector<StateDef> states;
    std::vector<EquationDef> equations;
    std::vector<OutputDef> outputs;
    std::optional<SceneHint> scene;
    std::vector<EnvironmentConfig> environments;
};

}  // namespace tracelab::sim
