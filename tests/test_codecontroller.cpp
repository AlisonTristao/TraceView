// Controllers and filters written in C (lab/control/codecontrolleradapter.h,
// codesignalfilter.h, labdevices.h) closing the loop on the line follower
// through BTP: the PD template step for step equal to the C++ PD, a filter
// replacing and adding signals by name, the errors a misnamed signal gives,
// a fault holding the plant's input until reset, and a saved file compiled
// again while running (a broken version leaving the old one in charge).

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
