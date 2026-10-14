#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lab/sim/modeldefinition.h"

namespace tracelab::sim {

// A function an Environment exposes to the model's expressions, called as
// "<environment>.<name>(args...)" -- track.sensors(x, y, th). It takes
// `argumentCount` numbers and writes `resultCount` (1 for a scalar, one per
// sensor for track.sensors). It may be called many times per sample period
// (every solver stage when an output is evaluated), so it must not allocate
// and must not change the environment's state: progress only moves in
// Environment::afterTick().
struct EnvironmentFunction {
    std::string name;
    std::size_t argumentCount = 0;
    std::size_t resultCount = 1;
    std::function<void(const double* arguments, double* results)> evaluate;
};

// What does not fit in an equation -- the track geometry and the line
// sensors -- as a native C++ block. The core stays generic: it only knows the
// functions an environment declares and the hooks below.
class Environment {
public:
    virtual ~Environment() = default;

    // The prefix the expressions use ("track").
    virtual std::string name() const = 0;
    // Bound once by the model compiler, by name.
    virtual std::vector<EnvironmentFunction> functions() const = 0;

    // Back to the start (sim.reset): progress cleared.
    virtual void reset() = 0;
    // Once per sample period, after the solver reached the end of tick
    // `tick` (time t), with the TRUE state x[] (model order). This is where
    // progress moves (the track's reach rule) -- never inside a function.
    virtual void afterTick(std::uint64_t tick, double t, const double* x) = 0;
};

// The line-follower track (plan phase T7). The plant publishes from it:
//   sim.track    a UTF8 document topic, once and on every change:
//                {"points": [[x, y], ...], "closed": true}, in metres, world
//                frame. The scene draws only this; it never reads the .tfg.
//   sim.truth    next_point (index of the next track point not yet reached)
//                and coverage (next_point / point count * 100, in %), next to
//                the true pose x, y, theta and the tick. Evaluation only:
//                the controller never sees sim.truth.
//   sim.preview  tick plus px[], py[]: the track points next_point + k *
//                previewSpacing, k = 1..previewCount, in the CAR frame of the
//                state of that tick (x lateral, positive to the right; y
//                forward; metres). Exactly what a reference-tracking
//                controller consumes, so it never needs the true pose. This
//                is privileged information a real robot does not have, which
//                is why it is its own explicit topic.
// Reach rule (the pygame Cluster's, now in the plant): with the car at the
// origin of its own frame heading +y, the next point is reached when its
// car-frame position satisfies |x| < R and 0 < y < R, with R = the car size
// (wheel distance); repeat while the following points satisfy it too.
// sensors(x, y, th) returns sensorCount readings, one per sensor of a bar
// sensorDistance ahead of the axle and centred on it, spaced sensorSpacing.
class TrackEnvironment : public Environment {
public:
    // Read from the model's "environments" entry of type "track".
    struct Settings {
        std::string trackPath;  // .tfg (a zip, see track_editor/tfg.py)
        std::size_t sensorCount = 15;
        double sensorSpacing = 0.008;
        double sensorDistance = 0.15;
        double carSize = 0.15;
        std::size_t previewCount = 10;
        std::size_t previewSpacing = 5;  // in track points
    };

    // The track in metres, world frame: the sim.track document's points.
    virtual std::size_t pointCount() const = 0;
    virtual double pointX(std::size_t index) const = 0;
    virtual double pointY(std::size_t index) const = 0;
    virtual bool closed() const = 0;

    // Progress as of the last afterTick(): sim.truth/next_point and
    // sim.truth/coverage.
    virtual std::size_t nextPoint() const = 0;
    virtual double coveragePercent() const = 0;

    // sim.preview for the pose (x, y, theta) of the tick being published:
    // writes previewCount values to each of px and py (car frame). Points
    // past the end of an open track are left out: returns how many were
    // written.
    virtual std::size_t preview(double x, double y, double theta, double* px, double* py) const = 0;
};

// The environment a model's "environments" entry names ("track" builds a
// TrackEnvironment), or nullptr for an unknown type. Defined in
// environment.cpp.
std::unique_ptr<Environment> createEnvironment(const EnvironmentConfig& config);

}  // namespace tracelab::sim
