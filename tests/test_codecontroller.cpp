// Controllers and filters written in C (lab/control/codecontrolleradapter.h,
// codesignalfilter.h, labdevices.h) closing the loop on the line follower
// through BTP: the PD template step for step equal to the C++ PD, a filter
// replacing and adding signals by name, the errors a misnamed signal gives,
// a fault holding the plant's input until reset, and a saved file compiled
// again while running (a broken version leaving the old one in charge), and
// the Kalman template estimating the car's speed and turn rate from its
// noisy encoders, gyro and accelerometer better than any of them alone.

#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "lab/benchmarks/linefollower/code/lftemplates.h"
#include "lab/benchmarks/linefollower/device/lfdevices.h"
#include "lab/code/codefiles.h"
#include "lab/control/codecontrolleradapter.h"
#include "lab/control/codesignalfilter.h"
#include "lab/control/controllerhost.h"
#include "lab/control/labdevices.h"
#include "lab/plant/codeplant.h"

using namespace tracelab;
using namespace tracelab::lf;

namespace {

struct Step {
    double uL;
    double uR;
};

// Runs `plant` in lockstep for `steps` ticks and returns what it applied.
QVector<Step> run(LinePlantDevice& plant, int steps, QObject* context) {
    QVector<Step> got;
    QObject::connect(&plant, &LinePlantDevice::ticked, context, [&](long long) {
        got.append({plant.appliedInput()[0], plant.appliedInput()[1]});
        if (got.size() >= steps) {
            plant.pause();
        }
    });
    plant.run();
    QTest::qWaitFor([&] { return got.size() >= steps; }, 60000);
    QObject::disconnect(&plant, &LinePlantDevice::ticked, context, nullptr);
    return got;
}

// The track, as far as the car's line sensors care: no line anywhere.
double noTrackDistance(double, double) {
    return 1.0;
}
double noTrackHalfWidth() {
    return 0.01;
}

double rms(const QVector<double>& errors) {
    double sum = 0.0;
    for (double e : errors) {
        sum += e * e;
    }
    return errors.isEmpty() ? 0.0 : std::sqrt(sum / errors.size());
}

PlantSettings lockstep() {
    PlantSettings settings;
    settings.runMode = RunMode::Lockstep;
    settings.autoRun = false;
    return settings;
}

}  // namespace

class TestCodeController : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        registerLineFollowerTemplates();
    }

    void pdTemplateMatchesTheNativePd() {
        constexpr int kSteps = 400;
        QVector<Step> native;
        {
            LinePlantDevice plant(0x5000001u, lockstep());
            LineDeviceRegistry::instance().addPlant(QStringLiteral("native-plant"), &plant);
            ControllerSettings settings;
            settings.plantDeviceId = QStringLiteral("native-plant");
            settings.type = QStringLiteral("pd");
            LineControllerDevice controller(QStringLiteral("native-pd"), settings);
            QTRY_VERIFY_WITH_TIMEOUT(controller.host()->plantReady(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);
            native = run(plant, kSteps, this);
            QCOMPARE(native.size(), kSteps);
        }
        QVector<Step> code;
        {
            LinePlantDevice plant(0x5000002u, lockstep());
            LabDeviceRegistry::instance().addPlant(QStringLiteral("code-plant"), &plant);
            const QJsonObject config{
                {"plantDeviceId", "code-plant"},
                {"controller", QJsonObject{{"template", "linefollower/pd"}}},
            };
            LabControllerDevice controller(QStringLiteral("code-pd"), config);
            QVERIFY2(controller.compiled(LabControllerDevice::kController),
                     qPrintable(controller.module(LabControllerDevice::kController)
                                    ? controller.module(LabControllerDevice::kController)->errorText()
                                    : QStringLiteral("no module")));
            QTRY_VERIFY_WITH_TIMEOUT(controller.host()->plantReady(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(controller.host()->configured(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);
            code = run(plant, kSteps, this);
            QCOMPARE(code.size(), kSteps);
            QVERIFY(plant.plant().progress().coverage() > 1.0);
        }
        for (int k = 0; k < kSteps; ++k) {
            QVERIFY2(std::fabs(native[k].uL - code[k].uL) < 1e-9 &&
                         std::fabs(native[k].uR - code[k].uR) < 1e-9,
                     qPrintable(QStringLiteral("step %1: native (%2, %3) code (%4, %5)")
                                    .arg(k + 1)
                                    .arg(native[k].uL)
                                    .arg(native[k].uR)
                                    .arg(code[k].uL)
                                    .arg(code[k].uR)));
        }
        QVERIFY(std::fabs(native.last().uL - native.last().uR) > 1e-6);  // it steered
    }

    // The MPCs in C against the C++ ones, closed loop through BTP in
    // lockstep: the same model, scheduling and QP, so the inputs agree to
    // rounding (the sums run in another order).
    void mpcTemplatesMatchTheNativeOnes_data() {
        QTest::addColumn<QString>("native");
        QTest::addColumn<QString>("templateId");
        QTest::addColumn<QJsonObject>("params");
        QTest::newRow("qlpv taylor") << "qlpv" << "linefollower/qlpv_mpc" << QJsonObject{};
        QTest::newRow("qlpv iterative")
            << "qlpv-iterative" << "linefollower/qlpv_mpc" << QJsonObject{{"scheduling", 3}};
        QTest::newRow("gpc") << "gpc" << "linefollower/gpc" << QJsonObject{};
    }

    void mpcTemplatesMatchTheNativeOnes() {
        QFETCH(QString, native);
        QFETCH(QString, templateId);
        QFETCH(QJsonObject, params);
        constexpr int kSteps = 300;
        static quint32 source = 0x6000000u;
        double nativeMs = 0.0;
        double codeMs = 0.0;
        QVector<Step> expected;
        {
            LinePlantDevice plant(++source, lockstep());
            const QString plantId = QStringLiteral("native-%1").arg(source);
            LineDeviceRegistry::instance().addPlant(plantId, &plant);
            ControllerSettings settings;
            settings.plantDeviceId = plantId;
            settings.type = native.startsWith(QLatin1String("qlpv")) ? QStringLiteral("qlpv")
                                                                     : native;
            if (native == QLatin1String("qlpv-iterative")) {
                settings.qlpvScheduling = QStringLiteral("iterative");
            }
            LineControllerDevice controller(QStringLiteral("native-%1").arg(source), settings);
            QTRY_VERIFY_WITH_TIMEOUT(controller.host()->plantReady(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);
            connect(controller.host(), &ControllerHost::stepped, this,
                    [&nativeMs](quint64, double ms) { nativeMs += ms; });
            expected = run(plant, kSteps, this);
            QCOMPARE(expected.size(), kSteps);
        }
        QVector<Step> got;
        {
            LinePlantDevice plant(++source, lockstep());
            const QString plantId = QStringLiteral("code-%1").arg(source);
            LabDeviceRegistry::instance().addPlant(plantId, &plant);
            LabControllerDevice controller(
                QStringLiteral("code-%1").arg(source),
                QJsonObject{{"plantDeviceId", plantId},
                            {"controller", QJsonObject{{"template", templateId}, {"params", params}}}});
            QVERIFY2(controller.compiled(LabControllerDevice::kController),
                     qPrintable(controller.messages(LabControllerDevice::kController).isEmpty()
                                    ? QString()
                                    : controller.messages(LabControllerDevice::kController)
                                          .first()
                                          .text));
            QTRY_VERIFY_WITH_TIMEOUT(controller.host()->configured(), 5000);
            QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);
            connect(controller.host(), &ControllerHost::stepped, this,
                    [&codeMs](quint64, double ms) { codeMs += ms; });
            got = run(plant, kSteps, this);
            QCOMPARE(got.size(), kSteps);
            QVERIFY(controller.runtimeError(LabControllerDevice::kController).isEmpty());
        }
        double worst = 0.0;
        int worstStep = 0;
        for (int k = 0; k < kSteps; ++k) {
            const double diff = qMax(std::fabs(expected[k].uL - got[k].uL),
                                     std::fabs(expected[k].uR - got[k].uR));
            if (diff > worst) {
                worst = diff;
                worstStep = k + 1;
            }
        }
        qInfo("%s: worst |u_native - u_code| = %.3g %% (step %d); compute %.3f ms/tick native, "
              "%.3f ms/tick in C",
              qPrintable(templateId), worst, worstStep, nativeMs / kSteps, codeMs / kSteps);
        QVERIFY2(worst < 1e-6, qPrintable(QStringLiteral("step %1: %2").arg(worstStep).arg(worst)));
        QVERIFY(std::fabs(expected.last().uL - expected.last().uR) > 1e-6);  // it steered
    }

    // The car in C with its noise on, driven open loop, and the Kalman
    // template on its outputs, step by step in this thread (no clock, no
    // BTP): its estimates against the plant's true values.
    void kalmanTracksTheCar() {
        CodePlant plant;
        QVERIFY2(plant.load(code::CodeLibrary::instance().templateSource(
                                QStringLiteral("linefollower/car")),
                            QStringLiteral("car.c"),
                            {{"tl_track_distance", reinterpret_cast<const void*>(&noTrackDistance)},
                             {"tl_track_line_half_width",
                              reinterpret_cast<const void*>(&noTrackHalfWidth)}}),
                 qPrintable(plant.errorText()));
        QVERIFY(plant.module().setParamValue(QStringLiteral("noise"), 1));
        QVERIFY(plant.reset());

        CodeSignalFilter kalman;
        QVERIFY(kalman.load(code::CodeLibrary::instance().templateSource(
                                QStringLiteral("linefollower/kalman")),
                            QStringLiteral("kalman.c")));
        QStringList out;
        QString error;
        QVERIFY2(kalman.configure(plant.outputNames(), &out, &error), qPrintable(error));
        QVERIFY(out.contains(QStringLiteral("v_est")) && out.contains(QStringLiteral("omega_est")));

        const auto index = [](const QStringList& names, const char* name) {
            const int i = names.indexOf(QLatin1String(name));
            Q_ASSERT(i >= 0);
            return i;
        };
        const QStringList outputs = plant.outputNames();
        const QStringList diags = plant.diagNames();
        const QStringList kalmanDiags = kalman.diagNames();
        const double ts = plant.sampleTime();
        const double r = 0.04, L = 0.15;  // the car's defaults
        QVector<double> vKalman, vWheels, wKalman, wWheels, wGyro, biasError;
        constexpr int kSteps = 2400;  // 30 s at 80 Hz
        for (int k = 1; k <= kSteps; ++k) {
            const double t = k * ts;
            const double uL = 45 + 35 * std::sin(2 * M_PI * 0.4 * t);
            const double uR = 45 + 35 * std::sin(2 * M_PI * 0.27 * t + 1.0);
            QVERIFY(plant.step({uL, uR}));
            QVector<double> filtered;
            QVERIFY(kalman.step(quint64(k), ts, plant.outputs(), &filtered));
            if (t < 3.0) {
                continue;  // settling
            }
            const QVector<double> y = plant.outputs();
            const QVector<double> truth = plant.diags();
            const QVector<double> estimate = kalman.diag();
            const double speed = truth[index(diags, "speed")];
            const double omega = truth[index(diags, "omega")];
            const double wl = y[index(outputs, "w_l")], wr = y[index(outputs, "w_r")];
            vKalman.append(estimate[index(kalmanDiags, "v_kf")] - speed);
            vWheels.append(r * (wl + wr) / 2 - speed);
            wKalman.append(estimate[index(kalmanDiags, "omega_kf")] - omega);
            wWheels.append(r * (wl - wr) / L - omega);
            wGyro.append(y[index(outputs, "gyro")] - omega);
            biasError.append(estimate[index(kalmanDiags, "gyro_bias_kf")] -
                             truth[index(diags, "gyro_bias_true")]);
        }
        qInfo("v: kalman %.4f, wheels %.4f m/s rms", rms(vKalman), rms(vWheels));
        qInfo("omega: kalman %.4f, wheels %.4f, gyro %.4f rad/s rms", rms(wKalman), rms(wWheels),
              rms(wGyro));
        qInfo("gyro bias: error %.4f rad/s rms", rms(biasError));
        QVERIFY(rms(vKalman) < 0.7 * rms(vWheels));
        // The turn rate changes fast here: the gain over the best sensor is
        // the fusion of the two (their noises put it near 0.019) and the
        // gyro's bias taken out.
        QVERIFY(rms(wKalman) < 0.9 * qMin(rms(wWheels), rms(wGyro)));
        QVERIFY(rms(biasError) < 0.01);
    }

    void filterReplacesAndAddsSignals() {
        CodeSignalFilter filter;
        QVERIFY(filter.load(QStringLiteral("#include <tracelab.h>\n"
                                           "TL_INOUT(w_l);\n"
                                           "TL_IN(w_r);\n"
                                           "TL_OUT(w_mean);\n"
                                           "TL_DIAG(calls, \"\");\n"
                                           "void filt_step(void) {\n"
                                           "    w_mean = (w_l + w_r) / 2;\n"
                                           "    w_l = 2 * w_l;\n"
                                           "    calls += 1;\n"
                                           "}\n"),
                            QStringLiteral("f.c")));
        QStringList out;
        QString error;
        QVERIFY2(filter.configure({"w_l", "w_r", "s[0]"}, &out, &error), qPrintable(error));
        QCOMPARE(out, (QStringList{"w_l", "w_r", "s[0]", "w_mean"}));
        QVector<double> values;
        QVERIFY(filter.step(1, 0.0125, {1.0, 3.0, 0.5}, &values));
        QCOMPARE(values, (QVector<double>{2.0, 3.0, 0.5, 2.0}));
        QCOMPARE(filter.diagNames(), QStringList{QStringLiteral("calls")});
        QCOMPARE(filter.diag(), QVector<double>{1.0});
        // A signal it reads that is not there.
        QVERIFY(!filter.configure({"w_r"}, &out, &error));
        QVERIFY2(error.contains(QLatin1String("w_l")), qPrintable(error));
    }

    void misnamedSignalsAreReported() {
        CodeControllerAdapter adapter;
        QVERIFY(adapter.load(QStringLiteral("#include <tracelab.h>\n"
                                            "TL_IN(w_left);\n"
                                            "TL_OUT(u_l); TL_OUT(u_r);\n"
                                            "void ctl_step(void) { u_l = u_r = w_left; }\n"),
                             QStringLiteral("c.c")));
        adapter.setSignalNames({"w_l", "w_r"}, {"u_l", "u_r"});
        QString error;
        QVERIFY(!adapter.configure(2, 2, &error));
        QVERIFY2(error.contains(QLatin1String("w_left")) && error.contains(QLatin1String("w_l")),
                 qPrintable(error));

        QVERIFY(adapter.load(QStringLiteral("#include <tracelab.h>\n"
                                            "TL_OUT(u_l);\n"
                                            "void ctl_step(void) { u_l = 1; }\n"),
                             QStringLiteral("c.c")));
        adapter.setSignalNames({"w_l"}, {"u_l", "u_r"});
        QVERIFY(!adapter.configure(1, 2, &error));
        QVERIFY2(error.contains(QLatin1String("u_r")), qPrintable(error));

        QVERIFY(adapter.load(QStringLiteral("#include <tracelab.h>\n"
                                            "TL_OUT(u_l); TL_OUT(u_r); TL_OUT(u_x);\n"
                                            "void ctl_step(void) { u_l = u_r = u_x = 1; }\n"),
                             QStringLiteral("c.c")));
        adapter.setSignalNames({"w_l"}, {"u_l", "u_r"});
        QVERIFY(!adapter.configure(1, 2, &error));
        QVERIFY2(error.contains(QLatin1String("u_x")), qPrintable(error));
    }

    void planAndFaultHold() {
        CodeControllerAdapter adapter;
        QVERIFY(adapter.load(QStringLiteral("#include <tracelab.h>\n"
                                            "TL_PARAM_BOOL(crash, 0);\n"
                                            "TL_IN(w_l);\n"
                                            "TL_OUT_PLAN(u_l, 3);\n"
                                            "TL_OUT(u_r);\n"
                                            "void ctl_step(void) {\n"
                                            "    for (int i = 0; i < 3; i++) u_l[i] = w_l + i;\n"
                                            "    u_r = -1;\n"
                                            "    if (crash) { volatile int z = 0; u_r = 1 / z; }\n"
                                            "}\n"),
                             QStringLiteral("plan.c")));
        adapter.setSignalNames({"w_l"}, {"u_l", "u_r"});
        QString error;
        QVERIFY2(adapter.configure(1, 2, &error), qPrintable(error));
        ControllerInput input;
        input.tick = 1;
        input.y = {10.0};
        ControllerOutput output;
        QVERIFY(adapter.compute(input, &output));
        QCOMPARE(output.u.size(), 3);
        QCOMPARE(output.u[2], (QVector<double>{12.0, -1.0}));  // u_r held over the plan
        adapter.setParam(QStringLiteral("crash"), 1);
        QVERIFY(!adapter.compute(input, &output));
        QVERIFY2(adapter.runtimeErrorText().startsWith(QLatin1String("plan.c:9:")),
                 qPrintable(adapter.runtimeErrorText()));
        adapter.setParam(QStringLiteral("crash"), 0);
        QVERIFY(!adapter.compute(input, &output));  // stopped until reset
        adapter.reset();
        QVERIFY(adapter.compute(input, &output));
    }

    void savedFileIsCompiledAgain() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("mine.c"));
        const QString good = code::CodeLibrary::instance().templateSource(QStringLiteral("linefollower/pd"));
        QVERIFY(!good.isEmpty());
        QVERIFY(code::CodeLibrary::writeFile(path, good));
        LinePlantDevice plant(0x5000003u, lockstep());
        LabDeviceRegistry::instance().addPlant(QStringLiteral("reload-plant"), &plant);
        LabControllerDevice controller(
            QStringLiteral("reload"),
            QJsonObject{{"plantDeviceId", "reload-plant"},
                        {"controller", QJsonObject{{"path", path}, {"params", QJsonObject{{"base", 40}}}}}});
        QVERIFY(controller.compiled(LabControllerDevice::kController));
        QCOMPARE(controller.module(LabControllerDevice::kController)->paramValue(QStringLiteral("base")), 40.0);
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->configured(), 5000);

        // A broken save: the old code keeps running, the messages say why.
        QSignalSpy blocks(&controller, &LabControllerDevice::blocksChanged);
        QVERIFY(code::CodeLibrary::writeFile(path, good + QStringLiteral("\nthis is not C\n")));
        QTRY_VERIFY_WITH_TIMEOUT(blocks.count() >= 1, 5000);
        QVERIFY(controller.controller() != nullptr);
        QVERIFY(controller.host()->configured());
        const auto messages = controller.messages(LabControllerDevice::kController);
        QVERIFY(!messages.isEmpty());
        QVERIFY(messages.first().line > 0);

        // A good save with a new parameter: it replaces the running code and
        // the value set by hand survives.
        blocks.clear();
        QString renamed = good;
        renamed.replace(QStringLiteral("TL_DIAG(line_seen, \"\");"),
                        QStringLiteral("TL_DIAG(line_seen, \"\");\nTL_DIAG(extra, \"\");"));
        QVERIFY(code::CodeLibrary::writeFile(path, renamed));
        QTRY_VERIFY_WITH_TIMEOUT(blocks.count() >= 1, 5000);
        QVERIFY(controller.host()->diagNames().contains(QStringLiteral("extra")));
        QCOMPARE(controller.module(LabControllerDevice::kController)->paramValue(QStringLiteral("base")), 40.0);
        QVERIFY(controller.host()->configured());
    }
};

QTEST_MAIN(TestCodeController)
#include "test_codecontroller.moc"
