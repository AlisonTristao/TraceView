#ifndef TRACELAB_LF_SCHEDULING_H
#define TRACELAB_LF_SCHEDULING_H

// Scheduling trajectory estimation P_k = col(rho(k), rho(k+1|k), ...,
// rho(k+N-1|k)) (port of mpc/scheduling.py).
//
// rho(k+j|k) = f_rho(x(k+j), x(k+j+1)), so it needs an estimate of the state
// sequence x(k), ..., x(k+N). Every strategy receives the instant k already
// written in the current car frame:
//   x0     measured state x(k)
//   sPrev  previous predicted states (x(k-1|k-1), ..., x(k-1+N|k-1)),
//          (N+1, nx), or nullptr on the first sample
// P is (N, nrho), one row per step.

#include <Eigen/Dense>
#include <memory>
#include <string>

#include "lfcontroller.h"
#include "qlpvmodel.h"

namespace tracelab::lf {

class Scheduling {
public:
    virtual ~Scheduling() = default;

    virtual std::string name() const = 0;
    // QPs per sample (> 1 only for the iterative strategy).
    virtual int maxQp() const {
        return 1;
    }
    virtual Eigen::MatrixXd estimate(const QlpvModel& model, const QlpvModel::State& x0,
                                     const Eigen::MatrixXd* sPrev, int n) const = 0;
    // Called after each QP with its predicted states s (N+1, nx): true and
    // *pNext = a new estimate to solve again, false to stop.
    virtual bool update(const QlpvModel& model, const QlpvModel::State& x0,
                        const Eigen::MatrixXd& s, const Eigen::MatrixXd& p,
                        Eigen::MatrixXd* pNext) const;
};

// rho(k+j|k) = f_rho(x(k), x(k)) for every j (scheduling.py Frozen).
class FrozenScheduling : public Scheduling {
public:
    std::string name() const override {
        return "frozen";
    }
    Eigen::MatrixXd estimate(const QlpvModel& model, const QlpvModel::State& x0,
                             const Eigen::MatrixXd* sPrev, int n) const override;
};

// rho(k+j|k) = f_rho(x*(k+j|k-1), x*(k+j+1|k-1)) from the previous optimal
// trajectory, x(k) measured, last state repeated (scheduling.py WarmStart).
class WarmStartScheduling : public Scheduling {
public:
    std::string name() const override {
        return "warm_start";
    }
    Eigen::MatrixXd estimate(const QlpvModel& model, const QlpvModel::State& x0,
                             const Eigen::MatrixXd* sPrev, int n) const override;
};

// Recursive first-order extrapolation around z(k) (scheduling.py Taylor):
//   rho(k|k) = f_rho(z(k)),  sigma = d f_rho/dz at z(k)
//   rho(k+j|k) = sat(rho(k+j-1|k) + sigma (z(k+j) - z(k+j-1)))
class TaylorScheduling : public Scheduling {
public:
    std::string name() const override {
        return "taylor";
    }
    Eigen::MatrixXd estimate(const QlpvModel& model, const QlpvModel::State& x0,
                             const Eigen::MatrixXd* sPrev, int n) const override;
};

// P^0 from the initial strategy, then the QP is solved again with
//   P^{l+1} = sat(P^l + gamma (f_rho(z^l) - P^l))
// until ||f_rho(z^l) - P^l||_inf < eps or lMax QPs (scheduling.py Iterative).
class IterativeScheduling : public Scheduling {
public:
    IterativeScheduling(int lMax, double eps, std::unique_ptr<Scheduling> initial,
                        double relaxation);

    std::string name() const override;
    int maxQp() const override {
        return m_lMax;
    }
    Eigen::MatrixXd estimate(const QlpvModel& model, const QlpvModel::State& x0,
                             const Eigen::MatrixXd* sPrev, int n) const override;
    bool update(const QlpvModel& model, const QlpvModel::State& x0, const Eigen::MatrixXd& s,
                const Eigen::MatrixXd& p, Eigen::MatrixXd* pNext) const override;

private:
    int m_lMax;
    double m_eps;
    std::unique_ptr<Scheduling> m_initial;
    double m_relaxation;
};

// (x(k), x*(k+1|k-1), ..., x*(k+N-1|k-1), x*(k+N-1|k-1)): the measured state
// and the shifted previous prediction (scheduling.py previous_sequence).
Eigen::MatrixXd previousSequence(const QlpvModel::State& x0, const Eigen::MatrixXd& sPrev, int n);

// scheduling.py make_scheduling: "frozen" | "warm_start" | "taylor" |
// "iterative" (with params.lMax/eps/initial/relaxation); nullptr otherwise.
std::unique_ptr<Scheduling> makeScheduling(const std::string& name, const QlpvParams& params);

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_SCHEDULING_H
