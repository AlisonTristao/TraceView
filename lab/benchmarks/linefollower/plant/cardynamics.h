#ifndef TRACELAB_LF_CARDYNAMICS_H
#define TRACELAB_LF_CARDYNAMICS_H

// Line-follower benchmark: the car of line_follower_simulator
// (car_modeling/car_dynamics.py), ported as is, plus its WORLD pose.
//
// Motors, in % of PWM, discrete first order (z transform of 1 / (tau s + 1)):
//   y(k+1) = a y(k) + b sat(u(k)) + c q(k)
//   a = exp(-Ts / tau), tau = settling time / 5, b = ke (1 - a), c = kq (1 - a)
//   sat = clamp to [-100, 100]; q = perturbation (left +q, right -q)
// Kinematics:
//   v = (y_l + y_r) w1 r / 2,  omega = (y_l - y_r) w1 r / L   (> 0 turns right)
//   w1 = 2 pi RPM / (60 * 100): wheel rad/s for 1 % of PWM
//
// The pygame simulator keeps the car fixed and moves the track by
// get_space() (rotate by -omega Ts, then move by the displacement in the
// rotated frame). Here the car's pose in the track frame is integrated
// instead; it is the same motion (see tests/test_lfplant.cpp):
//   x(k+1) = x(k) + Ts v(k+1) cos theta(k)
//   y(k+1) = y(k) + Ts v(k+1) sin theta(k)
//   theta(k+1) = theta(k) + Ts omega(k+1)
// with theta the heading in the track frame, which is drawn y down: the car
// points along (cos theta, sin theta), its right side is (-sin theta,
// cos theta) (lab/scenes/diffdrivecamera.h). simulator.car_pose() reports
// the same theta, wrapped to (-pi, pi].

#include <utility>

namespace tracelab::lf {

// config.py sections "car" and "motors" (the parts the dynamics use).
struct CarDynamicsParams {
    double ts = 1.0 / 80.0;               // s, sample period (z in car_dynamics.py)
    double wheelsRadius = 0.04;           // m
    double wheelsDistance = 0.15;         // m
    double wheelsRpm = 1000.0;            // at 100 % PWM
    double keLeft = 1.0;
    double keRight = 1.0;
    double kq = 1.0;                      // perturbation gain
    double accommodationTimeLeft = 0.62;  // s, 5 tau
    double accommodationTimeRight = 0.58;
};

// car_dynamics.motor.
class Motor {
public:
    void setConstants(double a, double b, double c) {
        m_a = a;
        m_b = b;
        m_c = c;
    }
    double y() const { return m_y; }
    double a() const { return m_a; }
    double b() const { return m_b; }
    double c() const { return m_c; }
    static double saturate(double u) { return u > 100.0 ? 100.0 : (u < -100.0 ? -100.0 : u); }
    void step(double u, double q) { m_y = m_a * m_y + m_b * saturate(u) + m_c * q; }
    void reset() { m_y = 0.0; }

private:
    double m_y = 0.0;
    double m_a = 0.0;
    double m_b = 0.0;
    double m_c = 0.0;
};

struct Pose {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;  // rad, not wrapped
};

// car_dynamics.car_dynamics.
class CarDynamics {
public:
    explicit CarDynamics(const CarDynamicsParams& params = {});

    const CarDynamicsParams& params() const { return m_params; }
    const Motor& left() const { return m_left; }
    const Motor& right() const { return m_right; }

    // step(u1, u2, q1, q2): the motors only (the pose is CarPose's).
    void step(double uLeft, double uRight, double qLeft, double qRight);
    void reset();

    double speed() const;      // m/s
    double omega() const;      // rad/s, > 0 turns right
    double speedNorm() const;  // (y_l + y_r) / 2
    double omegaNorm() const;  // (y_l - y_r) / 2
    // get_space(): (lateral, forward, angle) displacement of the step, car frame.
    struct Space {
        double dx;
        double dy;
        double angle;
    };
    Space space() const;
    std::pair<double, double> wheelsNorm() const { return {m_left.y(), m_right.y()}; }
    std::pair<double, double> wheelsSpeed() const;  // rad/s

    // The last inputs (v1, v2, q1, q2 of car_dynamics.py).
    double uLeft = 0.0;
    double uRight = 0.0;
    double qLeft = 0.0;
    double qRight = 0.0;

private:
    CarDynamicsParams m_params;
    Motor m_left;
    Motor m_right;
    double m_wheelSpeedPerPercent = 0.0;  // rad/s for 1 % of PWM
    double m_gainVm = 0.0;
    double m_gainOmega = 0.0;
};

// x(k+1) = x(k) + Ts v cos theta(k), y likewise, theta(k+1) = theta(k) + Ts omega.
Pose integratePose(const Pose& pose, double speed, double omega, double ts);

// theta wrapped to (-pi, pi], as simulator.car_pose() returns it.
double wrapAngle(double theta);

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_CARDYNAMICS_H
