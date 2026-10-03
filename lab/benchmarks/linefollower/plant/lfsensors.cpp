#include "lfsensors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "trackprogress.h"

namespace tracelab::lf {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

double pointSegmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length2 = dx * dx + dy * dy;
    double t = 0.0;
    if (length2 > 0.0) {
        t = std::clamp(((px - ax) * dx + (py - ay) * dy) / length2, 0.0, 1.0);
    }
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// The area of a disc of radius r left of the vertical line at a (centre at 0).
double discAreaLeftOf(double a, double r) {
    if (a <= -r) {
        return 0.0;
    }
    if (a >= r) {
        return kPi * r * r;
    }
    return r * r * std::acos(-a / r) + a * std::sqrt(r * r - a * a);
}

}  // namespace

LineFollowerSensorParams noisySensorParams(LineFollowerSensorParams base) {
    base.gyro.rate.whiteSigma = 0.02;
    base.gyro.rate.randomWalkSigma = 0.002;
    base.line.reading.whiteSigma = 0.02;
    return base;
}

// --- EncoderPair ---------------------------------------------------------

EncoderPair::EncoderPair(int ticksPerRevolution, std::string name)
    : SensorBlock(std::move(name), 2), m_ticks(ticksPerRevolution) {}

void EncoderPair::evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                           const sensors::SensorEnvironment& env, Eigen::VectorXd& y) {
    (void)u;
    (void)env;
    y[0] = x[LfState::WheelLeft];
    y[1] = x[LfState::WheelRight];
}

void EncoderPair::transduce(Eigen::VectorXd& raw) {
    if (m_ticks <= 0) {
        return;
    }
    const double perTick = kTwoPi / m_ticks;
    for (int wheel = 0; wheel < 2; ++wheel) {
        m_angle[wheel] += raw[wheel] * ts();
        const long long count = (long long)std::floor(m_angle[wheel] / perTick);
        raw[wheel] = double(count - m_count[wheel]) * perTick / ts();
        m_count[wheel] = count;
    }
}

void EncoderPair::resetState() {
    m_angle[0] = m_angle[1] = 0.0;
    m_count[0] = m_count[1] = 0;
}

// --- LineSensorArray -----------------------------------------------------

LineSensorArray::LineSensorArray(const std::vector<double>& trackX,
                                 const std::vector<double>& trackY, const LineArrayParams& params,
                                 std::string name)
    : SensorBlock(std::move(name), params.count), m_params(params), m_x(trackX), m_y(trackY) {
    if (m_x.size() != m_y.size()) {
        throw std::invalid_argument("LineSensorArray: track x and y differ in size");
    }
    const int n = int(m_x.size());
    m_segments = n < 2 ? 0 : (params.closedTrack ? n : n - 1);
    const double margin = params.lineHalfWidth + 0.5 * params.footprintDiameter;
    m_cell = std::max(0.05, 4.0 * margin);
    for (int i = 0; i < m_segments; ++i) {
        const int j = (i + 1) % n;
        const double x0 = std::min(m_x[i], m_x[j]) - margin;
        const double x1 = std::max(m_x[i], m_x[j]) + margin;
        const double y0 = std::min(m_y[i], m_y[j]) - margin;
        const double y1 = std::max(m_y[i], m_y[j]) + margin;
        for (long long ix = (long long)std::floor(x0 / m_cell);
             ix <= (long long)std::floor(x1 / m_cell); ++ix) {
            for (long long iy = (long long)std::floor(y0 / m_cell);
                 iy <= (long long)std::floor(y1 / m_cell); ++iy) {
                m_grid[cellKey(ix, iy)].push_back(i);
            }
        }
    }
}

Point2 LineSensorArray::sensorPosition(int index, const Pose& pose) const {
    const double lateral = (index - 0.5 * (m_params.count - 1)) * m_params.spacing;
    return carToWorld({lateral, m_params.distance}, pose);
}

double LineSensorArray::distanceToLine(const Point2& p) const {
    const auto it = m_grid.find(
        cellKey((long long)std::floor(p.x / m_cell), (long long)std::floor(p.y / m_cell)));
    double best = std::numeric_limits<double>::infinity();
    if (it == m_grid.end()) {
        return best;
    }
    const int n = int(m_x.size());
    for (const int i : it->second) {
        const int j = (i + 1) % n;
        best = std::min(best, pointSegmentDistance(p.x, p.y, m_x[i], m_y[i], m_x[j], m_y[j]));
    }
    return best;
}

double LineSensorArray::coverage(double distance) const {
    const double w = m_params.lineHalfWidth;
    const double r = 0.5 * m_params.footprintDiameter;
    if (!std::isfinite(distance)) {
        return 0.0;
    }
    if (r <= 0.0) {
        return distance <= w ? 1.0 : 0.0;
    }
    const double value =
        (discAreaLeftOf(w - distance, r) - discAreaLeftOf(-w - distance, r)) / (kPi * r * r);
    return std::clamp(value, 0.0, 1.0);
}

void LineSensorArray::evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                               const sensors::SensorEnvironment& env, Eigen::VectorXd& y) {
    (void)u;
    (void)env;
    const Pose pose{x[LfState::X], x[LfState::Y], x[LfState::Theta]};
    for (int i = 0; i < m_params.count; ++i) {
        y[i] = coverage(distanceToLine(sensorPosition(i, pose)));
    }
}

// --- LineFollowerSensors -------------------------------------------------

LineFollowerSensors::LineFollowerSensors(const Track& track, const LineFollowerSensorParams& params,
                                         double ts)
    : m_params(params) {
    m_encoders = &m_set.emplace<EncoderPair>(params.encoders.ticksPerRevolution);
    m_encoders->setAllImperfections(params.encoders.speed);

    Eigen::MatrixXd h = Eigen::MatrixXd::Zero(1, LfState::Size);
    h(0, LfState::Omega) = 1.0;
    m_gyro = &m_set.emplace<sensors::LinearObservation>("gyro", h);
    m_gyro->setAllImperfections(params.gyro.rate);

    m_line = &m_set.emplace<LineSensorArray>(track.x, track.y, params.line);
    sensors::ImperfectionConfig reading = params.line.reading;
    if (params.line.adcBits > 0) {
        reading.saturationMin = std::max(reading.saturationMin, 0.0);
        reading.saturationMax = std::min(reading.saturationMax, 1.0);
        reading.quantization = 1.0 / (std::ldexp(1.0, params.line.adcBits) - 1.0);
    }
    m_line->setAllImperfections(reading);

    m_set.configure(ts, params.seed);
    m_x = Eigen::VectorXd::Zero(LfState::Size);
    m_u = Eigen::VectorXd::Zero(2);
    reset();
}

void LineFollowerSensors::reset() {
    m_set.reset();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    m_true = {nan, nan, nan, std::vector<double>(std::size_t(m_params.line.count), nan)};
    m_measured = m_true;
}

void LineFollowerSensors::sample(const CarDynamics& car, const Pose& pose, double t,
                                 long long tick) {
    const auto wheels = car.wheelsSpeed();
    m_x[LfState::WheelLeft] = wheels.first;
    m_x[LfState::WheelRight] = wheels.second;
    m_x[LfState::Omega] = car.omega();
    m_x[LfState::X] = pose.x;
    m_x[LfState::Y] = pose.y;
    m_x[LfState::Theta] = pose.theta;
    m_u[0] = car.uLeft;
    m_u[1] = car.uRight;
    sensors::SensorEnvironment env;
    env.t = t;
    env.tick = tick;
    m_set.sample(m_x, m_u, env);

    const Eigen::VectorXd& encTrue = m_encoders->trueOutput();
    const Eigen::VectorXd& encMeas = m_encoders->measured();
    m_true.wheelSpeedLeft = encTrue[0];
    m_true.wheelSpeedRight = encTrue[1];
    m_measured.wheelSpeedLeft = encMeas[0];
    m_measured.wheelSpeedRight = encMeas[1];
    m_true.gyro = m_gyro->trueOutput()[0];
    m_measured.gyro = m_gyro->measured()[0];
    for (int i = 0; i < m_params.line.count; ++i) {
        m_true.line[std::size_t(i)] = m_line->trueOutput()[i];
        m_measured.line[std::size_t(i)] = m_line->measured()[i];
    }
}

}  // namespace tracelab::lf
