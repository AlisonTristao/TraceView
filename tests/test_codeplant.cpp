// Plants written in C (lab/plant/codeplant.h, codeplantdevice.h) and the
// line follower's track environment (trackenvironment.h): a continuous plant
// integrated by RK4 against its exact solution, a discrete one, inputs held
// within their bounds; a plant device with no environment closing the loop
// with a controller in C through BTP, bound by the names its code declares,
// and rebinding when a new version of the plant declares other outputs;
// the line follower's car in C on the track equal to the C++ plant, sample
// for sample, under the same controller; and the benchmark project's own
// devices (noise on, the Kalman filter beside the MPC) on the line.

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "lab/benchmarks/linefollower/code/lftemplates.h"
#include "lab/benchmarks/linefollower/device/lfplantdevice.h"
#include "lab/benchmarks/linefollower/device/trackenvironment.h"
#include "lab/code/codefiles.h"
#include "lab/control/codesignalfilter.h"
#include "lab/control/controllerhost.h"
#include "lab/control/labdevices.h"
#include "lab/plant/codeplant.h"
#include "lab/plant/codeplantdevice.h"

using namespace tracelab;

namespace {

const char* const kFirstOrder = R"(#include <tracelab.h>
TL_PARAM(tau, 0.5, "s", 0.01, 10);
TL_INPUT(u, "", -1, 1);
TL_STATE(w, "", 0);
TL_OUTPUT(w_meas, "");
void plant_deriv(void) { der(w) = (u - w) / tau; }
void plant_output(void) { w_meas = w; }
)";

const char* const kController = R"(#include <tracelab.h>
TL_PARAM(target, 0.5, "", -1, 1);
TL_PARAM(kp, 4, "", 0, 100);
TL_IN(w_meas);
TL_OUT(u);
void ctl_step(void) { u = kp * (target - w_meas); }
)";

}  // namespace

class TestCodePlant : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        lf::registerLineFollowerTemplates();
        lf::registerTrackEnvironment();
    }

    void rk4AgainstTheExactSolution() {
        CodePlant plant;
        QVERIFY2(plant.load(QString::fromLatin1(kFirstOrder), QStringLiteral("first.c")),
                 qPrintable(plant.errorText()));
        QVERIFY(!plant.isDiscrete());
        plant.setSampleTime(0.01);
        plant.setSolver(CodePlant::Solver::Rk4, 4);
        QVERIFY(plant.reset());
        for (int k = 0; k < 100; ++k) {
            QVERIFY(plant.step({1.0}));
        }
        // w(t) = 1 - exp(-t / tau) at t = 1 s.
        QCOMPARE(plant.tick(), 100LL);
        QVERIFY(std::fabs(plant.state(QStringLiteral("w")) - (1.0 - std::exp(-2.0))) < 1e-9);
        QCOMPARE(plant.outputs().first(), plant.state(QStringLiteral("w")));
        // Inputs are held within their bounds.
        QVERIFY(plant.reset());
        QVERIFY(plant.step({5.0}));
        QCOMPARE(plant.appliedInputs().first(), 1.0);
        // Euler is first order: off by more.
        plant.setSolver(CodePlant::Solver::Euler, 1);
        QVERIFY(plant.reset());
        for (int k = 0; k < 100; ++k) {
            QVERIFY(plant.step({1.0}));
        }
        const double error = std::fabs(plant.state(QStringLiteral("w")) - (1.0 - std::exp(-2.0)));
        QVERIFY(error > 1e-4 && error < 1e-2);
    }

    void refusesIncompletePlants() {
        CodePlant plant;
        QVERIFY(!plant.load(QStringLiteral("#include <tracelab.h>\nTL_DISCRETE;\nTL_STATE(x, \"\", 0);\n"
                                           "TL_OUTPUT(y, \"\");\n"),
                            QStringLiteral("p.c")));
        QVERIFY(plant.errorText().contains(QLatin1String("plant_update")));
        QVERIFY(!plant.load(QStringLiteral("#include <tracelab.h>\nTL_STATE(x, \"\", 0);\n"
                                           "void plant_deriv(void) { der(x) = 1; }\n"),
                            QStringLiteral("p.c")));
        QVERIFY(plant.errorText().contains(QLatin1String("TL_OUTPUT")));
    }

    void genericLoopThroughBtp() {
        QTemporaryDir dir;
        const QString plantPath = dir.filePath(QStringLiteral("first.c"));
        const QString controllerPath = dir.filePath(QStringLiteral("p.c"));
        QVERIFY(code::CodeLibrary::writeFile(plantPath, QString::fromLatin1(kFirstOrder)));
        QVERIFY(code::CodeLibrary::writeFile(controllerPath, QString::fromLatin1(kController)));
        CodePlantDevice plant(QStringLiteral("generic-plant"), 0x7100001u,
                              QJsonObject{{"plant", QJsonObject{{"path", plantPath}}},
                                          {"runMode", "lockstep"},
                                          {"fps", 100}});
        QVERIFY(plant.codeBlocks()->compiled(0));
        LabDeviceRegistry::instance().addPlant(QStringLiteral("generic-plant"), &plant);
        LabControllerDevice controller(
            QStringLiteral("generic-ctl"),
            QJsonObject{{"plantDeviceId", "generic-plant"},
                        {"controller", QJsonObject{{"path", controllerPath}}}});
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->configured(), 5000);
        QCOMPARE(controller.host()->measurementNames(), QStringList{QStringLiteral("w_meas")});
        QCOMPARE(controller.host()->inputNames(), QStringList{QStringLiteral("u")});
        QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);
        plant.run();
        QTRY_VERIFY_WITH_TIMEOUT(plant.tick() >= 300, 30000);
        plant.pause();
        // P control of a first-order plant: w -> kp/(1+kp) * target = 0.4.
        QVERIFY2(std::fabs(plant.plant()->state(QStringLiteral("w")) - 0.4) < 1e-3,
                 qPrintable(QString::number(plant.plant()->state(QStringLiteral("w")))));

        // A new version of the plant with another output name: the manifest
        // is rebuilt, the controller rebinds and says what is missing.
        QString renamed = QString::fromLatin1(kFirstOrder);
        renamed.replace(QStringLiteral("w_meas"), QStringLiteral("speed"));
        QVERIFY(code::CodeLibrary::writeFile(plantPath, renamed));
        plant.codeBlocks()->reload(0);
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->measurementNames() ==
                                     QStringList{QStringLiteral("speed")},
                                 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.host()->configured(), 5000);
        QVERIFY2(controller.host()->configureError().contains(QLatin1String("w_meas")),
                 qPrintable(controller.host()->configureError()));
    }

    void carOnTheTrackMatchesTheNativePlant() {
        constexpr int kSteps = 400;
        struct Sample {
            double uL, uR, x, y, theta;
            // The car in C only: its accelerometer and the true v, omega.
            double accelX = 0, accelY = 0, speed = 0, omega = 0;
        };
        static quint32 source = 0x7200000u;
        const auto runLoop = [&](LabPlantDevice& plant, const QString& plantId,
                                 const std::function<Sample()>& sample) {
            QVector<Sample> got;
            LabDeviceRegistry::instance().addPlant(plantId, &plant);
            LabControllerDevice controller(
                QStringLiteral("ctl-%1").arg(++source),
                QJsonObject{{"plantDeviceId", plantId},
                            {"controller", QJsonObject{{"template", "linefollower/qlpv_mpc"}}}});
            QTest::qWaitFor([&] { return controller.host()->configured(); }, 5000);
            QTest::qWaitFor([&] { return controller.host()->stepsAnswered() == 0 &&
                                         controller.host()->plantReady(); }, 5000);
            QObject::connect(&plant, &LabPlantDevice::ticked, &controller, [&](long long tick) {
                if (tick > 0) {
                    got.append(sample());
                }
                if (got.size() >= kSteps) {
                    plant.pause();
                }
            });
            QTest::qWait(200);  // REMOTE armed
            plant.run();
            QTest::qWaitFor([&] { return got.size() >= kSteps; }, 120000);
            plant.pause();
            return got;
        };

        lf::PlantSettings settings;
        settings.runMode = RunMode::Lockstep;
        settings.autoRun = false;
        lf::LinePlantDevice native(0x7200001u, settings);
        const QVector<Sample> expected = runLoop(native, QStringLiteral("car-native"), [&] {
            return Sample{native.appliedInput()[0], native.appliedInput()[1],
                          native.plant().pose().x, native.plant().pose().y,
                          native.plant().pose().theta};
        });
        QCOMPARE(expected.size(), kSteps);

        CodePlantDevice code(
            QStringLiteral("car-code"), 0x7200002u,
            QJsonObject{{"plant", QJsonObject{{"template", "linefollower/car"}}},
                        {"environment", QJsonObject{{"id", "linefollower.track"},
                                                    {"seed", qint64(settings.track.seed)},
                                                    {"horizon", settings.horizon},
                                                    {"futureSpacing", settings.futureSpacing}}},
                        {"runMode", "lockstep"},
                        {"autoRun", false}});
        QVERIFY2(code.codeBlocks()->compiled(0),
                 qPrintable(code.codeBlocks()->messages(0).isEmpty()
                                ? QString()
                                : code.codeBlocks()->messages(0).first().text));
        const QVector<Sample> got = runLoop(code, QStringLiteral("car-code"), [&] {
            const CodePlant* p = code.plant();
            const auto output = [p](const char* name) {
                return p->outputs().value(p->outputNames().indexOf(QLatin1String(name)), qQNaN());
            };
            const auto diag = [p](const char* name) {
                return p->diags().value(p->diagNames().indexOf(QLatin1String(name)), qQNaN());
            };
            return Sample{code.appliedInput()[0], code.appliedInput()[1],
                          p->state(QStringLiteral("x")), p->state(QStringLiteral("y")),
                          p->state(QStringLiteral("theta")), output("accel_x"),
                          output("accel_y"), diag("speed"), diag("omega")};
        });
        QCOMPARE(got.size(), kSteps);
        double worst = 0.0;
        int worstStep = 0;
        for (int k = 0; k < kSteps; ++k) {
            const double diff = qMax(qMax(std::fabs(expected[k].uL - got[k].uL),
                                          std::fabs(expected[k].uR - got[k].uR)),
                                     qMax(std::fabs(expected[k].x - got[k].x),
                                          std::fabs(expected[k].theta - got[k].theta)));
            if (diff > worst) {
                worst = diff;
                worstStep = k + 1;
            }
        }
        qInfo("car in C vs C++ plant: worst difference %.3g (step %d)", worst, worstStep);
        QVERIFY2(worst < 1e-9, qPrintable(QStringLiteral("step %1: %2").arg(worstStep).arg(worst)));
        QVERIFY(std::fabs(got.last().x - got.first().x) > 0.1);  // it went somewhere

        // The accelerometer (noise off): along the car the speed's change
        // over the step, across it v * omega.
        const double ts = code.sampleTime();
        double worstAccel = 0.0;
        for (int k = 1; k < kSteps; ++k) {
            worstAccel = qMax(worstAccel, std::fabs(got[k].accelX -
                                                    (got[k].speed - got[k - 1].speed) / ts));
            worstAccel = qMax(worstAccel, std::fabs(got[k].accelY - got[k].speed * got[k].omega));
        }
        QVERIFY2(worstAccel < 1e-9, qPrintable(QString::number(worstAccel)));
        QVERIFY(std::isfinite(got.last().accelY));
    }

    void benchmarkProjectFollowsTheLine_data() {
        QTest::addColumn<bool>("noiseOff");
        QTest::newRow("as shipped") << false;
        QTest::newRow("noise off") << true;
    }

    // The benchmark project's own devices (the car with its sensors' noise,
    // the qLPV MPC and the Kalman filter beside it) in lockstep: on the line
    // the whole time, and the filter's speed close to the car's.
    void benchmarkProjectFollowsTheLine() {
        QFETCH(bool, noiseOff);
        QFile file(QStringLiteral(TRACELAB_LF_PROJECT));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonObject plantConfig, controllerConfig;
        QString plantId, controllerId;
        const QJsonArray devices = QJsonDocument::fromJson(file.readAll())
                                       .object().value("devices").toObject().value("devices")
                                       .toArray();
        for (const QJsonValue& value : devices) {
            const QJsonObject device = value.toObject();
            if (device.value("moduleType").toString() == QLatin1String("tracelab.plant")) {
                plantId = device.value("id").toString();
                plantConfig = device.value("moduleConfig").toObject();
            } else if (device.value("moduleType").toString() ==
                       QLatin1String("tracelab.controller")) {
                controllerId = device.value("id").toString();
                controllerConfig = device.value("moduleConfig").toObject();
            }
        }
        QVERIFY(!plantId.isEmpty() && !controllerId.isEmpty());
        QCOMPARE(controllerConfig.value("plantDeviceId").toString(), plantId);
        QCOMPARE(controllerConfig.value("filters").toArray().size(), 1);
        QJsonObject spec = plantConfig.value("plant").toObject();
        QCOMPARE(spec.value("params").toObject().value("noise").toInt(), 1);
        if (noiseOff) {
            spec.insert("params", QJsonObject{{"noise", 0}});
            plantConfig.insert("plant", spec);
        }
        plantConfig.insert("runMode", "lockstep");
        plantConfig.insert("autoRun", false);
        plantId += noiseOff ? QStringLiteral("-quiet") : QStringLiteral("-noisy");
        controllerConfig.insert("plantDeviceId", plantId);

        CodePlantDevice plant(plantId, noiseOff ? 0x7300001u : 0x7300002u, plantConfig);
        QVERIFY(plant.codeBlocks()->compiled(0));
        auto* track = dynamic_cast<lf::TrackEnvironment*>(plant.environment());
        QVERIFY(track);
        LabDeviceRegistry::instance().addPlant(plantId, &plant);
        LabControllerDevice controller(controllerId + plantId, controllerConfig);
        QVERIFY(controller.compiled(0));
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->configured(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);  // REMOTE, armed

        constexpr long long kTicks = 1600;  // 20 s at 80 Hz
        const CodePlant* car = plant.plant();
        const int speedIndex = car->diagNames().indexOf(QLatin1String("speed"));
        const int omegaIndex = car->diagNames().indexOf(QLatin1String("omega"));
        const int gyroIndex = car->outputNames().indexOf(QLatin1String("gyro"));
        const int vIndex = controller.filter(0)->diagNames().indexOf(QLatin1String("v_kf"));
        QVERIFY(speedIndex >= 0 && omegaIndex >= 0 && gyroIndex >= 0 && vIndex >= 0);
        double worstLateral = 0.0, speedError = 0.0, gyroError = 0.0;
        double lastSpeed = 0.0;
        int samples = 0;
        connect(&plant, &LabPlantDevice::ticked, &controller, [&](long long tick) {
            const double speed = car->diags().value(speedIndex);
            if (tick > 80) {  // after the start
                worstLateral = qMax(worstLateral, track->distance(car->state(QStringLiteral("x")),
                                                                  car->state(QStringLiteral("y"))));
                // The controller answers tick k after the plant says it
                // ticked: the filter's estimate is still the last tick's.
                const double v = controller.filter(0)->diag().value(vIndex);
                speedError += (v - lastSpeed) * (v - lastSpeed);
                const double gyro = car->outputs().value(gyroIndex) - car->diags().value(omegaIndex);
                gyroError += gyro * gyro;
                ++samples;
            }
            lastSpeed = speed;
            if (tick >= kTicks) {
                plant.pause();
            }
        });
        plant.run();
        QTRY_VERIFY_WITH_TIMEOUT(plant.tick() >= kTicks, 120000);
        plant.pause();
        speedError = std::sqrt(speedError / qMax(1, samples));
        gyroError = std::sqrt(gyroError / qMax(1, samples));
        qInfo("worst lateral %.4f m (half width %.4f), Kalman speed %.4f m/s rms, gyro noise "
              "%.4f rad/s rms",
              worstLateral, track->lineHalfWidth(), speedError, gyroError);
        QVERIFY2(worstLateral < track->lineHalfWidth(), qPrintable(QString::number(worstLateral)));
        QVERIFY2(speedError < 0.005, qPrintable(QString::number(speedError)));
        if (noiseOff) {
            QCOMPARE(gyroError, 0.0);
        } else {
            QVERIFY2(gyroError > 0.01, qPrintable(QString::number(gyroError)));
        }
    }
};

QTEST_MAIN(TestCodePlant)
#include "test_codeplant.moc"
