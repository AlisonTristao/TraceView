#ifndef TRACELAB_LF_QLPVMODEL_H
#define TRACELAB_LF_QLPVMODEL_H

// The exact qLPV embedding of the differential-drive car, written in the car
// frame of the instant k (port of mpc/model.py, LineFollowerQLPV).
//
//   states  x = [w_l, w_r, theta, X, Y]  (wheel speeds [rad/s], heading [rad],
//                                         lateral and forward position [m])
//   inputs  u = [u_l, u_r]               (% of PWM)
//
//   w(k+1)     = a w(k) + b (u(k) + d)
//   theta(k+1) = theta(k) + Ts r/L (w_l - w_r)(k+1)
//   X(k+1)     = X(k) + v(k+1) Ts sin(theta(k))        v = r/2 (w_l + w_r)
//   Y(k+1)     = Y(k) + v(k+1) Ts cos(theta(k))
//
//   x(k+1) = A(rho) x(k) + B(rho) (u(k) + d),  A(rho) = A0 + rho_1 A1 + rho_2 A2
//
// Two exact factorizations of the bilinear term v sin(theta):
//   sin_cos: rho = [sin(theta), cos(theta)]     -> X linear in the speed
//   sinc:    rho = [cos(theta), v(k+1) sinc(theta)] -> X linear in the heading
// The scheduling proxy is evaluated on z = [x(k), x(k+1)] (v(k+1) depends on
// u(k) in "sinc"). theta positive turns right, X positive right, Y forward.

#include <Eigen/Dense>
#include <array>

#include "lfcontroller.h"

namespace tracelab::lf {

enum class Embedding { kSinCos, kSinc };

// Unnormalised sinc sin(t)/t and its derivative (model.py sinc, dsinc).
double sinc(double theta);
double dsinc(double theta);

class QlpvModel {
public:
    static constexpr int kNx = 5;
    static constexpr int kNu = 2;
    static constexpr int kNrho = 2;
    // Outputs theta, X, Y (model.py output_index).
    static constexpr int kOutput0 = 2;
    static constexpr int kNy = 3;

    using State = Eigen::Matrix<double, kNx, 1>;
    using Pair = Eigen::Matrix<double, 2 * kNx, 1>;
    using Rho = Eigen::Matrix<double, kNrho, 1>;
    using Input = Eigen::Matrix<double, kNu, 1>;
    using StateMatrix = Eigen::Matrix<double, kNx, kNx>;
    using InputMatrix = Eigen::Matrix<double, kNx, kNu>;
    using Jacobian = Eigen::Matrix<double, kNrho, 2 * kNx>;

    QlpvModel(double ts, const CarParams& car, Embedding embedding);

    Embedding embedding() const {
        return m_embedding;
    }
    const char* embeddingName() const {
        return m_embedding == Embedding::kSinc ? "sinc" : "sin_cos";
    }
    double ts() const {
        return m_ts;
    }
    const Input& alpha() const {
        return m_alpha;
    }
    const Input& beta() const {
        return m_beta;
    }
    const Rho& rhoMin() const {
        return m_rhoMin;
    }
    const Rho& rhoMax() const {
        return m_rhoMax;
    }
    // Affine terms: index 0 is A0/B0, 1 and 2 multiply rho_1 and rho_2.
    const StateMatrix& aAffine(int i) const {
        return m_aAff[i];
    }
    const InputMatrix& bAffine(int i) const {
        return m_bAff[i];
    }

    // A(rho), B(rho) for one scheduling point (model.py matrices).
    void matrices(const Rho& rho, StateMatrix* a, InputMatrix* b) const;

    // Scheduling proxy rho = f(z), z = [x(j), x(j+1)] (model.py f_rho).
    Rho fRho(const Pair& z) const;
    // f_rho over every consecutive pair of a state sequence (M+1, nx) ->
    // (M, nrho) (model.py f_rho(pairs(S))).
    Eigen::MatrixXd fRhoSequence(const Eigen::MatrixXd& states) const;
    // d f_rho / dz at z (model.py jacobian_rho).
    Jacobian jacobianRho(const Pair& z) const;
    Rho saturateRho(const Rho& rho) const;

    // Exact nonlinear step (model.py step, for validation).
    State step(const State& x, const Input& u, const Input& d) const;

    // x(k) in the car frame of k (model.py state).
    State state(const std::array<double, 2>& wheelSpeeds) const;
    // Pose (theta, X, Y) of the car at k written in the car frame of k-1
    // (odometry, exact for this model; model.py frame_shift).
    Eigen::Vector3d frameShift(const std::array<double, 2>& wheelSpeeds) const;
    // Rewrites a state trajectory (rows) given in the frame of k-1 in the
    // frame of k (model.py realign_states).
    void realignStates(Eigen::MatrixXd* states, const Eigen::Vector3d& pose) const;
    // The input disturbance that explains the last wheel step
    // w(k) = a w(k-1) + b (u(k-1) + d) (model.py input_disturbance).
    Input inputDisturbance(const Input& wPrev, const Input& wNow, const Input& uPrev) const;

    // Row j of a state sequence as [x(j), x(j+1)] (model.py pairs).
    static Pair pair(const Eigen::MatrixXd& states, int j);

private:
    Embedding m_embedding;
    double m_ts;
    double m_r;
    double m_cTheta;
    double m_cV;
    Input m_alpha;
    Input m_beta;
    Rho m_rhoMin;
    Rho m_rhoMax;
    std::array<StateMatrix, 1 + kNrho> m_aAff;
    std::array<InputMatrix, 1 + kNrho> m_bAff;
};

// Motor discretization shared by the qLPV model and the GPC (model.py,
// gpc.py): Ke = 2 pi RPM / 6000 rad/s per 1 % of PWM, a = exp(-5 Ts / t_s)
// (5 tau = settling time t_s), b = Ke ke (1 - a).
struct MotorDiscretization {
    double ke = 0.0;
    std::array<double, 2> alpha{};
    std::array<double, 2> beta{};
};
MotorDiscretization discretizeMotors(double ts, const CarParams& car);

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_QLPVMODEL_H
