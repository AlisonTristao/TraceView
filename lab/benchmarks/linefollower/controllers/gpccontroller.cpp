#include "gpccontroller.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "qlpvcontroller.h"
#include "qlpvmodel.h"

namespace tracelab::lf {

GpcController::GpcController(const ControllerConfig& config)
    : m_ts(config.ts),
      m_n(config.horizon),
      m_nu(config.gpc.controlHorizon),
      m_d0(config.gpc.d0),
      m_lookahead(config.gpc.thetaReference == "lookahead"),
      m_uMin(config.uMin),
      m_uMax(config.uMax),
      m_weightGrowth(config.gpc.weightGrowth),
      m_thetaReference(config.gpc.thetaReference),
      m_cTheta(config.car.wheelsRadius / config.car.wheelsDistance),
      m_cD(config.car.wheelsRadius / 2.0) {
    const int n = m_n;
    const int nu = m_nu;
    const MotorDiscretization motors = discretizeMotors(config.ts, config.car);

    // A~(z^-1) = (1 - a z^-1)(1 - z^-1)^2 = 1 + a1 z^-1 + a2 z^-2 + a3 z^-3
    // (np.polymul([1, -a], [1, -2, 1])[1:]).
    std::array<Eigen::MatrixXd, 2> toeplitz;
    for (int w = 0; w < 2; ++w) {
        const double alpha = motors.alpha[size_t(w)];
        const Eigen::Vector3d a(-2.0 - alpha, 1.0 + 2.0 * alpha, -alpha);
        m_f[size_t(w)] = freeMatrix(a);
        const Eigen::VectorXd g = stepResponse(a, config.ts * motors.beta[size_t(w)]);
        // G[i:, i] = g[:N - i] (gpc.py _toeplitz).
        Eigen::MatrixXd t = Eigen::MatrixXd::Zero(n, nu);
        for (int i = 0; i < std::min(nu, n); ++i) {
            t.col(i).tail(n - i) = g.head(n - i);
        }
        toeplitz[size_t(w)] = t;
    }

    // [[c_theta G_l, -c_theta G_r], [c_d G_l, c_d G_r]]
    m_g.resize(2 * n, 2 * nu);
    m_g << m_cTheta * toeplitz[0], -m_cTheta * toeplitz[1], m_cD * toeplitz[0], m_cD * toeplitz[1];

    // Exponential weighting along the horizon, q / N and lambda / Nu.
    Eigen::VectorXd qDiag(2 * n);
    for (int j = 0; j < n; ++j) {
        const double profile =
            std::pow(config.gpc.weightGrowth, double(j) / double(std::max(n - 1, 1)));
        qDiag(j) = profile * config.gpc.qTheta / n;
        qDiag(n + j) = profile * config.gpc.qDistance / n;
    }
    const Eigen::MatrixXd gtq = m_g.transpose() * qDiag.asDiagonal();
    const Eigen::MatrixXd lhs =
        gtq * m_g + Eigen::MatrixXd::Identity(2 * nu, 2 * nu) * (config.gpc.lamb / nu);
    // K = solve(G'QG + R, G'Q) -- LU, as numpy.linalg.solve.
    m_k = lhs.partialPivLu().solve(gtq);
    reset();
}

Eigen::MatrixXd GpcController::freeMatrix(const Eigen::Vector3d& a) const {
    // phi_f(k+j) = F[j] . [phi(k), phi(k-1), phi(k-2)] (gpc.py _free_matrix).
    Eigen::MatrixXd f(m_n, 3);
    Eigen::Vector3d h0(1, 0, 0), h1(0, 1, 0), h2(0, 0, 1);
    for (int j = 0; j < m_n; ++j) {
        const Eigen::Vector3d next = -(a(0) * h0 + a(1) * h1 + a(2) * h2);
        f.row(j) = next.transpose();
        h2 = h1;
        h1 = h0;
        h0 = next;
    }
    return f;
}

Eigen::VectorXd GpcController::stepResponse(const Eigen::Vector3d& a, double b) const {
    // phi(k+1) = -a1 phi(k) - a2 phi(k-1) - a3 phi(k-2) + b Delta u(k), Delta u = 1 at k = 0.
    Eigen::VectorXd g(m_n);
    double p0 = 0.0, p1 = 0.0, p2 = 0.0;
    for (int j = 0; j < m_n; ++j) {
        const double next = -(a(0) * p0 + a(1) * p1 + a(2) * p2) + (j == 0 ? b : 0.0);
        g(j) = next;
        p2 = p1;
        p1 = p0;
        p0 = next;
    }
    return g;
}

std::string GpcController::name() const {
    // f"GPC (N={horizon}, Nu={control_horizon}, {theta_reference}{growth})"
    std::string growth;
    if (m_weightGrowth != 1.0) {
        char buf[48];
        std::snprintf(buf, sizeof buf, ", g=%g", m_weightGrowth);
        growth = buf;
    }
    return "GPC (N=" + std::to_string(m_n) + ", Nu=" + std::to_string(m_nu) + ", " +
           m_thetaReference + growth + ")";
}

void GpcController::reset() {
    m_u.setZero();
    m_phi.setZero();
    m_hasStep = false;
    m_free.resize(0);
    m_dU.resize(0);
    m_ref.resize(0, 0);
    m_stats = {};
}

std::array<double, 2> GpcController::step(const std::array<double, 2>& wheelSpeedsRadS,
                                          const std::vector<CarPoint>& futurePoints) {
    if (int(futurePoints.size()) != m_n) {
        throw std::invalid_argument("GpcController::step: futurePoints must hold horizon points");
    }
    const auto t0 = std::chrono::steady_clock::now();
    const int n = m_n;

    // Integrate the wheel angles, written relative to phi(k) (the outputs are
    // reset at each instant).
    const Eigen::Vector2d w(wheelSpeedsRadS[0], wheelSpeedsRadS[1]);
    const Eigen::Vector2d phiK = m_phi.col(0) + w * m_ts;
    Eigen::Matrix<double, 2, 3> phi;
    phi << phiK, m_phi.col(0), m_phi.col(1);
    m_phi = phi.colwise() - phiK;

    // Free response of each wheel (N), then [theta; d].
    const Eigen::VectorXd fL = m_f[0] * m_phi.row(0).transpose();
    const Eigen::VectorXd fR = m_f[1] * m_phi.row(1).transpose();
    m_free.resize(2 * n);
    m_free << m_cTheta * (fL - fR), m_cD * (fL + fR);

    // Reference [theta_r; d_r] (gpc.py reference).
    m_ref = trackReference(futurePoints);
    if (m_lookahead) {
        m_ref.col(0) = m_ref.col(1) / m_d0;
    }
    Eigen::VectorXd r(2 * n);
    r << m_ref.col(0), m_ref.col(2);

    m_dU = m_k * (r - m_free);
    m_u(0) = std::clamp(m_u(0) + m_dU(0), m_uMin, m_uMax);
    m_u(1) = std::clamp(m_u(1) + m_dU(m_nu), m_uMin, m_uMax);
    m_hasStep = true;

    m_stats = {};
    m_stats.solveMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return {m_u(0), m_u(1)};
}

Prediction GpcController::prediction() const {
    // theta and d from the GPC prediction (free + forced); X, Y integrated
    // from them only for visualization (gpc.py prediction).
    Prediction out;
    if (!m_hasStep) {
        return out;
    }
    const int n = m_n;
    const Eigen::VectorXd y = m_free + m_g * m_dU;
    double dPrev = 0.0, heading = 0.0, x = 0.0, yPos = 0.0;
    for (int j = 0; j < n; ++j) {
        const double ds = y(n + j) - dPrev;
        x += ds * std::sin(heading);
        yPos += ds * std::cos(heading);
        out.theta.push_back(y(j));
        out.x.push_back(x);
        out.y.push_back(yPos);
        dPrev = y(n + j);
        heading = y(j);
    }
    return out;
}

Prediction GpcController::reference() const {
    Prediction out;
    for (Eigen::Index j = 0; j < m_ref.rows(); ++j) {
        out.theta.push_back(m_ref(j, 0));
        out.x.push_back(m_ref(j, 1));
        out.y.push_back(m_ref(j, 2));
    }
    return out;
}

ControllerStats GpcController::stats() const {
    return m_stats;
}

}  // namespace tracelab::lf
