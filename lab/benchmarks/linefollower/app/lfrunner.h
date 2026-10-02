#ifndef TRACELAB_LF_RUNNER_H
#define TRACELAB_LF_RUNNER_H

// tracelab-linefollower: one run of the line-follower benchmark, the loop of
// line_follower_simulator's runner.run() --
//
//   u = 0
//   loop: stop at max_time; data = step_simulation(u) (None: finished);
//         u = controller.step(data.wheels_speed, data.future_points); log
//
// -- plus the per-step log (run_logger.py columns where they exist here) and
// runner.metrics(). No Qt: the window (simwindow.h) only drives tick().

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "lab/benchmarks/linefollower/plant/linefollowerplant.h"

#ifndef TRACELAB_LF_NO_CONTROLLERS
#include "lfcontroller.h"
#endif

namespace tracelab::lf {

struct RunnerConfig {
    TrackParams track;
    PlantParams plant;
    int fps = 80;
    double maxTime = 0.0;  // s, 0 = until the end of the track
    std::string controllerType = "qlpv";  // "qlpv" | "gpc"
    int horizon = 45;
};

// One logged step (runner.log_step()).
struct RunRow {
    double t, x, y, heading, speed, omega, wL, wR, uL, uR, perturbation, lateralError, coverage;
    double refX1, refY1, refXN, refYN;  // future points 1 and N, car frame (raw, not the controller's ref)
    double thetaPred1, xPred1, yPred1, thetaPredN, xPredN, yPredN;
    double solveMs;
    int qp, qpIters;
};

class LineFollowerRunner {
public:
    explicit LineFollowerRunner(const RunnerConfig& config);
    ~LineFollowerRunner();

    const RunnerConfig& config() const { return m_config; }
    LineFollowerPlant& plant() { return *m_plant; }
    const LineFollowerPlant& plant() const { return *m_plant; }
    bool hasController() const;
    std::string controllerName() const;

    // One sample. False when the run is over (see stopReason()).
    bool tick();
    // Ends the run from outside ("esc", "quit").
    void stop(const std::string& reason);
    bool running() const { return m_stopReason.empty(); }
    const std::string& stopReason() const { return m_stopReason; }

    const std::array<double, 2>& input() const { return m_u; }
    // The last prediction (car frame of the last tick), empty without one.
    const std::vector<double>& predictionX() const { return m_predX; }
    const std::vector<double>& predictionY() const { return m_predY; }
    int lastQps() const { return m_lastQps; }
    double lastSolveMs() const { return m_lastSolveMs; }

    const std::vector<RunRow>& rows() const { return m_rows; }
    // runner.metrics(), formatted one per line.
    std::string metricsText() const;
    bool saveLog(const std::string& path, std::string* error) const;

private:
    RunnerConfig m_config;
    std::unique_ptr<LineFollowerPlant> m_plant;
#ifndef TRACELAB_LF_NO_CONTROLLERS
    std::unique_ptr<Controller> m_controller;
#endif
    std::array<double, 2> m_u{0.0, 0.0};
    std::vector<double> m_predX;
    std::vector<double> m_predY;
    int m_lastQps = 0;
    double m_lastSolveMs = 0.0;
    long long m_steps = 0;
    std::string m_stopReason;
    std::vector<RunRow> m_rows;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_RUNNER_H
