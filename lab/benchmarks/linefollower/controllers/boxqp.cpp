#include "boxqp.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace tracelab::lf {

namespace {

// scipy L-BFGS-B defaults the fallback mirrors (see boxqp.h).
constexpr double kProjectedGradientTol = 1e-5;
constexpr int kFallbackMaxIter = 15000;

Eigen::VectorXd clip(const Eigen::VectorXd& x, const Eigen::VectorXd& lb,
                     const Eigen::VectorXd& ub) {
    return x.cwiseMax(lb).cwiseMin(ub);
}

double objective(const Eigen::MatrixXd& h, const Eigen::VectorXd& g, const Eigen::VectorXd& x) {
    return x.dot(0.5 * (h * x) + g);
}

double projectedGradientNorm(const Eigen::VectorXd& x, const Eigen::VectorXd& grad,
                             const Eigen::VectorXd& lb, const Eigen::VectorXd& ub) {
    return (clip(x - grad, lb, ub) - x).cwiseAbs().maxCoeff();
}

// x(free) = H(free, free)^-1 (-g(free) - H(free, bound) x(bound)), x(bound)
// as given (the Newton step of qlpv_mpc.py solve_box_qp). LU with partial
// pivoting, the algorithm behind numpy.linalg.solve (LAPACK gesv).
void solveFree(const Eigen::MatrixXd& h, const Eigen::VectorXd& g, const std::vector<int>& free,
               const std::vector<int>& bound, Eigen::VectorXd* x) {
    if (free.empty()) {
        return;
    }
    // Explicit gathers (plain loops rather than Eigen indexed views, whose
    // products trip a GCC 13 -Warray-bounds false positive).
    const int nf = int(free.size());
    Eigen::MatrixXd hff(nf, nf);
    Eigen::VectorXd rhs(nf);
    for (int i = 0; i < nf; ++i) {
        const int fi = free[size_t(i)];
        double r = -g(fi);
        for (int b : bound) {
            r -= h(fi, b) * (*x)(b);
        }
        rhs(i) = r;
        for (int k = 0; k < nf; ++k) {
            hff(i, k) = h(fi, free[size_t(k)]);
        }
    }
    const Eigen::VectorXd xFree = hff.partialPivLu().solve(rhs);
    for (int i = 0; i < nf; ++i) {
        (*x)(free[size_t(i)]) = xFree(i);
    }
}

}  // namespace

BoxQpResult solveBoxQp(const Eigen::MatrixXd& h, const Eigen::VectorXd& g,
                       const Eigen::VectorXd& lb, const Eigen::VectorXd& ub,
                       const Eigen::VectorXd* x0, int maxIter) {
    const Eigen::Index n = g.size();
    const Eigen::VectorXd d = h.diagonal();
    Eigen::VectorXd x = clip(x0 == nullptr ? Eigen::VectorXd(-g.cwiseQuotient(d)) : *x0, lb, ub);

    Eigen::Array<bool, Eigen::Dynamic, 1> lower(n), upper(n);
    bool haveSet = false;
    std::vector<int> free, bound;
    free.reserve(size_t(n));
    bound.reserve(size_t(n));
    for (int it = 1; it <= maxIter; ++it) {
        // Predicted active set from one diagonally scaled gradient step.
        const Eigen::VectorXd t = x - (h * x + g).cwiseQuotient(d);
        const Eigen::Array<bool, Eigen::Dynamic, 1> newLower = t.array() <= lb.array();
        const Eigen::Array<bool, Eigen::Dynamic, 1> newUpper = t.array() >= ub.array();
        if (haveSet && (newLower == lower).all() && (newUpper == upper).all()) {
            return {x, it - 1, false};
        }
        lower = newLower;
        upper = newUpper;
        haveSet = true;

        free.clear();
        bound.clear();
        for (int i = 0; i < int(n); ++i) {
            if (lower(i)) {
                x(i) = lb(i);
                bound.push_back(i);
            } else if (upper(i)) {
                x(i) = ub(i);
                bound.push_back(i);
            } else {
                x(i) = 0.0;
                free.push_back(i);
            }
        }
        solveFree(h, g, free, bound, &x);
    }

    return {solveBoxQpProjected(h, g, lb, ub, clip(x, lb, ub)), maxIter, true};
}

Eigen::VectorXd solveBoxQpProjected(const Eigen::MatrixXd& h, const Eigen::VectorXd& g,
                                    const Eigen::VectorXd& lb, const Eigen::VectorXd& ub,
                                    const Eigen::VectorXd& x0) {
    const Eigen::Index n = g.size();
    // Step 1/L, L = lambda_max of the symmetric part of H.
    const Eigen::MatrixXd hs = 0.5 * (h + h.transpose());
    const double lMax = Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd>(hs, Eigen::EigenvaluesOnly)
                            .eigenvalues()
                            .maxCoeff();
    const double step = lMax > 0.0 ? 1.0 / lMax : 1.0;

    Eigen::VectorXd x = clip(x0, lb, ub);
    Eigen::VectorXd y = x;
    double fx = objective(h, g, x);
    double t = 1.0;
    for (int k = 0; k < kFallbackMaxIter; ++k) {
        if (projectedGradientNorm(x, h * x + g, lb, ub) <= kProjectedGradientTol) {
            break;
        }
        const Eigen::VectorXd xNext = clip(y - step * (h * y + g), lb, ub);
        const double fNext = objective(h, g, xNext);
        if (fNext > fx) {
            // Adaptive restart: momentum overshot, restart from x (a plain
            // projected gradient step from x never increases f).
            t = 1.0;
            y = x;
            continue;
        }
        const double tNext = 0.5 * (1.0 + std::sqrt(1.0 + 4.0 * t * t));
        y = xNext + ((t - 1.0) / tNext) * (xNext - x);
        x = xNext;
        fx = fNext;
        t = tNext;
    }

    // Polish: Newton step on the variables not held at a bound by the gradient.
    const Eigen::VectorXd grad = h * x + g;
    std::vector<int> free, bound;
    for (int i = 0; i < int(n); ++i) {
        const bool atLower = x(i) <= lb(i) && grad(i) > 0.0;
        const bool atUpper = x(i) >= ub(i) && grad(i) < 0.0;
        (atLower || atUpper ? bound : free).push_back(i);
    }
    Eigen::VectorXd polished = x;
    solveFree(h, g, free, bound, &polished);
    const bool feasible = polished.allFinite() && (polished.array() >= lb.array()).all() &&
                          (polished.array() <= ub.array()).all();
    if (feasible && objective(h, g, polished) <= fx) {
        return polished;
    }
    return x;
}

}  // namespace tracelab::lf
