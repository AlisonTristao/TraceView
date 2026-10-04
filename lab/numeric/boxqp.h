#ifndef TRACELAB_NUMERIC_BOXQP_H
#define TRACELAB_NUMERIC_BOXQP_H

// Box-constrained QP
//
//   min 1/2 x'Hx + g'x   s.t.  lb <= x <= ub     (H positive definite)
//
// port of line_follower_simulator's mpc/qlpv_mpc.py solve_box_qp. Generic:
// the line follower's MPC uses it, and code blocks get it as tl_qp_box()
// (lab/code/include/tracelab.h).

#include <Eigen/Dense>

namespace tracelab::numeric {

// solve_box_qp's max_iter default.
inline constexpr int kBoxQpMaxIter = 50;

struct BoxQpResult {
    Eigen::VectorXd x;
    // Active-set iterations; kBoxQpMaxIter when the fallback ran (as Python).
    int iterations = 0;
    // The active set did not settle in maxIter iterations and the projected
    // gradient fallback produced x.
    bool fallback = false;
};

// Primal-dual active set (a semismooth Newton method on
// x = clip(x - D^-1 (Hx + g)), D = diag(H)), warm started by x0 (clipped).
// Stops as soon as the predicted active set repeats -- x is then the exact
// KKT point of that set. If it has not settled after maxIter iterations it
// falls back to solveBoxQpProjected() from clip(x).
BoxQpResult solveBoxQp(const Eigen::MatrixXd& h, const Eigen::VectorXd& g,
                       const Eigen::VectorXd& lb, const Eigen::VectorXd& ub,
                       const Eigen::VectorXd* x0 = nullptr, int maxIter = kBoxQpMaxIter);

// The fallback, on the same objective and bounds.
//
// DEVIATION from the Python: solve_box_qp falls back to
// scipy.optimize.minimize(method="L-BFGS-B", bounds=...) with scipy's
// defaults (pgtol 1e-5, maxiter 15000). There is no L-BFGS-B here; instead
// this runs FISTA (accelerated projected gradient, step 1/lambda_max(H),
// function-value adaptive restart so it is monotone) until the same
// stopping test as L-BFGS-B's pgtol -- ||clip(x - grad) - x||_inf <= 1e-5 --
// or 15000 iterations, then "polishes" once: the Newton step on the
// variables that are not held at a bound by a pushing gradient, kept only if
// it stays feasible and does not raise the objective. Both methods converge
// to the same unique minimizer (H positive definite); the iterates differ,
// so the result agrees with scipy's to the solvers' tolerances, not bitwise.
Eigen::VectorXd solveBoxQpProjected(const Eigen::MatrixXd& h, const Eigen::VectorXd& g,
                                    const Eigen::VectorXd& lb, const Eigen::VectorXd& ub,
                                    const Eigen::VectorXd& x0);

}  // namespace tracelab::numeric

#endif  // TRACELAB_NUMERIC_BOXQP_H
