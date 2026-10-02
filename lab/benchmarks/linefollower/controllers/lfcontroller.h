#ifndef TRACELAB_LF_CONTROLLER_H
#define TRACELAB_LF_CONTROLLER_H

// Line-follower benchmark: the controller side.
//
// The C++ port of the Python simulator's controllers (mpc/ in
// line_follower_simulator), behind the same call the Python runner makes:
//
//   u = controller.step(wheel_speeds_rad_s, future_points)   # Python
//   std::array<double, 2> u = controller->step(wheelSpeeds, futurePoints);
//
// Plain C++17, no Qt, so the same code can later move behind the portable
// loopctl.h interface (BTP profiles) and into firmware.

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace tracelab::lf {

// The car and motor parameters both the plant and the controllers' internal
// models use (config.py sections "car" and "motors").
struct CarParams {
    double wheelsRadius = 0.04;            // m
    double wheelsDistance = 0.15;          // m
    double wheelsRpm = 1000.0;             // wheel speed at 100 % PWM
    double keLeft = 1.0;                   // static gain, left motor
    double keRight = 1.0;                  // static gain, right motor
    double accommodationTimeLeft = 0.62;   // s, 5 tau of the left motor
    double accommodationTimeRight = 0.58;  // s, 5 tau of the right motor
};

// config.py section "qlpv".
struct QlpvParams {
    std::string embedding = "sinc";     // "sinc" | "sin_cos"
    std::string scheduling = "taylor";  // "frozen" | "warm_start" | "taylor" | "iterative"
    int lMax = 3;                       // iterative only
    double eps = 1e-3;                  // iterative only
    std::string initial = "frozen";     // iterative only: "frozen" | "warm_start" | "taylor"
    double relaxation = 1.0;            // iterative only
    bool contouring = true;
    std::array<double, 3> q{1e1, 1e4, 1e2};   // [theta, lateral, longitudinal]
    double weightGrowth = 1.0;                // exponential weighting along the horizon
    std::array<double, 3> qN{1e2, 1e5, 1e3};  // terminal
    std::array<double, 2> r{0.0, 0.0};
    std::array<double, 2> rd{1e-2, 1e-2};
    double disturbanceFilter = 0.2;
};

// config.py section "gpc".
struct GpcParams {
    int controlHorizon = 5;
    double qTheta = 0.5;
    double qDistance = 1.0;
    double lamb = 1e-3;
    double weightGrowth = 1.0;
    std::string thetaReference = "lookahead";  // "lookahead" | "tangent"
    double d0 = 0.3;                           // m, theta_r = x / d0
};

struct ControllerConfig {
    std::string type = "qlpv";  // "qlpv" | "gpc" (config.py "controller.type")
    double ts = 1.0 / 80.0;     // s, sample period (1 / fps)
    int horizon = 45;           // N = number of future points
    double uMin = -100.0;       // %
    double uMax = 100.0;        // %
    CarParams car;
    QlpvParams qlpv;
    GpcParams gpc;
};

// A future track point in the CAR frame at instant k, the Python convention:
// x lateral (positive right), y forward, metres.
struct CarPoint {
    double x = 0.0;
    double y = 0.0;
};

// The controller's predicted trajectory over the horizon, car frame at k
// (the Python controller.prediction(): N x [theta, X, Y]).
struct Prediction {
    std::vector<double> theta;  // rad
    std::vector<double> x;      // m, lateral
    std::vector<double> y;      // m, forward
};

struct ControllerStats {
    int qps = 0;           // QPs solved in the last step
    int qpIterations = 0;  // active-set iterations in the last step
    double solveMs = 0.0;  // wall time of the last step
    // max |f_rho(predicted states) - rho used| of the last step (Python
    // stats["rho_error"]); 0 for GPC.
    double rhoError = 0.0;
};

class Controller {
public:
    virtual ~Controller() = default;

    // Display name, e.g. "qLPV MPC (sinc, taylor)" or "GPC".
    virtual std::string name() const = 0;

    // One sample: measured wheel speeds [left, right] in rad/s and the
    // horizon's future points (car frame). Returns [u_l, u_r] in %, within
    // [uMin, uMax]. futurePoints must hold exactly `horizon` points (the
    // Python reshape(N, ny) fails otherwise); std::invalid_argument if not.
    virtual std::array<double, 2> step(const std::array<double, 2>& wheelSpeedsRadS,
                                       const std::vector<CarPoint>& futurePoints) = 0;

    // Empty before the first step() (Python returns None).
    virtual Prediction prediction() const = 0;
    virtual ControllerStats stats() const = 0;
    virtual void reset() = 0;

    // The reference the last step() tracked, N x [theta, X, Y] (the Python
    // controller.ref, logged by runner.log_step). Empty before the first step.
    virtual Prediction reference() const {
        return {};
    }

    // The scheduling trajectory of the last step, N x [rho_1, rho_2] (the
    // Python scheduling_trajectory()); empty for GPC and before the first step.
    virtual std::vector<std::array<double, 2>> schedulingTrajectory() const {
        return {};
    }
};

// nullptr for an unknown config.type, qlpv.embedding, qlpv.scheduling or
// qlpv.initial, or a horizon < 2 (config.py: min 2).
std::unique_ptr<Controller> makeController(const ControllerConfig& config);

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_CONTROLLER_H
