#include "linefollowerplant.h"

#include <utility>

namespace tracelab::lf {

namespace {
constexpr double kHalfPi = 1.57079632679489661923;
}

LineFollowerPlant::LineFollowerPlant(Track track, const PlantParams& params)
    : m_track(std::move(track)),
      m_params(params),
      m_car(params.car),
      m_progress(m_track.x, m_track.y, params.progress) {
    reset();
}

void LineFollowerPlant::reset() {
    m_car.reset();
    m_progress.reset();
    m_pose = {};
    if (m_track.size() > 0) {
        m_pose = {m_track.x[0], m_track.y[0], -kHalfPi};
    }
    m_data = {};
    m_data.futurePoints = m_progress.futurePoints();
    m_perturbation = 0.0;
    m_tick = 0;
    m_time = 0.0;
    m_finished = false;
}

bool LineFollowerPlant::step(double uLeft, double uRight) {
    if (m_finished) {
        return false;
    }
    m_car.step(uLeft, uRight, m_perturbation, -m_perturbation);
    m_pose = integratePose(m_pose, m_car.speed(), m_car.omega(), m_params.car.ts);
    m_progress.update(m_pose);
    if (m_progress.finished()) {
        m_finished = true;
        return false;
    }
    m_time += m_params.car.ts;  // ts = 1 / fps, as simulator.py adds it
    ++m_tick;
    m_data.futurePoints = m_progress.futurePoints();
    m_data.speed = m_car.speed();
    m_data.omega = m_car.omega();
    const auto wheels = m_car.wheelsSpeed();
    m_data.wheelSpeedLeft = wheels.first;
    m_data.wheelSpeedRight = wheels.second;
    m_data.lateralError = m_progress.lateralError();
    return true;
}

}  // namespace tracelab::lf
