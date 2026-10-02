// The line-follower controllers (lab/benchmarks/linefollower/controllers), the
// C++ port of line_follower_simulator's mpc/ package. There is no Python
// ground truth to run here, so these are sanity checks of the port: the
// model against the kinematics it embeds, the box QP against its KKT
// conditions, the sign conventions (theta > 0 turns right, so a right curve
// needs the left wheel faster), input bounds, determinism, every scheduling
// strategy and embedding, and both GPC heading references in closed loop.

#include <QtTest>
#include <cmath>
#include <functional>
#include <random>

#include "boxqp.h"
#include "gpccontroller.h"
#include "lfcontroller.h"
#include "qlpvcontroller.h"
#include "qlpvmodel.h"
#include "qlpvmpc.h"

using namespace tracelab::lf;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTs = 1.0 / 80.0;
constexpr double kSpacing = 0.03;  // m between future points (3 x 1 cm)

// The plant: the same first-order motors and unicycle kinematics as the
// Python simulator, in a world frame with the car-frame convention (heading
// psi from +y towards +x, so psi > 0 is a right turn).
struct Plant {
    MotorDiscretization motors;
    CarParams car;
    double x = 0.0, y = 0.0, psi = 0.0;
    std::array<double, 2> w{0.0, 0.0};

    Plant(const CarParams& c) : motors(discretizeMotors(kTs, c)), car(c) {}

    void step(const std::array<double, 2>& u) {
        for (int i = 0; i < 2; ++i) {
            w[size_t(i)] =
                motors.alpha[size_t(i)] * w[size_t(i)] + motors.beta[size_t(i)] * u[size_t(i)];
        }
        const double v = car.wheelsRadius / 2.0 * (w[0] + w[1]);
        x += v * kTs * std::sin(psi);
        y += v * kTs * std::cos(psi);
        psi += kTs * car.wheelsRadius / car.wheelsDistance * (w[0] - w[1]);
    }

    CarPoint toCar(double px, double py) const {
        const double dx = px - x;
        const double dy = py - y;
        return {dx * std::cos(psi) - dy * std::sin(psi), dx * std::sin(psi) + dy * std::cos(psi)};
    }
};

// A circle turning right, centred at (radius, 0): the car starts on it at the
// origin heading +y. Tracks the arc angle continuously.
struct RightCircle {
    double radius;
    double phi = 0.0;

    std::vector<CarPoint> futurePoints(const Plant& plant, int n) {
        double angle = std::atan2(plant.y, -(plant.x - radius));
        while (angle - phi > kPi) {
            angle -= 2.0 * kPi;
        }
        while (angle - phi < -kPi) {
            angle += 2.0 * kPi;
        }
        phi = angle;
        std::vector<CarPoint> points;
        for (int j = 0; j < n; ++j) {
            const double a = phi + (j + 1) * kSpacing / radius;
            points.push_back(plant.toCar(radius - radius * std::cos(a), radius * std::sin(a)));
        }
        return points;
    }

    double lateralError(const Plant& plant) const {
        return std::hypot(plant.x - radius, plant.y) - radius;
    }
};

std::vector<CarPoint> straightPoints(int n) {
    std::vector<CarPoint> points;
    for (int j = 0; j < n; ++j) {
        points.push_back({0.0, (j + 1) * kSpacing});
    }
    return points;
}

CarParams symmetricCar() {
    CarParams car;
    car.accommodationTimeLeft = car.accommodationTimeRight = 0.6;
    return car;
}

bool finite(const std::array<double, 2>& u) {
    return std::isfinite(u[0]) && std::isfinite(u[1]);
}

bool finite(const Prediction& p) {
    for (size_t j = 0; j < p.theta.size(); ++j) {
        if (!std::isfinite(p.theta[j]) || !std::isfinite(p.x[j]) || !std::isfinite(p.y[j])) {
            return false;
        }
    }
    return true;
}

struct LoopResult {
    bool ok = true;
    QString failure;
    double maxLateralLate = 0.0;  // max |lateral error| over the second half
    std::vector<std::array<double, 2>> inputs;
};

// Closed loop on the right circle; checks every step's u (finite, within the
// bounds) and prediction (N finite entries).
LoopResult runCircle(const ControllerConfig& config, int steps, double radius = 1.5) {
    LoopResult result;
    std::unique_ptr<Controller> controller = makeController(config);
    if (!controller) {
        result.ok = false;
        result.failure = "makeController returned nullptr";
        return result;
    }
    Plant plant(config.car);
    RightCircle track{radius};
    for (int k = 0; k < steps; ++k) {
        const std::array<double, 2> u =
            controller->step(plant.w, track.futurePoints(plant, config.horizon));
        result.inputs.push_back(u);
        const Prediction pred = controller->prediction();
        if (!finite(u) || u[0] < config.uMin - 1e-9 || u[0] > config.uMax + 1e-9 ||
            u[1] < config.uMin - 1e-9 || u[1] > config.uMax + 1e-9) {
            result.ok = false;
            result.failure = QString("step %1: u = [%2, %3] (%4)")
                                 .arg(k)
                                 .arg(u[0])
                                 .arg(u[1])
                                 .arg(QString::fromStdString(controller->name()));
            return result;
        }
        if (int(pred.theta.size()) != config.horizon || int(pred.x.size()) != config.horizon ||
            int(pred.y.size()) != config.horizon || !finite(pred)) {
            result.ok = false;
            result.failure = QString("step %1: bad prediction").arg(k);
            return result;
        }
        plant.step(u);
        if (k >= steps / 2) {
            result.maxLateralLate =
                std::max(result.maxLateralLate, std::abs(track.lateralError(plant)));
        }
    }
    return result;
}

ControllerConfig qlpvConfig(const std::string& embedding, const std::string& scheduling) {
    ControllerConfig config;
    config.type = "qlpv";
    config.qlpv.embedding = embedding;
    config.qlpv.scheduling = scheduling;
    return config;
}

ControllerConfig gpcConfig(const std::string& thetaReference) {
    ControllerConfig config;
    config.type = "gpc";
    config.gpc.thetaReference = thetaReference;
    return config;
}

}  // namespace

class TestLfControllers : public QObject {
    Q_OBJECT

private slots:
    void motorDiscretization();
    void modelEmbeddingsAreExact();
    void horizonProfile_data();
    void horizonProfile();
    void boxQpSatisfiesKkt();
    void boxQpFallbackAgrees();
    void straightLineDrivesBothWheelsEqually_data();
    void straightLineDrivesBothWheelsEqually();
    void rightCurveSpeedsUpLeftWheel_data();
    void rightCurveSpeedsUpLeftWheel();
    void outputsStayWithinBounds_data();
    void outputsStayWithinBounds();
    void deterministic();
    void everySchedulingAndEmbeddingRuns_data();
    void everySchedulingAndEmbeddingRuns();
    void gpcBothReferencesRun_data();
    void gpcBothReferencesRun();
    void qlpvTracksCircle();
    void names();
    void rejectsBadConfigAndInput();
};

void TestLfControllers::motorDiscretization() {
    CarParams car;
    const MotorDiscretization m = discretizeMotors(kTs, car);
    const double ke = 2.0 * kPi * 1000.0 / 6000.0;
    QVERIFY(std::abs(m.ke - ke) < 1e-12);
    const double aL = std::exp(-kTs / (0.62 / 5.0));
    QVERIFY(std::abs(m.alpha[0] - aL) < 1e-12);
    QVERIFY(std::abs(m.beta[0] - ke * (1.0 - aL)) < 1e-12);
    // Steady state: 100 % PWM -> wheels_RPM.
    QVERIFY(std::abs(m.beta[1] / (1.0 - m.alpha[1]) * 100.0 - 1000.0 * 2.0 * kPi / 60.0) < 1e-9);
}

void TestLfControllers::modelEmbeddingsAreExact() {
    CarParams car;
    const double r = car.wheelsRadius;
    for (Embedding e : {Embedding::kSinc, Embedding::kSinCos}) {
        const QlpvModel model(kTs, car, e);
        QlpvModel::State x;
        x << 30.0, 25.0, 0.4, 0.01, 0.02;
        const QlpvModel::Input u(40.0, -10.0), d(2.0, -1.0);
        const QlpvModel::State next = model.step(x, u, d);
        // Direct kinematics.
        const double wl = model.alpha()(0) * x(0) + model.beta()(0) * (u(0) + d(0));
        const double wr = model.alpha()(1) * x(1) + model.beta()(1) * (u(1) + d(1));
        const double v = r / 2.0 * (wl + wr);
        QVERIFY(std::abs(next(0) - wl) < 1e-12);
        QVERIFY(std::abs(next(1) - wr) < 1e-12);
        QVERIFY(std::abs(next(2) - (x(2) + kTs * r / car.wheelsDistance * (wl - wr))) < 1e-12);
        QVERIFY(std::abs(next(3) - (x(3) + v * kTs * std::sin(x(2)))) < 1e-12);
        QVERIFY(std::abs(next(4) - (x(4) + v * kTs * std::cos(x(2)))) < 1e-12);
    }
    // The sinc proxy's jacobian against central differences.
    const QlpvModel model(kTs, car, Embedding::kSinc);
    QlpvModel::Pair z;
    z << 30.0, 25.0, 0.3, 0.0, 0.0, 31.0, 24.0, 0.35, 0.01, 0.02;
    const QlpvModel::Jacobian j = model.jacobianRho(z);
    for (int i = 0; i < 2 * QlpvModel::kNx; ++i) {
        QlpvModel::Pair zp = z, zm = z;
        zp(i) += 1e-6;
        zm(i) -= 1e-6;
        const QlpvModel::Rho numeric = (model.fRho(zp) - model.fRho(zm)) / 2e-6;
        QVERIFY2((numeric - j.col(i)).cwiseAbs().maxCoeff() < 1e-6, qPrintable(QString::number(i)));
    }
}

void TestLfControllers::horizonProfile_data() {
    QTest::addColumn<int>("n");
    QTest::addColumn<double>("growth");
    QTest::newRow("constant") << 45 << 1.0;
    QTest::newRow("growing") << 45 << 10.0;
    QTest::newRow("short") << 1 << 10.0;
}

void TestLfControllers::horizonProfile() {
    QFETCH(int, n);
    QFETCH(double, growth);
    const Eigen::VectorXd w = tracelab::lf::horizonProfile(n, growth);
    QCOMPARE(int(w.size()), n);
    QCOMPARE(w(0), 1.0);
    if (n >= 2) {
        QVERIFY(std::abs(w(n - 1) - growth) < 1e-12);
    }
}

void TestLfControllers::boxQpSatisfiesKkt() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    for (int trial = 0; trial < 20; ++trial) {
        const int n = 12;
        Eigen::MatrixXd m(n, n);
        for (int i = 0; i < n; ++i) {
            for (int k = 0; k < n; ++k) {
                m(i, k) = uni(rng);
            }
        }
        const Eigen::MatrixXd h = m * m.transpose() + 0.1 * Eigen::MatrixXd::Identity(n, n);
        Eigen::VectorXd g(n);
        for (int i = 0; i < n; ++i) {
            g(i) = 5.0 * uni(rng);
        }
        const Eigen::VectorXd lb = Eigen::VectorXd::Constant(n, -0.5);
        const Eigen::VectorXd ub = Eigen::VectorXd::Constant(n, 0.8);
        const BoxQpResult res = solveBoxQp(h, g, lb, ub);
        QVERIFY(!res.fallback);
        const Eigen::VectorXd grad = h * res.x + g;
        for (int i = 0; i < n; ++i) {
            QVERIFY(res.x(i) >= lb(i) - 1e-12 && res.x(i) <= ub(i) + 1e-12);
            if (res.x(i) <= lb(i)) {
                QVERIFY(grad(i) >= -1e-9);
            } else if (res.x(i) >= ub(i)) {
                QVERIFY(grad(i) <= 1e-9);
            } else {
                QVERIFY(std::abs(grad(i)) < 1e-9);
            }
        }
    }
}

void TestLfControllers::boxQpFallbackAgrees() {
    // The projected-gradient fallback reaches the active set's minimizer.
    std::mt19937 rng(11);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    for (int trial = 0; trial < 10; ++trial) {
        const int n = 10;
        Eigen::MatrixXd m(n, n);
        for (int i = 0; i < n; ++i) {
            for (int k = 0; k < n; ++k) {
                m(i, k) = uni(rng);
            }
        }
        const Eigen::MatrixXd h = m * m.transpose() + 0.5 * Eigen::MatrixXd::Identity(n, n);
        Eigen::VectorXd g(n);
        for (int i = 0; i < n; ++i) {
            g(i) = 4.0 * uni(rng);
        }
        const Eigen::VectorXd lb = Eigen::VectorXd::Constant(n, -0.3);
        const Eigen::VectorXd ub = Eigen::VectorXd::Constant(n, 0.6);
        const Eigen::VectorXd exact = solveBoxQp(h, g, lb, ub).x;
        const Eigen::VectorXd projected =
            solveBoxQpProjected(h, g, lb, ub, Eigen::VectorXd::Zero(n));
        QVERIFY2((exact - projected).cwiseAbs().maxCoeff() < 1e-6,
                 qPrintable(QString::number((exact - projected).cwiseAbs().maxCoeff())));
    }
    // maxIter = 1 cannot settle (settling needs a repeated set): the fallback runs.
    Eigen::MatrixXd h(2, 2);
    h << 2.0, 0.5, 0.5, 1.0;
    const Eigen::VectorXd g = Eigen::Vector2d(-3.0, 1.0);
    const Eigen::VectorXd lb = Eigen::Vector2d(-1.0, -1.0), ub = Eigen::Vector2d(1.0, 1.0);
    const BoxQpResult forced = solveBoxQp(h, g, lb, ub, nullptr, 1);
    QVERIFY(forced.fallback);
    QCOMPARE(forced.iterations, 1);
    QVERIFY((forced.x - solveBoxQp(h, g, lb, ub).x).cwiseAbs().maxCoeff() < 1e-6);
}

void TestLfControllers::straightLineDrivesBothWheelsEqually_data() {
    QTest::addColumn<QString>("type");
    QTest::newRow("qlpv") << "qlpv";
    QTest::newRow("gpc lookahead") << "lookahead";
    QTest::newRow("gpc tangent") << "tangent";
}

void TestLfControllers::straightLineDrivesBothWheelsEqually() {
    QFETCH(QString, type);
    ControllerConfig config =
        type == "qlpv" ? qlpvConfig("sinc", "taylor") : gpcConfig(type.toStdString());
    config.car = symmetricCar();
    std::unique_ptr<Controller> controller = makeController(config);
    QVERIFY(controller);
    Plant plant(config.car);
    for (int k = 0; k < 200; ++k) {
        const std::array<double, 2> u = controller->step(plant.w, straightPoints(config.horizon));
        QVERIFY2(u[0] > 0.0 && u[1] > 0.0,
                 qPrintable(QString("k=%1 u=[%2, %3]").arg(k).arg(u[0]).arg(u[1])));
        QVERIFY2(std::abs(u[0] - u[1]) < 1e-6 * std::max(1.0, std::abs(u[0])),
                 qPrintable(QString("k=%1 u=[%2, %3]").arg(k).arg(u[0]).arg(u[1])));
        plant.w = {plant.motors.alpha[0] * plant.w[0] + plant.motors.beta[0] * u[0],
                   plant.motors.alpha[1] * plant.w[1] + plant.motors.beta[1] * u[1]};
    }
    // Offset-free in speed: the car settles at the reference's spacing / Ts.
    const double speed = config.car.wheelsRadius / 2.0 * (plant.w[0] + plant.w[1]);
    QVERIFY2(std::abs(speed - kSpacing / kTs) < 1e-3, qPrintable(QString::number(speed)));
    // Default car (slightly different motors): still forward and nearly equal.
    config.car = CarParams();
    controller = makeController(config);
    const std::array<double, 2> u = controller->step({0.0, 0.0}, straightPoints(config.horizon));
    QVERIFY(u[0] > 0.0 && u[1] > 0.0);
    QVERIFY(std::abs(u[0] - u[1]) < 0.2 * std::max(u[0], u[1]));
}

void TestLfControllers::rightCurveSpeedsUpLeftWheel_data() {
    QTest::addColumn<QString>("type");
    QTest::newRow("qlpv sinc") << "sinc";
    QTest::newRow("qlpv sin_cos") << "sin_cos";
    QTest::newRow("gpc lookahead") << "lookahead";
    QTest::newRow("gpc tangent") << "tangent";
}

void TestLfControllers::rightCurveSpeedsUpLeftWheel() {
    // Python convention: omega = r/L (w_l - w_r) > 0 turns right.
    QFETCH(QString, type);
    const bool qlpv = type.startsWith("sin");
    ControllerConfig config =
        qlpv ? qlpvConfig(type.toStdString(), "taylor") : gpcConfig(type.toStdString());
    config.car = symmetricCar();
    std::unique_ptr<Controller> controller = makeController(config);
    QVERIFY(controller);
    // Already moving straight at 1 m/s, the track bends right (R = 0.5 m).
    Plant plant(config.car);
    const double w0 = 1.0 / config.car.wheelsRadius;
    plant.w = {w0, w0};
    RightCircle track{0.5};
    const std::array<double, 2> u =
        controller->step(plant.w, track.futurePoints(plant, config.horizon));
    QVERIFY2(u[0] > u[1], qPrintable(QString("u=[%1, %2]").arg(u[0]).arg(u[1])));
    // And, a few samples in, the predicted path turns right. (Not on the
    // first sample: there sin_cos's frozen rho = [sin 0, cos 0] leaves X
    // flat along the whole horizon -- the Python model does the same.)
    plant.step(u);
    for (int k = 0; k < 4; ++k) {
        plant.step(controller->step(plant.w, track.futurePoints(plant, config.horizon)));
    }
    const Prediction pred = controller->prediction();
    QVERIFY(pred.theta.back() > 0.0);
    QVERIFY(pred.x.back() > 0.0);
}

void TestLfControllers::outputsStayWithinBounds_data() {
    QTest::addColumn<QString>("type");
    QTest::newRow("qlpv") << "qlpv";
    QTest::newRow("gpc") << "gpc";
}

void TestLfControllers::outputsStayWithinBounds() {
    QFETCH(QString, type);
    ControllerConfig config =
        type == "qlpv" ? qlpvConfig("sinc", "taylor") : gpcConfig("lookahead");
    // Tight enough that the inputs saturate (the GPC slows down to ~13 % on
    // this circle, so its bound is lower).
    config.uMin = -15.0;
    config.uMax = type == "qlpv" ? 40.0 : 12.0;
    const LoopResult result = runCircle(config, 300, 0.4);
    QVERIFY2(result.ok, qPrintable(result.failure));
    // The bounds are actually reached (the constraint is active).
    bool touched = false;
    for (const auto& u : result.inputs) {
        touched = touched || u[0] >= config.uMax - 1e-9 || u[1] >= config.uMax - 1e-9 ||
                  u[0] <= config.uMin + 1e-9 || u[1] <= config.uMin + 1e-9;
    }
    QVERIFY(touched);
}

void TestLfControllers::deterministic() {
    for (const ControllerConfig& config :
         {qlpvConfig("sinc", "iterative"), qlpvConfig("sin_cos", "taylor"), gpcConfig("tangent")}) {
        const LoopResult a = runCircle(config, 120);
        const LoopResult b = runCircle(config, 120);
        QVERIFY(a.ok && b.ok);
        QVERIFY(a.inputs == b.inputs);
        // reset() brings a used controller back to a fresh one.
        std::unique_ptr<Controller> controller = makeController(config);
        Plant plant(config.car);
        RightCircle track{1.5};
        for (int k = 0; k < 30; ++k) {
            plant.step(controller->step(plant.w, track.futurePoints(plant, config.horizon)));
        }
        controller->reset();
        QVERIFY(controller->prediction().theta.empty());
        Plant fresh(config.car);
        RightCircle freshTrack{1.5};
        for (int k = 0; k < 30; ++k) {
            const std::array<double, 2> u =
                controller->step(fresh.w, freshTrack.futurePoints(fresh, config.horizon));
            QVERIFY(u == a.inputs[size_t(k)]);
            fresh.step(u);
        }
    }
}

void TestLfControllers::everySchedulingAndEmbeddingRuns_data() {
    QTest::addColumn<QString>("embedding");
    QTest::addColumn<QString>("scheduling");
    QTest::addColumn<QString>("initial");
    QTest::addColumn<bool>("contouring");
    QTest::addColumn<double>("growth");
    for (const char* e : {"sinc", "sin_cos"}) {
        for (const char* s : {"frozen", "warm_start", "taylor", "iterative"}) {
            for (bool contouring : {true, false}) {
                QTest::newRow(
                    qPrintable(QString("%1 %2 %3").arg(e, s, contouring ? "contouring" : "xy")))
                    << e << s << "frozen" << contouring << 1.0;
            }
        }
        for (const char* initial : {"warm_start", "taylor"}) {
            QTest::newRow(qPrintable(QString("%1 iterative from %2").arg(e, initial)))
                << e << "iterative" << initial << true << 1.0;
        }
        QTest::newRow(qPrintable(QString("%1 taylor growth 10").arg(e)))
            << e << "taylor" << "frozen" << true << 10.0;
    }
}

void TestLfControllers::everySchedulingAndEmbeddingRuns() {
    QFETCH(QString, embedding);
    QFETCH(QString, scheduling);
    QFETCH(QString, initial);
    QFETCH(bool, contouring);
    QFETCH(double, growth);
    ControllerConfig config = qlpvConfig(embedding.toStdString(), scheduling.toStdString());
    config.qlpv.initial = initial.toStdString();
    config.qlpv.contouring = contouring;
    config.qlpv.weightGrowth = growth;
    config.qlpv.relaxation = 0.7;
    const LoopResult result = runCircle(config, 300);
    QVERIFY2(result.ok, qPrintable(result.failure));

    std::unique_ptr<Controller> controller = makeController(config);
    Plant plant(config.car);
    RightCircle track{1.5};
    controller->step(plant.w, track.futurePoints(plant, config.horizon));
    const ControllerStats stats = controller->stats();
    QVERIFY(stats.qps >= 1);
    QVERIFY(stats.qps <= (scheduling == "iterative" ? config.qlpv.lMax : 1));
    QVERIFY(std::isfinite(stats.rhoError));
    QCOMPARE(int(controller->schedulingTrajectory().size()), config.horizon);
    QCOMPARE(int(controller->reference().theta.size()), config.horizon);
}

void TestLfControllers::gpcBothReferencesRun_data() {
    QTest::addColumn<QString>("reference");
    QTest::newRow("lookahead") << "lookahead";
    QTest::newRow("tangent") << "tangent";
}

void TestLfControllers::gpcBothReferencesRun() {
    QFETCH(QString, reference);
    ControllerConfig config = gpcConfig(reference.toStdString());
    const LoopResult result = runCircle(config, 300);
    QVERIFY2(result.ok, qPrintable(result.failure));
    std::unique_ptr<Controller> controller = makeController(config);
    QVERIFY(controller->prediction().theta.empty());
    controller->step({0.0, 0.0}, straightPoints(config.horizon));
    QCOMPARE(int(controller->prediction().theta.size()), config.horizon);
    QCOMPARE(controller->stats().qps, 0);
    QVERIFY(controller->schedulingTrajectory().empty());
}

void TestLfControllers::qlpvTracksCircle() {
    // The default configuration follows a 1.5 m circle within 2 cm.
    for (const char* embedding : {"sinc", "sin_cos"}) {
        const LoopResult result = runCircle(qlpvConfig(embedding, "taylor"), 400);
        QVERIFY2(result.ok, qPrintable(result.failure));
        QVERIFY2(result.maxLateralLate < 0.02, qPrintable(QString::number(result.maxLateralLate)));
    }
}

void TestLfControllers::names() {
    QCOMPARE(QString::fromStdString(makeController(qlpvConfig("sinc", "taylor"))->name()),
             QString("qLPV MPC (sinc, taylor)"));
    ControllerConfig iterative = qlpvConfig("sin_cos", "iterative");
    iterative.qlpv.relaxation = 0.5;
    iterative.qlpv.weightGrowth = 10.0;
    QCOMPARE(QString::fromStdString(makeController(iterative)->name()),
             QString("qLPV MPC (sin_cos, iterative(frozen, l_max=3, gamma=0.5), g=10)"));
    QCOMPARE(QString::fromStdString(makeController(gpcConfig("lookahead"))->name()),
             QString("GPC (N=45, Nu=5, lookahead)"));
}

void TestLfControllers::rejectsBadConfigAndInput() {
    ControllerConfig config;
    config.type = "pid";
    QVERIFY(!makeController(config));
    QVERIFY(!makeController(qlpvConfig("tan", "taylor")));
    QVERIFY(!makeController(qlpvConfig("sinc", "magic")));
    ControllerConfig badInitial = qlpvConfig("sinc", "iterative");
    badInitial.qlpv.initial = "iterative";
    QVERIFY(!makeController(badInitial));
    ControllerConfig shortHorizon = qlpvConfig("sinc", "taylor");
    shortHorizon.horizon = 1;
    QVERIFY(!makeController(shortHorizon));

    std::unique_ptr<Controller> controller = makeController(qlpvConfig("sinc", "taylor"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             controller->step({0.0, 0.0}, straightPoints(10)));
    controller = makeController(gpcConfig("lookahead"));
    QVERIFY_THROWS_EXCEPTION(std::invalid_argument,
                             controller->step({0.0, 0.0}, straightPoints(10)));
}

QTEST_GUILESS_MAIN(TestLfControllers)
#include "test_lfcontrollers.moc"
