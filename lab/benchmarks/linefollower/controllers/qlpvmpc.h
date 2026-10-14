#ifndef TRACELAB_LF_QLPVMPC_H
#define TRACELAB_LF_QLPVMPC_H

// qLPV MPC (port of mpc/qlpv_mpc.py, QLPVMPC). For a given scheduling
// trajectory P_k the prediction is linear in U_k and the problem is a QP:
//
//   min  sum_{j=1}^{N} |y(k+j|k) - r(k+j)|^2_W(j)
//      + sum_{j=0}^{N-1} |u(k+j|k)|^2_R + |du(k+j|k)|^2_Rd
//   s.t. x(k+j+1|k) = A(rho(k+j|k)) x(k+j|k) + B(rho(k+j|k)) (u(k+j|k) + d)
//        u_min <= u(k+j|k) <= u_max
//
// W(j) = diag(Q) growth^(j/(N-1)) for j < N-1 and diag(Q_N) (unscaled) at
// the last step, unless the caller passes its own stage weights (contouring).

#include <Eigen/Dense>
#include <array>
#include <memory>
#include <vector>

#include "qlpvmodel.h"
#include "scheduling.h"

namespace tracelab::lf {

// w_j = growth^(j / (N-1)), j = 0..N-1: w_0 = 1, the last step weights
// `growth` times the first; all ones for N < 2 or growth == 1
// (qlpv_mpc.py horizon_profile).
Eigen::VectorXd horizonProfile(int horizon, double growth);

struct QlpvMpcStats {
    int qps = 0;
    int qpIterations = 0;
    int fallbacks = 0;  // QPs that needed the projected-gradient fallback
    double rhoError = 0.0;
    double solveMs = 0.0;
};

class QlpvMpc {
public:
    QlpvMpc(const QlpvModel& model, int horizon, const std::array<double, 3>& q,
            const std::array<double, 2>& r, const std::array<double, 2>& rd,
            const std::array<double, 3>& qN, double uMin, double uMax,
            std::unique_ptr<Scheduling> scheduling, double weightGrowth);

    void reset();

    // One sample (qlpv_mpc.py QLPVMPC.step).
    //   x0     x(k) in the current frame
    //   ref    (N, 3) reference [theta, X, Y] for y(k+1), ..., y(k+N)
    //   d      input disturbance
    //   frame  pose of the current frame in the previous one, or nullptr
    //   w      N stage weights (3x3) for this sample, or nullptr for the
    //          diag(Q)/diag(Q_N) default
    // Returns u(k).
    QlpvModel::Input step(const QlpvModel::State& x0, const Eigen::MatrixXd& ref,
                          const QlpvModel::Input& d, const Eigen::Vector3d* frame,
                          const std::vector<Eigen::Matrix3d>* w);

    // Lifted prediction X = free + Gamma U for the scheduling trajectory P:
    // free (N, nx), gamma (N nx, N nu), block row j = x(k+j+1|k)
    // (qlpv_mpc.py predict).
    void predict(const Eigen::MatrixXd& p, const QlpvModel::State& x0, const QlpvModel::Input& d,
                 Eigen::MatrixXd* free, Eigen::MatrixXd* gamma) const;

    int horizon() const {
        return m_n;
    }
    const Scheduling& scheduling() const {
        return *m_scheduling;
    }
    bool hasSolution() const {
        return m_hasSolution;
    }
    // Predicted states (N+1, nx), scheduling (N, nrho), inputs (N nu).
    const Eigen::MatrixXd& states() const {
        return m_s;
    }
    const Eigen::MatrixXd& schedulingTrajectory() const {
        return m_p;
    }
    const Eigen::VectorXd& inputs() const {
        return m_u;
    }
    const QlpvMpcStats& stats() const {
        return m_stats;
    }

private:
    struct QpSolution {
        Eigen::VectorXd u;
        Eigen::MatrixXd s;
        int iterations = 0;
        bool fallback = false;
    };
    // qlpv_mpc.py solve_qp.
    QpSolution solveQp(const Eigen::MatrixXd& p, const QlpvModel::State& x0,
                       const Eigen::MatrixXd& ref, const QlpvModel::Input& d,
                       const Eigen::VectorXd* u0, const std::vector<Eigen::Matrix3d>& w) const;

    QlpvModel m_model;
    int m_n;
    std::unique_ptr<Scheduling> m_scheduling;
    std::vector<Eigen::Matrix3d> m_w;  // default stage weights
    Eigen::VectorXd m_profile;         // exponential profile, last entry 1
    Eigen::MatrixXd m_hU;              // diag(R) + D' diag(Rd) D
    Eigen::MatrixXd m_dtRd;            // D' diag(Rd)
    Eigen::VectorXd m_lb;
    Eigen::VectorXd m_ub;

    QlpvModel::Input m_uPrev;
    bool m_hasSolution = false;
    Eigen::MatrixXd m_s;
    Eigen::MatrixXd m_p;
    Eigen::VectorXd m_u;
    QlpvMpcStats m_stats;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_QLPVMPC_H
