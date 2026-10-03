#pragma once

#include <QJsonObject>
#include <memory>

#include "lab/control/controlleradapter.h"

namespace tracelab::lf {

class Controller;
class PdController;

// The controller host device's settings (Device::moduleConfig of "TraceLab:
// Controller" in a project):
//   {"plantDeviceId": "...", "controller": "qlpv" | "gpc" | "pd", "fps": 80,
//    "horizon": 10, "filterTau": 0.0, "qLateral": 1e4, "qTheta": 10,
//    "gpcQTheta": 0.5, "gpcQDistance": 1.0, "gpcLambda": 1e-3,
//    "pdKp": 2500, "pdKd": 25, "pdBase": 60,
//    "qlpvScheduling": "taylor", "qlpvEmbedding": "sinc", "qlpvIterations": 3}
// fps and horizon must match the plant's (Ts and the preview length).
struct ControllerSettings {
    QString plantDeviceId;
    QString type = QStringLiteral("qlpv");
    int fps = 80;
    int horizon = 10;
    // First-order low-pass on the measured wheel speeds before the
    // controller (lab/filters C runtime, the code the firmware runs);
    // 0 = no filter.
    double filterTau = 0.0;
    double qLateral = 1e4;
    double qTheta = 1e1;
    // How the qLPV MPC fills the scheduling trajectory rho over the horizon
    // ("frozen" | "warm_start" | "taylor" | "iterative"), its embedding
    // ("sinc" | "sin_cos") and, for "iterative", the most QPs per step.
    QString qlpvScheduling = QStringLiteral("taylor");
    QString qlpvEmbedding = QStringLiteral("sinc");
    int qlpvIterations = 3;
    double gpcQTheta = 0.5;
    double gpcQDistance = 1.0;
    double gpcLambda = 1e-3;
    // PD on the line sensors (pdcontroller.h): % per m, % per m/s, %.
    double pdKp = 2500.0;
    double pdKd = 25.0;
    double pdBase = 60.0;

    static ControllerSettings fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

// The line follower's controllers (tracelab_lf_controllers: qLPV MPC, GPC and
// the PD) behind the ControllerHost's adapter contract. y[] is bound by name:
// the wheel speeds w_l and w_r of loop.state (the encoders: what the robot
// measures); the reference is sim.preview's car-frame points. The PD uses
// neither: only the line sensor array s[0..n-1] of loop.state. The answer is
// one step (M = 1), the prediction goes to ctl.diag, and so do the
// diagnostics qps, qp_iterations, rho_error, theta_ref and theta_pred (the
// heading aimed at and predicted on the first step), w_l_filt, w_r_filt (the
// wheel speeds the controller was given).
class LineControllerAdapter final : public ControllerAdapter {
public:
    explicit LineControllerAdapter(const ControllerSettings& settings = {});
    ~LineControllerAdapter() override;

    const ControllerSettings& settings() const {
        return m_settings;
    }
    // Rebuilds the controller (its internal state starts over).
    void setSettings(const ControllerSettings& settings);

    QString name() const override;
    void setSignalNames(const QStringList& measurements, const QStringList& inputs) override;
    bool configure(int measurementCount, int inputCount, QString* error) override;
    void reset() override;
    QVector<ControllerParam> params() const override;
    void setParam(const QString& name, double value) override;
    QStringList diagNames() const override;
    QStringList diagUnits() const override;
    bool compute(const ControllerInput& input, ControllerOutput* output) override;

private:
    struct Filter;
    void rebuild();

    ControllerSettings m_settings;
    bool computePd(const ControllerInput& input, ControllerOutput* output);

    std::unique_ptr<Controller> m_controller;
    std::unique_ptr<PdController> m_pd;  // instead of m_controller for "pd"
    std::unique_ptr<Filter> m_filter;
    int m_leftIndex = 0;
    int m_rightIndex = 1;
    QVector<int> m_sensorIndices;  // y[] positions of s[0], s[1], ...
};

}  // namespace tracelab::lf
