#include "qlpvmpc.h"

#include <chrono>
#include <cmath>

#include "boxqp.h"

namespace tracelab::lf {

namespace {

constexpr int kNx = QlpvModel::kNx;
constexpr int kNu = QlpvModel::kNu;
constexpr int kNy = QlpvModel::kNy;
constexpr int kOut = QlpvModel::kOutput0;

Eigen::Matrix3d diag3(const std::array<double, 3>& v) {
    return Eigen::Vector3d(v[0], v[1], v[2]).asDiagonal();
}

}  // namespace

Eigen::VectorXd horizonProfile(int horizon, double growth) {
    if (horizon < 2 || growth == 1.0) {
        return Eigen::VectorXd::Ones(horizon);
    }
    Eigen::VectorXd w(horizon);
    for (int j = 0; j < horizon; ++j) {
        w(j) = std::pow(growth, double(j) / double(horizon - 1));
    }
    return w;
}

QlpvMpc::QlpvMpc(const QlpvModel& model, int horizon, const std::array<double, 3>& q,
                 const std::array<double, 2>& r, const std::array<double, 2>& rd,
                 const std::array<double, 3>& qN, double uMin, double uMax,
                 std::unique_ptr<Scheduling> scheduling, double weightGrowth)
    : m_model(model), m_n(horizon), m_scheduling(std::move(scheduling)) {
    const int n = horizon;
    const int nU = n * kNu;

    m_w.assign(size_t(n), diag3(q));
    m_w.back() = diag3(qN);
    // Stage scale: the exponential profile on steps 0..N-2; Q_N is not scaled.
    m_profile = horizonProfile(n, weightGrowth);
    m_profile(n - 1) = 1.0;

    // du = D U - e0, e0 = [u(k-1), 0, ..., 0]; D = I - (identity shifted down by nu).
    Eigen::VectorXd rVec(nU), rdVec(nU);
    for (int j = 0; j < n; ++j) {
        rVec.segment<kNu>(j * kNu) << r[0], r[1];
        rdVec.segment<kNu>(j * kNu) << rd[0], rd[1];
    }
    Eigen::MatrixXd dMat = Eigen::MatrixXd::Identity(nU, nU);
    for (int i = kNu; i < nU; ++i) {
        dMat(i, i - kNu) = -1.0;
    }
    m_dtRd = dMat.transpose() * rdVec.asDiagonal();
    m_hU = Eigen::MatrixXd(rVec.asDiagonal()) + m_dtRd * dMat;

    m_lb = Eigen::VectorXd::Constant(nU, uMin);
    m_ub = Eigen::VectorXd::Constant(nU, uMax);
    reset();
}

void QlpvMpc::reset() {
    m_uPrev.setZero();
    m_hasSolution = false;
    m_s.resize(0, 0);
    m_p.resize(0, 0);
    m_u.resize(0);
    m_stats = {};
}

void QlpvMpc::predict(const Eigen::MatrixXd& p, const QlpvModel::State& x0,
                      const QlpvModel::Input& d, Eigen::MatrixXd* free,
                      Eigen::MatrixXd* gamma) const {
    const int n = m_n;
    free->resize(n, kNx);
    gamma->setZero(n * kNx, n * kNu);
    QlpvModel::State x = x0;
    Eigen::MatrixXd g = Eigen::MatrixXd::Zero(kNx, n * kNu);
    QlpvModel::StateMatrix a;
    QlpvModel::InputMatrix b;
    for (int j = 0; j < n; ++j) {
        m_model.matrices(p.row(j).transpose(), &a, &b);
        // x(j+1) = A_j x(j) + B_j d  (free response), G_{j+1} = [A_j G_j, B_j].
        x = a * x + b * d;
        const int cols = j * kNu;
        if (cols > 0) {
            g.leftCols(cols) = (a * g.leftCols(cols)).eval();
        }
        g.middleCols(cols, kNu) = b;
        free->row(j) = x.transpose();
        gamma->middleRows(j * kNx, kNx) = g;
    }
}

QlpvMpc::QpSolution QlpvMpc::solveQp(const Eigen::MatrixXd& p, const QlpvModel::State& x0,
                                     const Eigen::MatrixXd& ref, const QlpvModel::Input& d,
                                     const Eigen::VectorXd* u0,
                                     const std::vector<Eigen::Matrix3d>& w) const {
    const int n = m_n;
    const int nU = n * kNu;
    Eigen::MatrixXd free, gamma;
    predict(p, x0, d, &free, &gamma);

    // Outputs y = [theta, X, Y]: Gy (N ny, N nu), err = free_y - ref, WG = W Gy.
    Eigen::MatrixXd gy(n * kNy, nU), wg(n * kNy, nU);
    Eigen::VectorXd err(n * kNy);
    for (int j = 0; j < n; ++j) {
        gy.middleRows(j * kNy, kNy) = gamma.middleRows(j * kNx + kOut, kNy);
        err.segment<kNy>(j * kNy) =
            free.row(j).segment<kNy>(kOut).transpose() - ref.row(j).transpose();
        wg.middleRows(j * kNy, kNy) = (m_profile(j) * w[size_t(j)]) * gy.middleRows(j * kNy, kNy);
    }

    Eigen::VectorXd e0 = Eigen::VectorXd::Zero(nU);
    e0.head<kNu>() = m_uPrev;
    const Eigen::MatrixXd h = gy.transpose() * wg + m_hU;
    const Eigen::VectorXd g = wg.transpose() * err - m_dtRd * e0;

    BoxQpResult qp = solveBoxQp(h, g, m_lb, m_ub, u0);

    QpSolution out;
    out.s.resize(n + 1, kNx);
    out.s.row(0) = x0.transpose();
    const Eigen::VectorXd forced = gamma * qp.x;
    for (int j = 0; j < n; ++j) {
        out.s.row(j + 1) = free.row(j) + forced.segment<kNx>(j * kNx).transpose();
    }
    out.u = std::move(qp.x);
    out.iterations = qp.iterations;
    out.fallback = qp.fallback;
    return out;
}

QlpvModel::Input QlpvMpc::step(const QlpvModel::State& x0, const Eigen::MatrixXd& ref,
                               const QlpvModel::Input& d, const Eigen::Vector3d* frame,
                               const std::vector<Eigen::Matrix3d>* w) {
    const auto t0 = std::chrono::steady_clock::now();
    const int n = m_n;
    const std::vector<Eigen::Matrix3d>& weights = w != nullptr ? *w : m_w;

    // Previous prediction written in the current frame.
    Eigen::MatrixXd sPrev;
    if (m_hasSolution) {
        sPrev = m_s;
        if (frame != nullptr) {
            m_model.realignStates(&sPrev, *frame);
        }
    }
    Eigen::MatrixXd p = m_scheduling->estimate(m_model, x0, m_hasSolution ? &sPrev : nullptr, n);

    // Warm start with the shifted previous solution (U[1:], U[-1]).
    Eigen::VectorXd u0;
    if (m_hasSolution) {
        u0.resize(n * kNu);
        u0.head((n - 1) * kNu) = m_u.tail((n - 1) * kNu);
        u0.tail<kNu>() = m_u.tail<kNu>();
    }

    QlpvMpcStats stats;
    QpSolution sol;
    while (true) {
        sol = solveQp(p, x0, ref, d, m_hasSolution || stats.qps > 0 ? &u0 : nullptr, weights);
        ++stats.qps;
        stats.qpIterations += sol.iterations;
        stats.fallbacks += sol.fallback ? 1 : 0;
        if (stats.qps >= m_scheduling->maxQp()) {
            break;
        }
        Eigen::MatrixXd pNext;
        if (!m_scheduling->update(m_model, x0, sol.s, p, &pNext)) {
            break;
        }
        p = std::move(pNext);
        u0 = sol.u;
    }

    // Mismatch between the scheduling used and the one of the predicted states.
    stats.rhoError = (m_model.fRhoSequence(sol.s) - p).cwiseAbs().maxCoeff();

    m_s = std::move(sol.s);
    m_p = std::move(p);
    m_u = std::move(sol.u);
    m_hasSolution = true;
    m_uPrev = m_u.head<kNu>();
    stats.solveMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    m_stats = stats;
    return m_uPrev;
}

}  // namespace tracelab::lf
