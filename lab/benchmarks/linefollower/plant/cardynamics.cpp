#include "cardynamics.h"

#include <cmath>

namespace tracelab::lf {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

CarDynamics::CarDynamics(const CarDynamicsParams& params) : m_params(params) {
    m_wheelSpeedPerPercent = (2 * kPi * params.wheelsRpm) / (60 * 100);
    m_gainVm = m_wheelSpeedPerPercent * params.wheelsRadius / 2;
    m_gainOmega = m_wheelSpeedPerPercent * params.wheelsRadius / params.wheelsDistance;

    const double tauLeft = params.accommodationTimeLeft / 5;
    const double tauRight = params.accommodationTimeRight / 5;
    const double a1 = std::exp(-params.ts / tauLeft);
    const double a2 = std::exp(-params.ts / tauRight);
    m_left.setConstants(a1, params.keLeft * (1 - a1), params.kq * (1 - a1));
    m_right.setConstants(a2, params.keRight * (1 - a2), params.kq * (1 - a2));
}

void CarDynamics::step(double u1, double u2, double q1, double q2) {
    m_left.step(u1, q1);
    m_right.step(u2, q2);
    uLeft = u1;
    uRight = u2;
    qLeft = q1;
    qRight = q2;
}

void CarDynamics::reset() {
    m_left.reset();
    m_right.reset();
    uLeft = uRight = qLeft = qRight = 0.0;
}

double CarDynamics::speed() const {
    return (m_left.y() + m_right.y()) * m_gainVm;
}

double CarDynamics::omega() const {
    return (m_left.y() - m_right.y()) * m_gainOmega;
}

double CarDynamics::speedNorm() const {
    return (m_left.y() + m_right.y()) * 0.5;
}

double CarDynamics::omegaNorm() const {
    return (m_left.y() - m_right.y()) * 0.5;
}

CarDynamics::Space CarDynamics::space() const {
    const double s = speed() * m_params.ts;
    const double angle = omega() * m_params.ts;
    return {s * std::sin(angle), s * std::cos(angle), angle};
}

std::pair<double, double> CarDynamics::wheelsSpeed() const {
    return {m_left.y() * m_wheelSpeedPerPercent, m_right.y() * m_wheelSpeedPerPercent};
}

Pose integratePose(const Pose& pose, double speed, double omega, double ts) {
    const double s = speed * ts;
    return {pose.x + s * std::cos(pose.theta), pose.y + s * std::sin(pose.theta),
            pose.theta + omega * ts};
}

double wrapAngle(double theta) {
    return std::atan2(std::sin(theta), std::cos(theta));
}

}  // namespace tracelab::lf
