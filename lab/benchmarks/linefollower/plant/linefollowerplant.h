#ifndef TRACELAB_LF_LINEFOLLOWERPLANT_H
#define TRACELAB_LF_LINEFOLLOWERPLANT_H

// Line-follower benchmark: the plant side of line_follower_simulator's
// simulator.step_simulation(), without pygame. One step(u_l, u_r):
//
//   1. car.step(u_l, u_r, q, -q)          motors (perturbation +left/-right)
//   2. the pose moves (integratePose, the world instead of the track)
//   3. TrackProgress::update(pose)         reach rule, future points
//   4. finished when the next point passes the last one; otherwise the
//      StepData the controller gets: future points (car frame of THIS
//      pose), speed, omega, wheel speeds, lateral error.
//
// Plain C++17, no Qt (target tracelab_lf_plant).

#include <memory>
#include <vector>

#include "cardynamics.h"
#include "trackgenerator.h"
#include "trackprogress.h"

namespace tracelab::lf {

struct PlantParams {
    CarDynamicsParams car;
    TrackProgressParams progress;
};

// simulator.StepData without the line sensor.
struct StepData {
    std::vector<CarFramePoint> futurePoints;  // N points, x lateral, y forward
    double speed = 0.0;                       // m/s
    double omega = 0.0;                       // rad/s, > 0 turns right
    double wheelSpeedLeft = 0.0;              // rad/s
    double wheelSpeedRight = 0.0;
    double lateralError = 0.0;                // m
};

class LineFollowerPlant {
public:
    LineFollowerPlant(Track track, const PlantParams& params);

    const Track& track() const { return m_track; }
    const PlantParams& params() const { return m_params; }

    // The start: on the first track point, theta = -pi/2 (the pygame track
    // starts at angle 0 with the car pointing up the screen).
    void reset();

    // One sample with the inputs held over it. Returns false when the run
    // ended on this step (coverage reached 100 %: StepData not refreshed).
    bool step(double uLeft, double uRight);

    void setPerturbation(double value) { m_perturbation = value; }
    double perturbation() const { return m_perturbation; }

    const StepData& data() const { return m_data; }
    const Pose& pose() const { return m_pose; }
    const CarDynamics& car() const { return m_car; }
    const TrackProgress& progress() const { return m_progress; }
    // Completed steps (the ones that returned true) and their time,
    // accumulated like simulator.time_simulation += 1 / fps.
    long long tick() const { return m_tick; }
    double time() const { return m_time; }
    bool finished() const { return m_finished; }

private:
    Track m_track;
    PlantParams m_params;
    CarDynamics m_car;
    TrackProgress m_progress;
    Pose m_pose;
    StepData m_data;
    double m_perturbation = 0.0;
    long long m_tick = 0;
    double m_time = 0.0;
    bool m_finished = false;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_LINEFOLLOWERPLANT_H
