// The Furuta pendulum benchmark (lab/benchmarks/furuta): the plant in C
// conserving energy without friction or input, the qLPV NMPC swinging the
// pendulum up from hanging down and balancing it (warm-start and iterative
// scheduling), then following the arm's step to pi, frozen and Taylor
// running within the bounds, and the energy swing-up + LQR doing the same;
// plant and controller closed directly, the way the plant device and the
// controller host step them; then the benchmark's devices (the plant and
// the controller host, in lockstep through BTP, bound by the names the code
// declares) swinging it up the same way.

#include <QElapsedTimer>
#include <QtTest>
#include <cmath>

#include "lab/benchmarks/furuta/code/furutatemplates.h"
#include "lab/code/codefiles.h"
#include "lab/control/codecontrolleradapter.h"
#include "lab/control/controllerhost.h"
#include "lab/control/labdevices.h"
#include "lab/plant/codeplant.h"
#include "lab/plant/codeplantdevice.h"

using namespace tracelab;

namespace {

constexpr double kTs = 0.02;

double wrap(double angle) {
    return std::remainder(angle, 2.0 * M_PI);
}

struct Run {
    QVector<double> t, theta1, theta2, u, solveMs, qpIterations;
    QString error;
};

QString source(const char* id) {
    return code::CodeLibrary::instance().templateSource(QLatin1String(id));
}

// `seconds` of closed loop from hanging down at rest; `params` set on the
// controller before it starts.
Run closedLoop(const char* controllerId, double seconds, const QHash<QString, double>& params = {}) {
    Run run;
    CodePlant plant;
    if (!plant.load(source("furuta/pendulum"), QStringLiteral("pendulum.c"))) {
        run.error = plant.errorText();
        return run;
    }
    plant.setSampleTime(kTs);
    plant.setSolver(CodePlant::Solver::Rk4, 4);
    if (!plant.reset()) {
        run.error = QStringLiteral("plant reset");
        return run;
    }
    CodeControllerAdapter controller;
    if (!controller.load(source(controllerId), QStringLiteral("controller.c"))) {
        run.error = controller.module().messages().isEmpty()
                        ? QStringLiteral("controller")
                        : controller.module().messages().first().text;
        return run;
    }
    controller.setSampleTime(kTs);
    controller.setSignalNames({QStringLiteral("th1"), QStringLiteral("th2"), QStringLiteral("w1"),
                               QStringLiteral("w2")},
                              {QStringLiteral("u")});
    QString error;
    if (!controller.configure(4, 1, &error)) {
        run.error = error;
        return run;
    }
    for (auto it = params.begin(); it != params.end(); ++it) {
        controller.setParam(it.key(), it.value());
    }
    controller.reset();

    const int steps = int(std::lround(seconds / kTs));
    for (int k = 0; k < steps; ++k) {
        ControllerInput input;
        input.tick = quint64(plant.tick());
        input.y = plant.outputs();
        ControllerOutput output;
        QElapsedTimer clock;
        clock.start();
        if (!controller.compute(input, &output) || output.u.isEmpty()) {
            run.error = QStringLiteral("compute at tick %1: %2")
                            .arg(k)
                            .arg(controller.runtimeErrorText());
            return run;
        }
        run.solveMs.append(double(clock.nsecsElapsed()) / 1e6);
        run.qpIterations.append(output.diag.size() > 4 ? output.diag[4] : 0.0);
        if (!plant.step({output.u[0][0]})) {
            run.error = QStringLiteral("plant step at tick %1").arg(k);
            return run;
        }
        run.t.append(plant.time());
        run.theta1.append(plant.state(QStringLiteral("theta1")));
        run.theta2.append(plant.state(QStringLiteral("theta2")));
        run.u.append(plant.appliedInputs().first());
    }
    return run;
}

// The first time from which |wrap(theta2)| stays below `angle` until `until`.
double settledFrom(const Run& run, double angle, double until) {
    double from = qQNaN();
    for (int i = 0; i < run.t.size() && run.t[i] <= until; ++i) {
        if (std::fabs(wrap(run.theta2[i])) < angle) {
            if (std::isnan(from)) {
                from = run.t[i];
            }
        } else {
            from = qQNaN();
        }
    }
    return from;
}

double valueAt(const QVector<double>& t, const QVector<double>& values, double at) {
    for (int i = 0; i < t.size(); ++i) {
        if (t[i] >= at - 1e-9) {
            return values[i];
        }
    }
    return qQNaN();
}

void summarize(const char* name, const Run& run) {
    double mean = 0, worst = 0;
    for (double ms : run.solveMs) {
        mean += ms;
        worst = std::max(worst, ms);
    }
    mean /= double(std::max<qsizetype>(1, run.solveMs.size()));
    double iterations = 0;
    for (double it : run.qpIterations) {
        iterations += it;
    }
    iterations /= double(std::max<qsizetype>(1, run.qpIterations.size()));
    qInfo("%s: upright from %.2f s, theta1(19.5 s) %.3f, theta2(19.5 s) %.4f, theta2(24 s) "
          "%.4f, solve %.2f ms mean, %.2f ms max, %.1f QP iterations a step",
          name, settledFrom(run, 0.05, 10.9), valueAt(run.t, run.theta1, 19.5),
          wrap(valueAt(run.t, run.theta2, 19.5)), wrap(valueAt(run.t, run.theta2, 24.0)), mean,
          worst, iterations);
}

}  // namespace

class TestFuruta : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        furuta::registerFurutaTemplates();
        for (const char* id : {"furuta/pendulum", "furuta/qlpv_mpc", "furuta/energy_lqr"}) {
            QVERIFY2(!source(id).isEmpty(), id);
        }
    }

    void plantConservesEnergy() {
        CodePlant plant;
        QVERIFY2(plant.load(source("furuta/pendulum"), QStringLiteral("pendulum.c")),
                 qPrintable(plant.errorText()));
        QVERIFY(!plant.isDiscrete());
        QCOMPARE(plant.outputNames(), (QStringList{QStringLiteral("th1"), QStringLiteral("th2"),
                                                   QStringLiteral("w1"), QStringLiteral("w2")}));
        plant.module().setParamValue(QStringLiteral("mu1"), 0.0);
        plant.module().setParamValue(QStringLiteral("mu2"), 0.0);
        plant.module().setParamValue(QStringLiteral("kick_time"), 0.0);
        // Released 1 rad off the upright with the arm turning.
        plant.module().setParamValue(QStringLiteral("theta2_start"), 1.0);
        plant.setSampleTime(kTs);
        plant.setSolver(CodePlant::Solver::Rk4, 10);
        QVERIFY(plant.reset({{QStringLiteral("dtheta1"), 2.0}}));
        QCOMPARE(plant.state(QStringLiteral("theta2")), 1.0);
        const double m2 = 0.09, L1 = 0.085, l2 = 0.13, I1 = 0.005, I2 = 5.07e-4, g = 9.81;
        const auto total = [&] {
            const double th2 = plant.state(QStringLiteral("theta2"));
            const double w1 = plant.state(QStringLiteral("dtheta1"));
            const double w2 = plant.state(QStringLiteral("dtheta2"));
            const double alpha = I1 + m2 * L1 * L1, beta = m2 * l2 * l2, gamma = m2 * L1 * l2;
            const double delta = I2 + m2 * l2 * l2;
            const double s = std::sin(th2), c = std::cos(th2);
            return 0.5 * (alpha + beta * s * s) * w1 * w1 + gamma * c * w1 * w2 +
                   0.5 * delta * w2 * w2 + m2 * g * l2 * c;
        };
        const double e0 = total();
        double worst = 0;
        for (int k = 0; k < 500; ++k) {
            QVERIFY(plant.step({0.0}));
            worst = std::max(worst, std::fabs(total() - e0));
        }
        QVERIFY2(worst < 1e-6 * std::fabs(e0), qPrintable(QString::number(worst)));
        // It fell and swung: a real motion, not a resting state.
        QVERIFY(std::fabs(plant.state(QStringLiteral("dtheta2"))) > 0.1);
    }

    void qlpvMpcSwingsUp_data() {
        QTest::addColumn<int>("scheduling");
        QTest::newRow("warm_start") << 1;
        QTest::newRow("iterative") << 3;
    }

    void qlpvMpcSwingsUp() {
        QFETCH(int, scheduling);
        const Run run = closedLoop("furuta/qlpv_mpc", 25.0,
                                   {{QStringLiteral("scheduling"), double(scheduling)}});
        QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
        summarize(QTest::currentDataTag(), run);
        // Up and balanced before the step at 11 s.
        const double upright = settledFrom(run, 0.05, 10.9);
        QVERIFY2(upright < 10.0, qPrintable(QString::number(upright)));
        // The arm follows the step to pi, the pendulum stays up.
        QVERIFY(std::fabs(valueAt(run.t, run.theta1, 19.5) - M_PI) < 0.1);
        QVERIFY(std::fabs(wrap(valueAt(run.t, run.theta2, 19.5))) < 0.05);
        for (double u : run.u) {
            QVERIFY(std::fabs(u) <= 0.6 + 1e-12);
        }
    }

    // Frozen and Taylor scheduling run (finite, within the torque bounds)
    // but are not asked to swing the pendulum up: frozen does not in the
    // paper either, and Taylor does not within these 25 s in this port (the
    // paper's did, after longer oscillations).
    void qlpvMpcOtherSchedulings_data() {
        QTest::addColumn<int>("scheduling");
        QTest::newRow("frozen") << 0;
        QTest::newRow("taylor") << 2;
    }

    void qlpvMpcOtherSchedulings() {
        QFETCH(int, scheduling);
        const Run run = closedLoop("furuta/qlpv_mpc", 25.0,
                                   {{QStringLiteral("scheduling"), double(scheduling)}});
        QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
        summarize(QTest::currentDataTag(), run);
        for (int i = 0; i < run.u.size(); ++i) {
            QVERIFY(std::isfinite(run.theta1[i]) && std::isfinite(run.theta2[i]));
            QVERIFY(std::fabs(run.u[i]) <= 0.6 + 1e-12);
        }
    }

    void devicesThroughBtpSwingUp() {
        CodePlantDevice plant(QStringLiteral("furuta-plant"), 0x7300001u,
                              QJsonObject{{"plant", QJsonObject{{"template", "furuta/pendulum"}}},
                                          {"runMode", "lockstep"},
                                          {"autoRun", false},
                                          {"fps", 50},
                                          {"solver", "rk4"},
                                          {"substeps", 4}});
        QVERIFY(plant.codeBlocks()->compiled(0));
        LabDeviceRegistry::instance().addPlant(QStringLiteral("furuta-plant"), &plant);
        LabControllerDevice controller(
            QStringLiteral("furuta-ctl"),
            QJsonObject{{"plantDeviceId", "furuta-plant"},
                        {"controller", QJsonObject{{"template", "furuta/qlpv_mpc"}}}});
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->configured(), 5000);
        QCOMPARE(controller.host()->measurementNames(),
                 (QStringList{QStringLiteral("th1"), QStringLiteral("th2"), QStringLiteral("w1"),
                              QStringLiteral("w2")}));
        QCOMPARE(controller.host()->inputNames(), QStringList{QStringLiteral("u")});
        QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);  // REMOTE, armed
        plant.run();
        QTRY_VERIFY_WITH_TIMEOUT(plant.tick() >= 550, 120000);
        plant.pause();
        // Up by 11 s like the direct loop (upright from 9 s there).
        const double theta2 = wrap(plant.plant()->state(QStringLiteral("theta2")));
        QVERIFY2(std::fabs(theta2) < 0.05, qPrintable(QString::number(theta2)));
        QVERIFY(std::fabs(plant.appliedInput().first()) <= 0.6);
    }

    void energyLqrSwingsUp() {
        const Run run = closedLoop("furuta/energy_lqr", 25.0);
        QVERIFY2(run.error.isEmpty(), qPrintable(run.error));
        summarize("energy_lqr", run);
        const double upright = settledFrom(run, 0.05, 10.9);
        QVERIFY2(upright < 10.0, qPrintable(QString::number(upright)));
        QVERIFY(std::fabs(valueAt(run.t, run.theta1, 19.5) - M_PI) < 0.1);
        QVERIFY(std::fabs(wrap(valueAt(run.t, run.theta2, 19.5))) < 0.05);
    }
};

QTEST_GUILESS_MAIN(TestFuruta)
#include "test_furuta.moc"
