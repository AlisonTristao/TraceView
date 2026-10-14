#ifndef TRACELAB_LF_LFSENSORS_H
#define TRACELAB_LF_LFSENSORS_H

// Line-follower benchmark: the car's sensors, built on TraceLab's generic
// sensor blocks (lab/sensors). They separate the plant's TRUE state from
// what a real car would MEASURE; simulation side only.
//
// The blocks read the state vector (LfState):
//   x = [w_l, w_r, omega, x, y, theta]   wheel speeds (rad/s), yaw rate (rad/s,
//                                        > 0 turns right), world pose (m, rad)
// and are sampled once per plant step, after the pose moved.
//
// - "encoders" (EncoderPair): each wheel's angle integrates the true wheel
//   speed over Ts; the encoder counts floor(angle * ticks / 2 pi) and the
//   speed is (count[k] - count[k-1]) * 2 pi / (ticks * Ts) -- a resolution
//   of 2 pi / (ticks Ts) (0.49 rad/s at 1024 ticks and 80 Hz); then the
//   channel imperfections (none by default).
// - "gyro" (a LinearObservation, H selects omega): yaw rate in the plant's
//   convention (> 0 turns right) plus its imperfections (none by default;
//   the noise preset adds white noise and a random-walk bias).
// - "line" (LineSensorArray): `count` sensors across the car, `spacing`
//   apart, `distance` ahead of the axle, s0 the leftmost (car frame x =
//   -(count - 1) / 2 * spacing). Each sensor sees a disc of
//   `footprintDiameter` on the floor; its ideal reading is the fraction of
//   that disc the line covers, 0..1 (1 = on the line). The line is the
//   track polyline widened by `lineHalfWidth` on each side (0.02 m: the
//   Line follower scene draws the track as discs of the "Line radius",
//   0.02 m), taken locally straight: with d the distance from the sensor to
//   the polyline and r the footprint radius,
//     reading = [A(w - d) - A(-w - d)] / (pi r^2),
//     A(a) = r^2 acos(-a / r) + a sqrt(r^2 - a^2)   (the disc's area left of a)
//   (a step d <= w when r = 0). The nearest segment comes from a uniform grid
//   of the segments built once (each segment in every cell within w + r of
//   it), so a query looks at a handful of segments, and where the
//   lemniscate crosses itself both branches are seen, as a real array
//   would. Then the ADC: saturation to [0, 1] and quantization to `adcBits`
//   bits (LSB 1 / (2^bits - 1)), plus the reading's imperfections.
//
// Defaults (config.py and a typical car): 1024 ticks/rev after quadrature;
// 15 sensors, 0.008 m apart, 0.15 m ahead, 4 mm footprint, 12-bit ADC; no
// noise anywhere. noisySensorParams() is the `--sensor-noise on` preset:
// gyro white 0.02 rad/s and random walk 0.002 rad/s/sqrt(s), line white 0.02
// (the encoders stay quantization only). The sensor seed is independent of
// the track seed.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "cardynamics.h"
#include "lab/sensors/observation.h"
#include "trackgenerator.h"

namespace tracelab::lf {

namespace LfState {
enum : int { WheelLeft = 0, WheelRight, Omega, X, Y, Theta, Size };
}

struct EncoderParams {
    int ticksPerRevolution = 1024;  // after quadrature; <= 0: no quantization
    sensors::ImperfectionConfig speed;
};

struct GyroParams {
    sensors::ImperfectionConfig rate;
};

struct LineArrayParams {
    int count = 15;
    double spacing = 0.008;               // m
    double distance = 0.15;               // m ahead of the axle
    double lineHalfWidth = 0.02;          // m
    double footprintDiameter = 0.004;     // m
    int adcBits = 12;                     // <= 0: no ADC (no saturation, no quantization)
    bool closedTrack = true;              // the last point joins the first
    sensors::ImperfectionConfig reading;  // ADC saturation/quantization are added on top
};

struct LineFollowerSensorParams {
    std::uint64_t seed = 1;
    EncoderParams encoders;
    GyroParams gyro;
    LineArrayParams line;
};

// The `--sensor-noise on` preset applied to `base` (see the header comment).
LineFollowerSensorParams noisySensorParams(LineFollowerSensorParams base = {});

class EncoderPair : public sensors::SensorBlock {
public:
    explicit EncoderPair(int ticksPerRevolution, std::string name = "encoders");
    int ticksPerRevolution() const {
        return m_ticks;
    }
    long long count(int wheel) const {
        return m_count[wheel];
    }

protected:
    void evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                  const sensors::SensorEnvironment& env, Eigen::VectorXd& y) override;
    void transduce(Eigen::VectorXd& raw) override;
    void resetState() override;

private:
    int m_ticks;
    double m_angle[2] = {0.0, 0.0};
    long long m_count[2] = {0, 0};
};

class LineSensorArray : public sensors::SensorBlock {
public:
    LineSensorArray(const std::vector<double>& trackX, const std::vector<double>& trackY,
                    const LineArrayParams& params, std::string name = "line");

    const LineArrayParams& params() const {
        return m_params;
    }
    // Sensor i's position in the world for `pose`.
    Point2 sensorPosition(int index, const Pose& pose) const;
    // Distance from `p` to the track polyline (infinity beyond w + r of it).
    double distanceToLine(const Point2& p) const;
    // The ideal reading for a sensor at distance d from the centreline.
    double coverage(double distance) const;

protected:
    void evaluate(const Eigen::VectorXd& x, const Eigen::VectorXd& u,
                  const sensors::SensorEnvironment& env, Eigen::VectorXd& y) override;

private:
    long long cellKey(long long ix, long long iy) const {
        return (ix << 32) ^ (iy & 0xffffffffLL);
    }

    LineArrayParams m_params;
    std::vector<double> m_x;
    std::vector<double> m_y;
    int m_segments = 0;
    double m_cell = 0.25;
    std::unordered_map<long long, std::vector<int>> m_grid;
};

// What the sensors report, true (ideal) or measured.
struct SensorReadings {
    double wheelSpeedLeft = 0.0;   // rad/s
    double wheelSpeedRight = 0.0;  // rad/s
    double gyro = 0.0;             // rad/s, > 0 turns right
    std::vector<double> line;      // 0..1, s0 leftmost
};

class LineFollowerSensors {
public:
    LineFollowerSensors(const Track& track, const LineFollowerSensorParams& params, double ts);

    const LineFollowerSensorParams& params() const {
        return m_params;
    }
    void reset();
    // One sample from the true state after a plant step.
    void sample(const CarDynamics& car, const Pose& pose, double t, long long tick);

    const SensorReadings& trueReadings() const {
        return m_true;
    }
    const SensorReadings& measured() const {
        return m_measured;
    }
    const sensors::SensorSet& set() const {
        return m_set;
    }
    const LineSensorArray& lineArray() const {
        return *m_line;
    }
    const EncoderPair& encoders() const {
        return *m_encoders;
    }

private:
    LineFollowerSensorParams m_params;
    sensors::SensorSet m_set;
    EncoderPair* m_encoders = nullptr;
    sensors::SensorBlock* m_gyro = nullptr;
    LineSensorArray* m_line = nullptr;
    Eigen::VectorXd m_x;
    Eigen::VectorXd m_u;
    SensorReadings m_true;
    SensorReadings m_measured;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_LFSENSORS_H
