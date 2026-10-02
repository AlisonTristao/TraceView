#include "lfrunner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>

namespace tracelab::lf {

namespace {

std::string number(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.17g", value);
    return buffer;
}

}  // namespace

LineFollowerRunner::LineFollowerRunner(const RunnerConfig& config) : m_config(config) {
    m_config.plant.car.ts = 1.0 / config.fps;
    m_config.plant.progress.futureCount = config.horizon;
    m_plant = std::make_unique<LineFollowerPlant>(generateTrack(m_config.track), m_config.plant);
#ifndef TRACELAB_LF_NO_CONTROLLERS
    ControllerConfig cc;
    cc.type = config.controllerType;
    cc.ts = m_config.plant.car.ts;
    cc.horizon = config.horizon;
    const CarDynamicsParams& car = m_config.plant.car;
    cc.car.wheelsRadius = car.wheelsRadius;
    cc.car.wheelsDistance = car.wheelsDistance;
    cc.car.wheelsRpm = car.wheelsRpm;
    cc.car.keLeft = car.keLeft;
    cc.car.keRight = car.keRight;
    cc.car.accommodationTimeLeft = car.accommodationTimeLeft;
    cc.car.accommodationTimeRight = car.accommodationTimeRight;
    m_controller = makeController(cc);
#endif
}

LineFollowerRunner::~LineFollowerRunner() = default;

bool LineFollowerRunner::hasController() const {
#ifndef TRACELAB_LF_NO_CONTROLLERS
    return m_controller != nullptr;
#else
    return false;
#endif
}

std::string LineFollowerRunner::controllerName() const {
#ifndef TRACELAB_LF_NO_CONTROLLERS
    if (m_controller) {
        return m_controller->name();
    }
#endif
    return "no controller (u = 0)";
}

void LineFollowerRunner::stop(const std::string& reason) {
    if (m_stopReason.empty()) {
        m_stopReason = reason;
    }
}

bool LineFollowerRunner::tick() {
    if (!running()) {
        return false;
    }
    const long long maxSteps =
        m_config.maxTime > 0 ? (long long)(m_config.maxTime * m_config.fps) : -1;
    if (maxSteps >= 0 && m_steps >= maxSteps) {
        stop("time");
        return false;
    }
    if (!m_plant->step(m_u[0], m_u[1])) {
        stop("finished");
        return false;
    }
    const StepData& data = m_plant->data();

#ifndef TRACELAB_LF_NO_CONTROLLERS
    if (m_controller) {
        std::vector<CarPoint> future;
        future.reserve(data.futurePoints.size());
        for (const CarFramePoint& p : data.futurePoints) {
            future.push_back({p.x, p.y});
        }
        m_u = m_controller->step({data.wheelSpeedLeft, data.wheelSpeedRight}, future);
        const Prediction prediction = m_controller->prediction();
        m_predX = prediction.x;
        m_predY = prediction.y;
        const ControllerStats stats = m_controller->stats();
        m_lastQps = stats.qps;
        m_lastSolveMs = stats.solveMs;
    }
#endif

    const Pose& pose = m_plant->pose();
    RunRow row{};
    row.t = m_plant->time();
    row.x = pose.x;
    row.y = pose.y;
    row.heading = wrapAngle(pose.theta);
    row.speed = data.speed;
    row.omega = data.omega;
    row.wL = data.wheelSpeedLeft;
    row.wR = data.wheelSpeedRight;
    row.uL = m_u[0];
    row.uR = m_u[1];
    row.perturbation = m_plant->perturbation();
    row.lateralError = data.lateralError;
    row.coverage = m_plant->progress().coverage();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<CarFramePoint>& fp = data.futurePoints;
    row.refX1 = fp.empty() ? nan : fp.front().x;
    row.refY1 = fp.empty() ? nan : fp.front().y;
    row.refXN = fp.empty() ? nan : fp.back().x;
    row.refYN = fp.empty() ? nan : fp.back().y;
    row.thetaPred1 = row.xPred1 = row.yPred1 = row.thetaPredN = row.xPredN = row.yPredN = nan;
#ifndef TRACELAB_LF_NO_CONTROLLERS
    if (m_controller) {
        const Prediction prediction = m_controller->prediction();
        if (!prediction.x.empty() && prediction.x.size() == prediction.y.size()) {
            row.xPred1 = prediction.x.front();
            row.yPred1 = prediction.y.front();
            row.xPredN = prediction.x.back();
            row.yPredN = prediction.y.back();
        }
        if (!prediction.theta.empty()) {
            row.thetaPred1 = prediction.theta.front();
            row.thetaPredN = prediction.theta.back();
        }
        const ControllerStats stats = m_controller->stats();
        row.solveMs = stats.solveMs;
        row.qp = stats.qps;
        row.qpIters = stats.qpIterations;
    }
#endif
    m_rows.push_back(row);
    ++m_steps;
    return true;
}

std::string LineFollowerRunner::metricsText() const {
    std::ostringstream out;
    out << "controller: " << controllerName() << "\n";
    const bool finished = m_stopReason == "finished";
    out << "finished: " << (finished ? "True" : "False") << "\n";
    out << "stop_reason: " << (m_stopReason.empty() ? "running" : m_stopReason) << "\n";
    if (m_rows.empty()) {
        return out.str();
    }
    double lat2 = 0.0;
    double latMax = 0.0;
    double speed = 0.0;
    double solve = 0.0;
    double solveMax = 0.0;
    double qp = 0.0;
    for (const RunRow& row : m_rows) {
        const double lat = std::fabs(row.lateralError);
        lat2 += lat * lat;
        latMax = std::max(latMax, lat);
        speed += row.speed;
        solve += row.solveMs;
        solveMax = std::max(solveMax, row.solveMs);
        qp += row.qp;
    }
    const double n = double(m_rows.size());
    char buffer[512];
    std::snprintf(buffer, sizeof buffer,
                  "time_s: %.3f\ncoverage_%%: %.3f\nlat_rms_cm: %.3f\nlat_max_cm: %.3f\n"
                  "speed_mean_m_s: %.3f\nsolve_mean_ms: %.3f\nsolve_max_ms: %.3f\nqp_mean: %.3f\n",
                  n / m_config.fps, finished ? 100.0 : m_rows.back().coverage,
                  std::sqrt(lat2 / n) * 100.0, latMax * 100.0, speed / n, solve / n, solveMax,
                  qp / n);
    out << buffer;
    return out.str();
}

bool LineFollowerRunner::saveLog(const std::string& path, std::string* error) const {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error) {
            *error = "could not write " + path;
        }
        return false;
    }
    std::fputs(
        "t,x,y,heading,speed,omega,w_l,w_r,u_l,u_r,perturbation,lateral_error,coverage,"
        "X_ref_1,Y_ref_1,X_ref_N,Y_ref_N,theta_pred_1,X_pred_1,Y_pred_1,theta_pred_N,X_pred_N,"
        "Y_pred_N,solve_time_ms,qp,qp_iters\n",
        file);
    for (const RunRow& r : m_rows) {
        const double values[] = {r.t,          r.x,          r.y,         r.heading,    r.speed,
                                 r.omega,      r.wL,         r.wR,        r.uL,         r.uR,
                                 r.perturbation, r.lateralError, r.coverage, r.refX1,   r.refY1,
                                 r.refXN,      r.refYN,      r.thetaPred1, r.xPred1,    r.yPred1,
                                 r.thetaPredN, r.xPredN,     r.yPredN,    r.solveMs};
        std::string line;
        for (double v : values) {
            line += (std::isnan(v) ? std::string("nan") : number(v)) + ",";
        }
        line += std::to_string(r.qp) + "," + std::to_string(r.qpIters) + "\n";
        std::fputs(line.c_str(), file);
    }
    std::fclose(file);

    // <name>_track.csv, as run_logger.py writes it.
    std::string base = path;
    const std::size_t dot = base.find_last_of('.');
    const std::size_t slash = base.find_last_of("/\\");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        base.resize(dot);
    }
    const std::string trackPath = base + "_track.csv";
    FILE* trackFile = std::fopen(trackPath.c_str(), "wb");
    if (!trackFile) {
        if (error) {
            *error = "could not write " + trackPath;
        }
        return false;
    }
    std::fputs("x,y\n", trackFile);
    const Track& track = m_plant->track();
    for (std::size_t i = 0; i < track.size(); ++i) {
        std::fputs((number(track.x[i]) + "," + number(track.y[i]) + "\n").c_str(), trackFile);
    }
    std::fclose(trackFile);
    return true;
}

}  // namespace tracelab::lf
