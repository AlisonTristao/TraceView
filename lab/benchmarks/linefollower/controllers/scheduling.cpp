#include "scheduling.h"

#include <cstdio>

namespace tracelab::lf {

bool Scheduling::update(const QlpvModel& /*model*/, const QlpvModel::State& /*x0*/,
                        const Eigen::MatrixXd& /*s*/, const Eigen::MatrixXd& /*p*/,
                        Eigen::MatrixXd* /*pNext*/) const {
    return false;
}

Eigen::MatrixXd previousSequence(const QlpvModel::State& x0, const Eigen::MatrixXd& sPrev, int n) {
    // np.vstack((x0, S_prev[2:N + 1], S_prev[N:N + 1])): N + 1 rows.
    Eigen::MatrixXd seq(n + 1, QlpvModel::kNx);
    seq.row(0) = x0.transpose();
    seq.middleRows(1, n - 1) = sPrev.middleRows(2, n - 1);
    seq.row(n) = sPrev.row(n);
    return seq;
}

Eigen::MatrixXd FrozenScheduling::estimate(const QlpvModel& model, const QlpvModel::State& x0,
                                           const Eigen::MatrixXd* /*sPrev*/, int n) const {
    QlpvModel::Pair z;
    z << x0, x0;
    return model.fRho(z).transpose().replicate(n, 1);
}

Eigen::MatrixXd WarmStartScheduling::estimate(const QlpvModel& model, const QlpvModel::State& x0,
                                              const Eigen::MatrixXd* sPrev, int n) const {
    if (sPrev == nullptr) {
        return FrozenScheduling().estimate(model, x0, sPrev, n);
    }
    return model.fRhoSequence(previousSequence(x0, *sPrev, n));
}

Eigen::MatrixXd TaylorScheduling::estimate(const QlpvModel& model, const QlpvModel::State& x0,
                                           const Eigen::MatrixXd* sPrev, int n) const {
    if (sPrev == nullptr) {
        return FrozenScheduling().estimate(model, x0, sPrev, n);
    }
    const Eigen::MatrixXd seq = previousSequence(x0, *sPrev, n);
    // One f_rho, one jacobian, N-1 increments sigma (z(j) - z(j-1)).
    QlpvModel::Pair zPrev = QlpvModel::pair(seq, 0);
    const QlpvModel::Jacobian sigma = model.jacobianRho(zPrev);
    Eigen::MatrixXd p(n, QlpvModel::kNrho);
    QlpvModel::Rho rho = model.fRho(zPrev);
    p.row(0) = rho.transpose();
    for (int j = 1; j < n; ++j) {
        const QlpvModel::Pair z = QlpvModel::pair(seq, j);
        rho = model.saturateRho(rho + sigma * (z - zPrev));
        p.row(j) = rho.transpose();
        zPrev = z;
    }
    return p;
}

IterativeScheduling::IterativeScheduling(int lMax, double eps, std::unique_ptr<Scheduling> initial,
                                         double relaxation)
    : m_lMax(lMax), m_eps(eps), m_initial(std::move(initial)), m_relaxation(relaxation) {}

std::string IterativeScheduling::name() const {
    // f"iterative({initial.name}, l_max={l_max}, gamma={relaxation:g})"
    char gamma[32];
    std::snprintf(gamma, sizeof gamma, "%g", m_relaxation);
    return "iterative(" + m_initial->name() + ", l_max=" + std::to_string(m_lMax) +
           ", gamma=" + gamma + ")";
}

Eigen::MatrixXd IterativeScheduling::estimate(const QlpvModel& model, const QlpvModel::State& x0,
                                              const Eigen::MatrixXd* sPrev, int n) const {
    return m_initial->estimate(model, x0, sPrev, n);
}

bool IterativeScheduling::update(const QlpvModel& model, const QlpvModel::State& /*x0*/,
                                 const Eigen::MatrixXd& s, const Eigen::MatrixXd& p,
                                 Eigen::MatrixXd* pNext) const {
    const Eigen::MatrixXd step = model.fRhoSequence(s) - p;
    if (step.cwiseAbs().maxCoeff() < m_eps) {
        return false;
    }
    pNext->resize(p.rows(), p.cols());
    for (Eigen::Index j = 0; j < p.rows(); ++j) {
        const QlpvModel::Rho rho = p.row(j).transpose() + m_relaxation * step.row(j).transpose();
        pNext->row(j) = model.saturateRho(rho).transpose();
    }
    return true;
}

namespace {

std::unique_ptr<Scheduling> makeBasic(const std::string& name) {
    if (name == "frozen") {
        return std::make_unique<FrozenScheduling>();
    }
    if (name == "warm_start") {
        return std::make_unique<WarmStartScheduling>();
    }
    if (name == "taylor") {
        return std::make_unique<TaylorScheduling>();
    }
    return nullptr;
}

}  // namespace

std::unique_ptr<Scheduling> makeScheduling(const std::string& name, const QlpvParams& params) {
    if (name != "iterative") {
        return makeBasic(name);
    }
    // config.py offers only frozen/warm_start/taylor as the initial estimate.
    // (Python's make_scheduling("iterative") would build a default Iterative
    // whose estimate() is the frozen one; that case is rejected here.)
    std::unique_ptr<Scheduling> initial = makeBasic(params.initial);
    if (initial == nullptr) {
        return nullptr;
    }
    return std::make_unique<IterativeScheduling>(params.lMax, params.eps, std::move(initial),
                                                 params.relaxation);
}

}  // namespace tracelab::lf
