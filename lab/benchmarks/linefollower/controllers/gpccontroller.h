#ifndef TRACELAB_LF_GPCCONTROLLER_H
#define TRACELAB_LF_GPCCONTROLLER_H

// GPC linearized at theta = 0 (port of mpc/gpc.py, LineFollowerGPC), built
// from the motor parameters.
//
// Each wheel angle phi follows the CARIMA model (Delta u as input):
//   (1 - a z^-1)(1 - z^-1)^2 phi(k) = Ts b z^-1 Delta u(k)
// Outputs (car frame of k, linear in the wheel angles):
//   theta = r/L (phi_l - phi_r),   d = r/2 (phi_l + phi_r)
// Free response: the homogeneous recursion from the last 3 wheel angles;
// forced response: each wheel's step response (Toeplitz, Nu columns).
// Reference: theta_r = x / d0 ("lookahead") or the track tangent
// ("tangent"), d_r = y. Unconstrained law Delta U = K (r - f), the input is
// saturated afterwards.

#include <Eigen/Dense>

#include "lfcontroller.h"

namespace tracelab::lf {

class GpcController : public Controller {
public:
    explicit GpcController(const ControllerConfig& config);

    std::string name() const override;
    std::array<double, 2> step(const std::array<double, 2>& wheelSpeedsRadS,
                               const std::vector<CarPoint>& futurePoints) override;
    Prediction prediction() const override;
    ControllerStats stats() const override;
    void reset() override;
    Prediction reference() const override;

    // (2 Nu, 2 N) gain, decision [Delta u_l (Nu); Delta u_r (Nu)], outputs
    // [theta (N); d (N)].
    const Eigen::MatrixXd& gain() const {
        return m_k;
    }
    const Eigen::MatrixXd& dynamicMatrix() const {
        return m_g;
    }

private:
    Eigen::MatrixXd freeMatrix(const Eigen::Vector3d& a) const;
    Eigen::VectorXd stepResponse(const Eigen::Vector3d& a, double b) const;

    double m_ts;
    int m_n;
    int m_nu;
    double m_d0;
    bool m_lookahead;
    double m_uMin;
    double m_uMax;
    double m_weightGrowth;
    std::string m_thetaReference;
    double m_cTheta;
    double m_cD;
    std::array<Eigen::MatrixXd, 2> m_f;  // (N, 3) free response per wheel
    Eigen::MatrixXd m_g;                 // (2N, 2Nu)
    Eigen::MatrixXd m_k;                 // (2Nu, 2N)

    Eigen::Vector2d m_u;
    Eigen::Matrix<double, 2, 3> m_phi;  // phi(k), phi(k-1), phi(k-2) relative to phi(k)
    bool m_hasStep = false;
    Eigen::VectorXd m_free;
    Eigen::VectorXd m_dU;
    Eigen::MatrixXd m_ref;
    ControllerStats m_stats;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_GPCCONTROLLER_H
