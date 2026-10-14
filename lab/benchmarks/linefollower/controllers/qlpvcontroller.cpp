#include "qlpvcontroller.h"

#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace tracelab::lf {

Eigen::VectorXd gradient(const Eigen::VectorXd& f) {
    const Eigen::Index n = f.size();
    Eigen::VectorXd df(n);
    df(0) = f(1) - f(0);
    df(n - 1) = f(n - 1) - f(n - 2);
    for (Eigen::Index i = 1; i < n - 1; ++i) {
        df(i) = (f(i + 1) - f(i - 1)) / 2.0;
    }
    return df;
}

Eigen::MatrixXd trackReference(const std::vector<CarPoint>& points) {
    const Eigen::Index n = Eigen::Index(points.size());
    Eigen::VectorXd x(n), y(n);
    for (Eigen::Index j = 0; j < n; ++j) {
        x(j) = points[size_t(j)].x;
        y(j) = points[size_t(j)].y;
    }
    const Eigen::VectorXd dx = gradient(x);
    const Eigen::VectorXd dy = gradient(y);
    Eigen::MatrixXd ref(n, 3);
    for (Eigen::Index j = 0; j < n; ++j) {
        ref(j, 0) = std::atan2(dx(j), dy(j));
        ref(j, 1) = x(j);
        ref(j, 2) = y(j);
    }
    return ref;
}

std::vector<Eigen::Matrix3d> contouringWeights(const Eigen::MatrixXd& ref,
                                               const std::array<double, 3>& q,
                                               const std::array<double, 3>& qN) {
    const Eigen::Index n = ref.rows();
    std::vector<Eigen::Matrix3d> w(static_cast<size_t>(n));
    for (Eigen::Index j = 0; j < n; ++j) {
        const std::array<double, 3>& qj = j == n - 1 ? qN : q;
        const double s = std::sin(ref(j, 0));
        const double c = std::cos(ref(j, 0));
        const Eigen::Vector2d normal(c, -s);
        const Eigen::Vector2d tangent(s, c);
        Eigen::Matrix3d& wj = w[size_t(j)];
        wj.setZero();
        wj(0, 0) = qj[0];
        wj.bottomRightCorner<2, 2>() =
            qj[1] * normal * normal.transpose() + qj[2] * tangent * tangent.transpose();
    }
    return w;
}

QlpvController::QlpvController(const ControllerConfig& config, Embedding embedding,
                               std::unique_ptr<Scheduling> scheduling)
    : m_model(config.ts, config.car, embedding),
      m_mpc(m_model, config.horizon, config.qlpv.q, config.qlpv.r, config.qlpv.rd, config.qlpv.qN,
            config.uMin, config.uMax, std::move(scheduling), config.qlpv.weightGrowth),
      m_params(config.qlpv),
      m_n(config.horizon) {
    reset();
}

std::string QlpvController::name() const {
    // f"qLPV MPC ({embedding}, {scheduling}{growth})"
    std::string growth;
    if (m_params.weightGrowth != 1.0) {
        char buf[48];
        std::snprintf(buf, sizeof buf, ", g=%g", m_params.weightGrowth);
        growth = buf;
    }
    return std::string("qLPV MPC (") + m_model.embeddingName() + ", " + m_mpc.scheduling().name() +
           growth + ")";
}

void QlpvController::reset() {
    m_mpc.reset();
    m_u.setZero();
    m_wPrev.setZero();
    m_d.setZero();
    m_ref.resize(0, 0);
}

std::array<double, 2> QlpvController::step(const std::array<double, 2>& wheelSpeedsRadS,
                                           const std::vector<CarPoint>& futurePoints) {
    if (int(futurePoints.size()) != m_n) {
        throw std::invalid_argument("QlpvController::step: futurePoints must hold horizon points");
    }
    const QlpvModel::Input w(wheelSpeedsRadS[0], wheelSpeedsRadS[1]);

    // Input disturbance from the last wheel step, low-pass filtered.
    const QlpvModel::Input dNow = m_model.inputDisturbance(m_wPrev, w, m_u);
    m_d += m_params.disturbanceFilter * (dNow - m_d);
    m_wPrev = w;

    m_ref = trackReference(futurePoints);
    std::vector<Eigen::Matrix3d> weights;
    if (m_params.contouring) {
        weights = contouringWeights(m_ref, m_params.q, m_params.qN);
    }
    const Eigen::Vector3d frame = m_model.frameShift(wheelSpeedsRadS);
    m_u = m_mpc.step(m_model.state(wheelSpeedsRadS), m_ref, m_d, &frame,
                     m_params.contouring ? &weights : nullptr);
    return {m_u(0), m_u(1)};
}

Prediction QlpvController::prediction() const {
    // S[1:, (theta, X, Y)]
    Prediction out;
    if (!m_mpc.hasSolution()) {
        return out;
    }
    const Eigen::MatrixXd& s = m_mpc.states();
    for (Eigen::Index j = 1; j < s.rows(); ++j) {
        out.theta.push_back(s(j, 2));
        out.x.push_back(s(j, 3));
        out.y.push_back(s(j, 4));
    }
    return out;
}

Prediction QlpvController::reference() const {
    Prediction out;
    for (Eigen::Index j = 0; j < m_ref.rows(); ++j) {
        out.theta.push_back(m_ref(j, 0));
        out.x.push_back(m_ref(j, 1));
        out.y.push_back(m_ref(j, 2));
    }
    return out;
}

std::vector<std::array<double, 2>> QlpvController::schedulingTrajectory() const {
    std::vector<std::array<double, 2>> out;
    const Eigen::MatrixXd& p = m_mpc.schedulingTrajectory();
    for (Eigen::Index j = 0; j < p.rows(); ++j) {
        out.push_back({p(j, 0), p(j, 1)});
    }
    return out;
}

ControllerStats QlpvController::stats() const {
    const QlpvMpcStats& s = m_mpc.stats();
    ControllerStats out;
    out.qps = s.qps;
    out.qpIterations = s.qpIterations;
    out.solveMs = s.solveMs;
    out.rhoError = s.rhoError;
    return out;
}

}  // namespace tracelab::lf
