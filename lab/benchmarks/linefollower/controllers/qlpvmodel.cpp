#include "qlpvmodel.h"

#include <algorithm>
#include <cmath>

namespace tracelab::lf {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

double sinc(double theta) {
    // np.sinc(theta / pi) = sin(theta) / theta, 1 at 0.
    return theta == 0.0 ? 1.0 : std::sin(theta) / theta;
}

double dsinc(double theta) {
    // (t cos t - sin t) / t^2, its series -t/3 near 0 (model.py dsinc).
    if (std::abs(theta) < 1e-4) {
        return -theta / 3.0;
    }
    return (theta * std::cos(theta) - std::sin(theta)) / (theta * theta);
}

MotorDiscretization discretizeMotors(double ts, const CarParams& car) {
    MotorDiscretization m;
    m.ke = 2.0 * kPi * car.wheelsRpm / 6000.0;
    m.alpha = {std::exp(-5.0 * ts / car.accommodationTimeLeft),
               std::exp(-5.0 * ts / car.accommodationTimeRight)};
    m.beta = {m.ke * car.keLeft * (1.0 - m.alpha[0]), m.ke * car.keRight * (1.0 - m.alpha[1])};
    return m;
}

QlpvModel::QlpvModel(double ts, const CarParams& car, Embedding embedding)
    : m_embedding(embedding), m_ts(ts), m_r(car.wheelsRadius) {
    const MotorDiscretization motors = discretizeMotors(ts, car);
    const double aL = motors.alpha[0];
    const double aR = motors.alpha[1];
    const double bL = motors.beta[0];
    const double bR = motors.beta[1];
    m_alpha << aL, aR;
    m_beta << bL, bR;

    // Kinematics: theta gains Ts r/L (w_l - w_r), the speed is r/2 (w_l + w_r).
    const double cT = ts * car.wheelsRadius / car.wheelsDistance;
    const double cV = ts * car.wheelsRadius / 2.0;
    m_cTheta = cT;
    m_cV = cV;

    StateMatrix a0 = StateMatrix::Identity();
    a0(0, 0) = aL;
    a0(1, 1) = aR;
    a0(2, 0) = cT * aL;
    a0(2, 1) = -cT * aR;
    InputMatrix b0 = InputMatrix::Zero();
    b0(0, 0) = bL;
    b0(1, 1) = bR;
    b0(2, 0) = cT * bL;
    b0(2, 1) = -cT * bR;

    // A row multiplied by the speed v(k+1) = c_v (a w + b u) (model.py speed_row).
    auto speedRow = [&](int row, StateMatrix* a, InputMatrix* b) {
        a->setZero();
        b->setZero();
        (*a)(row, 0) = cV * aL;
        (*a)(row, 1) = cV * aR;
        (*b)(row, 0) = cV * bL;
        (*b)(row, 1) = cV * bR;
    };

    StateMatrix a1, a2;
    InputMatrix b1, b2;
    if (embedding == Embedding::kSinCos) {
        speedRow(3, &a1, &b1);  // X: sin(theta) v(k+1)
        speedRow(4, &a2, &b2);  // Y: cos(theta) v(k+1)
        m_rhoMin << -1.0, -1.0;
        m_rhoMax << 1.0, 1.0;
    } else {
        speedRow(4, &a1, &b1);  // Y: cos(theta) v(k+1)
        a2.setZero();
        b2.setZero();
        a2(3, 2) = ts;  // X: v(k+1) sinc(theta) theta
        // Max speed with 20 % of margin for disturbances.
        const double vMax = 1.2 * 100.0 * motors.ke * car.wheelsRadius;
        m_rhoMin << -1.0, -vMax;
        m_rhoMax << 1.0, vMax;
    }
    m_aAff = {a0, a1, a2};
    m_bAff = {b0, b1, b2};
}

void QlpvModel::matrices(const Rho& rho, StateMatrix* a, InputMatrix* b) const {
    *a = m_aAff[0] + rho(0) * m_aAff[1] + rho(1) * m_aAff[2];
    *b = m_bAff[0] + rho(0) * m_bAff[1] + rho(1) * m_bAff[2];
}

QlpvModel::Rho QlpvModel::fRho(const Pair& z) const {
    const double theta = z(2);
    Rho rho;
    if (m_embedding == Embedding::kSinCos) {
        rho << std::sin(theta), std::cos(theta);
    } else {
        const double vNext = m_r / 2.0 * (z(kNx) + z(kNx + 1));
        rho << std::cos(theta), vNext * sinc(theta);
    }
    return rho;
}

Eigen::MatrixXd QlpvModel::fRhoSequence(const Eigen::MatrixXd& states) const {
    const int m = int(states.rows()) - 1;
    Eigen::MatrixXd p(std::max(m, 0), kNrho);
    for (int j = 0; j < m; ++j) {
        p.row(j) = fRho(pair(states, j)).transpose();
    }
    return p;
}

QlpvModel::Jacobian QlpvModel::jacobianRho(const Pair& z) const {
    const double theta = z(2);
    Jacobian j = Jacobian::Zero();
    if (m_embedding == Embedding::kSinCos) {
        j(0, 2) = std::cos(theta);
        j(1, 2) = -std::sin(theta);
    } else {
        const double vNext = m_r / 2.0 * (z(kNx) + z(kNx + 1));
        j(0, 2) = -std::sin(theta);
        j(1, 2) = vNext * dsinc(theta);
        j(1, kNx) = m_r / 2.0 * sinc(theta);
        j(1, kNx + 1) = m_r / 2.0 * sinc(theta);
    }
    return j;
}

QlpvModel::Rho QlpvModel::saturateRho(const Rho& rho) const {
    return rho.cwiseMax(m_rhoMin).cwiseMin(m_rhoMax);
}

QlpvModel::State QlpvModel::step(const State& x, const Input& u, const Input& d) const {
    // The wheels do not depend on rho: x(k+1)[:2] first, then rho(z).
    State next = x;
    next.head<kNu>() = m_alpha.cwiseProduct(x.head<kNu>()) + m_beta.cwiseProduct(u + d);
    Pair z;
    z << x, next;
    StateMatrix a;
    InputMatrix b;
    matrices(fRho(z), &a, &b);
    return a * x + b * (u + d);
}

QlpvModel::State QlpvModel::state(const std::array<double, 2>& wheelSpeeds) const {
    State x;
    x << wheelSpeeds[0], wheelSpeeds[1], 0.0, 0.0, 0.0;
    return x;
}

Eigen::Vector3d QlpvModel::frameShift(const std::array<double, 2>& wheelSpeeds) const {
    const double wL = wheelSpeeds[0];
    const double wR = wheelSpeeds[1];
    return {m_cTheta * (wL - wR), 0.0, m_cV * (wL + wR)};
}

void QlpvModel::realignStates(Eigen::MatrixXd* states, const Eigen::Vector3d& pose) const {
    const double c = std::cos(pose(0));
    const double s = std::sin(pose(0));
    for (Eigen::Index j = 0; j < states->rows(); ++j) {
        const double dX = (*states)(j, 3) - pose(1);
        const double dY = (*states)(j, 4) - pose(2);
        (*states)(j, 2) -= pose(0);
        (*states)(j, 3) = c * dX - s * dY;
        (*states)(j, 4) = s * dX + c * dY;
    }
}

QlpvModel::Input QlpvModel::inputDisturbance(const Input& wPrev, const Input& wNow,
                                             const Input& uPrev) const {
    return (wNow - m_alpha.cwiseProduct(wPrev)).cwiseQuotient(m_beta) - uPrev;
}

QlpvModel::Pair QlpvModel::pair(const Eigen::MatrixXd& states, int j) {
    Pair z;
    z << states.row(j).transpose(), states.row(j + 1).transpose();
    return z;
}

}  // namespace tracelab::lf
