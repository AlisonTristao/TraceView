#ifndef TRACELAB_LF_QLPVCONTROLLER_H
#define TRACELAB_LF_QLPVCONTROLLER_H

// The qLPV MPC line follower (port of mpc/line_follower.py,
// LineFollowerController): builds the reference from the track's future
// points, estimates the input disturbance (offset-free tracking) and keeps the
// previous solution in the car frame through the odometry frame shift.
//
// contouring = true:  Q = [theta, lateral, longitudinal] (errors along the track)
// contouring = false: Q = [theta, X, Y] (errors in the car frame)

#include <Eigen/Dense>
#include <vector>

#include "lfcontroller.h"
#include "qlpvmodel.h"
#include "qlpvmpc.h"

namespace tracelab::lf {

// np.gradient (edge_order 1): one-sided differences at the ends, central
// inside. n >= 2.
Eigen::VectorXd gradient(const Eigen::VectorXd& f);

// Future points (N, car frame) -> reference (N, 3) [theta, X, Y], theta the
// heading of the track tangent atan2(dx, dy) (line_follower.py track_reference).
Eigen::MatrixXd trackReference(const std::vector<CarPoint>& points);

// Stage weights (N of 3x3) with the position error along the track
// (line_follower.py contouring_weights):
//   e_lat = (p - r).n, e_lon = (p - r).t,
//   t = (sin theta_r, cos theta_r), n = (cos theta_r, -sin theta_r)
//   W = diag(q_theta, q_lat n n' + q_lon t t'), Q_N at the last step.
std::vector<Eigen::Matrix3d> contouringWeights(const Eigen::MatrixXd& ref,
                                               const std::array<double, 3>& q,
                                               const std::array<double, 3>& qN);

class QlpvController : public Controller {
public:
    QlpvController(const ControllerConfig& config, Embedding embedding,
                   std::unique_ptr<Scheduling> scheduling);

    std::string name() const override;
    std::array<double, 2> step(const std::array<double, 2>& wheelSpeedsRadS,
                               const std::vector<CarPoint>& futurePoints) override;
    Prediction prediction() const override;
    ControllerStats stats() const override;
    void reset() override;
    Prediction reference() const override;
    std::vector<std::array<double, 2>> schedulingTrajectory() const override;

    const QlpvModel& model() const {
        return m_model;
    }
    const QlpvMpc& mpc() const {
        return m_mpc;
    }
    // The filtered input disturbance estimate d.
    const QlpvModel::Input& disturbance() const {
        return m_d;
    }

private:
    QlpvModel m_model;
    QlpvMpc m_mpc;
    QlpvParams m_params;
    int m_n;

    QlpvModel::Input m_u;
    QlpvModel::Input m_wPrev;
    QlpvModel::Input m_d;
    Eigen::MatrixXd m_ref;
};

}  // namespace tracelab::lf

#endif  // TRACELAB_LF_QLPVCONTROLLER_H
