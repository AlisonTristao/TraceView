// The line follower as TraceView devices (lab/benchmarks/linefollower/
// device): the plant's manifest (topics and actions, loop.u STREAM), the plant
// served over the in-process loopback to TraceView's own BtpBackend through a
// TransportType::Module DeviceConnection, a closed loop through BTP (plant +
// controller host, lockstep) matching the in-process harness step for step,
// and sim.disturb from the device's command path.

#include <QSignalSpy>
#include <QtTest>
#include <cmath>

#include "backend/backend.h"
#include "core/deviceconnection.h"
#include "core/moduledevicetype.h"
#include "devices/device.h"
#include "lab/benchmarks/linefollower/device/lfdevices.h"
#include "lab/benchmarks/linefollower/device/lfplantmanifest.h"
#include "lab/control/controllerhost.h"
#include "lfcontroller.h"
#include "pdcontroller.h"

using namespace tracelab::lf;

namespace {

void registerTypes() {
    for (const traceview::ModuleDeviceType& type : lineFollowerDeviceTypes()) {
        traceview::ModuleDeviceTypeRegistry::instance().registerType(type);
    }
}

traceview::Device plantDevice(const QString& id, const QJsonObject& config) {
    traceview::Device device;
    device.id = id;
    device.name = QStringLiteral("TraceLab: Line follower");
    device.transportType = traceview::TransportType::Module;
    device.moduleType = QString::fromLatin1(kPlantDeviceType);
    device.moduleConfig = config;
    return device;
}

QStringList topicNames(const traceview::Backend* backend) {
    QStringList names;
    for (const traceview::CatalogTopicInfo& topic : backend->catalogTopics()) {
        names.append(topic.name);
    }
    return names;
}

// The in-process harness's loop (lfrunner.cpp): u = 0, step, controller on the
// encoders' wheel speeds and the future points, step...
struct HarnessStep {
    double x, y, theta, uL, uR;
};
QVector<HarnessStep> harness(int steps) {
    PlantSettings settings;
    LineFollowerPlant plant(generateTrack(settings.track), settings.plantParams());
    ControllerConfig config;
    config.ts = 1.0 / settings.fps;
    config.horizon = settings.horizon;
    auto controller = makeController(config);
    std::array<double, 2> u{0.0, 0.0};
    QVector<HarnessStep> out;
    for (int k = 0; k < steps; ++k) {
        plant.step(u[0], u[1]);
        out.append({plant.pose().x, plant.pose().y, plant.pose().theta, u[0], u[1]});
        std::vector<CarPoint> future;
        for (const CarFramePoint& p : plant.data().futurePoints) {
            future.push_back({p.x, p.y});
        }
        u = controller->step({plant.measured().wheelSpeedLeft, plant.measured().wheelSpeedRight},
                             future);
    }
    return out;
}

class TestLfDevices : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        registerTypes();
    }

    void manifestDeclaresTheLoopProfile() {
        PlantCatalog catalog;
        QVERIFY(buildPlantCatalog(catalog));
        QStringList topics;
        for (std::size_t i = 0; i < catalog.topic_count(); ++i) {
            topics.append(QString::fromUtf8(catalog.topic_at(i)->name));
        }
        QCOMPARE(topics, (QStringList{"loop.state", "loop.output", "sim.truth", "sim.states",
                                      "sim.preview", "sim.params", "sim.track"}));
        QStringList actions;
        for (std::size_t i = 0; i < catalog.action_count(); ++i) {
            actions.append(QString::fromUtf8(catalog.action_at(i)->name));
        }
        QCOMPARE(actions, (QStringList{"loop.u", "loop.mode", "loop.config", "sim.run",
                                       "sim.pause", "sim.reset", "sim.step", "sim.disturb",
                                       "sim.speed"}));
        const btp::CatalogAction* u = catalog.action(action::kLoopU, 1);
        QVERIFY(u != nullptr);
        QVERIFY((u->flags & btp::kActionStream) != 0);
        QCOMPARE(int(u->parameter_count), 3);
        QVERIFY((catalog.action(action::kLoopMode, 1)->flags & btp::kActionDangerous) != 0);
    }

    void plantIsADeviceOverLoopback() {
        traceview::DeviceConnection connection(
            plantDevice(QStringLiteral("plant-manifest"), QJsonObject{{"autoRun", false}}));
        connection.connectToModule();
        QVERIFY(connection.isConnected());
        QTRY_VERIFY_WITH_TIMEOUT(topicNames(connection.backend()).contains("sim.track"), 5000);
        const QStringList names = topicNames(connection.backend());
        for (const char* name : {"loop.state", "loop.output", "sim.truth", "sim.states",
                                 "sim.preview", "sim.params"}) {
            QVERIFY2(names.contains(QLatin1String(name)), name);
        }
        quint32 source = 0;
        for (const traceview::CatalogTopicInfo& topic : connection.backend()->catalogTopics()) {
            if (topic.name == QLatin1String("sim.truth")) {
                source = topic.sourceId;
                QCOMPARE(topic.sourceId,
                         deviceSourceId(QStringLiteral("plant-manifest"), kPlantDeviceType));
            }
        }
        // sim.track arrives as a UTF8 document once subscribed.
        QSignalSpy text(connection.backend(), &traceview::Backend::textSample);
        connection.backend()->addSubscriber(source, topic::kSimTrack, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!text.isEmpty(), 5000);
        const QString track = text.first().at(3).toString();
        QVERIFY(track.startsWith(QLatin1String("{\"points\":[[")));
        QVERIFY(track.contains(QLatin1String("\"stride\":")));
        connection.disconnectFrom();
    }

    void disturbGoesThroughTheCommandPath() {
        const QString id = QStringLiteral("plant-disturb");
        traceview::DeviceConnection connection(plantDevice(id, QJsonObject{{"autoRun", false}}));
        connection.connectToModule();
        QTRY_VERIFY_WITH_TIMEOUT(topicNames(connection.backend()).contains("sim.truth"), 5000);
        LinePlantDevice* plant = LineDeviceRegistry::instance().plant(id);
        QVERIFY(plant != nullptr);
        QCOMPARE(plant->perturbation(), 0.0);
        connection.backend()->sendCommand("sim.disturb 5");
        QTRY_COMPARE_WITH_TIMEOUT(plant->perturbation(), 5.0, 3000);
        // One command at a time: the next goes once the result is back.
        QTest::qWait(50);
        connection.backend()->sendCommand("sim.disturb -12.5");
        QTRY_COMPARE_WITH_TIMEOUT(plant->perturbation(), -12.5, 3000);
        // The perturbation reaches the motors (left +q, right -q).
        QTest::qWait(50);
        connection.backend()->sendCommand("sim.step");
        QTRY_COMPARE_WITH_TIMEOUT(plant->tick(), 1LL, 3000);
        const auto wheels = plant->plant().car().wheelsSpeed();
        QVERIFY(wheels.first < 0.0);
        QVERIFY(wheels.second > 0.0);
        connection.disconnectFrom();
    }









    void pdFollowsTheLineThroughBtp() {
        PlantSettings settings;
        settings.runMode = RunMode::Lockstep;
        settings.autoRun = false;
        LinePlantDevice plant(0x3234567u, settings);
        LineDeviceRegistry::instance().addPlant(QStringLiteral("pd-plant"), &plant);
        ControllerSettings controllerSettings;
        controllerSettings.plantDeviceId = QStringLiteral("pd-plant");
        controllerSettings.type = QStringLiteral("pd");
        LineControllerDevice controller(QStringLiteral("pd-controller"), controllerSettings);
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->plantReady(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);  // REMOTE, armed
        double worstError = 0.0;
        connect(&plant, &LinePlantDevice::ticked, this, [&](long long tick) {
            if (tick > 40) {  // after the start transient
                worstError = qMax(worstError, std::fabs(plant.plant().data().lateralError));
            }
            if (tick >= 800) {  // 10 s
                plant.pause();
            }
        });
        plant.run();
        QTRY_VERIFY_WITH_TIMEOUT(plant.tick() >= 800, 60000);
        // On the line the whole time (the line is 4 cm wide) and moving.
        QVERIFY2(worstError < 0.02, qPrintable(QString::number(worstError)));
        QVERIFY(plant.plant().progress().coverage() > 5.0);
        QVERIFY(controller.host()->stepsAnswered() >= 799);
    }

    void closedLoopThroughBtpMatchesTheHarness() {
        constexpr int kSteps = 40;
        const QVector<HarnessStep> expected = harness(kSteps);

        PlantSettings settings;
        settings.runMode = RunMode::Lockstep;
        settings.autoRun = false;
        LinePlantDevice plant(0x1234567u, settings);
        LineDeviceRegistry::instance().addPlant(QStringLiteral("loop-plant"), &plant);
        ControllerSettings controllerSettings;
        controllerSettings.plantDeviceId = QStringLiteral("loop-plant");
        LineControllerDevice controller(QStringLiteral("loop-controller"), controllerSettings);
        QTRY_VERIFY_WITH_TIMEOUT(controller.host()->plantReady(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(int(plant.mode()), 2, 5000);  // REMOTE, armed

        QVector<HarnessStep> got;
        connect(&plant, &LinePlantDevice::ticked, this, [&](long long) {
            got.append({plant.plant().pose().x, plant.plant().pose().y,
                        plant.plant().pose().theta, plant.appliedInput()[0],
                        plant.appliedInput()[1]});
            if (got.size() >= kSteps) {
                plant.pause();
            }
        });
        plant.run();
        QTRY_VERIFY_WITH_TIMEOUT(got.size() >= kSteps, 60000);
        QCOMPARE(controller.host()->stepsAnswered() >= quint64(kSteps - 1), true);
        for (int k = 0; k < kSteps; ++k) {
            const HarnessStep& a = got[k];
            const HarnessStep& b = expected[k];
            QVERIFY2(std::fabs(a.x - b.x) < 1e-9 && std::fabs(a.y - b.y) < 1e-9 &&
                         std::fabs(a.theta - b.theta) < 1e-9 && std::fabs(a.uL - b.uL) < 1e-9 &&
                         std::fabs(a.uR - b.uR) < 1e-9,
                     qPrintable(QStringLiteral("step %1: (%2, %3, %4 | %5, %6) vs (%7, %8, %9 | "
                                               "%10, %11)")
                                    .arg(k + 1)
                                    .arg(a.x)
                                    .arg(a.y)
                                    .arg(a.theta)
                                    .arg(a.uL)
                                    .arg(a.uR)
                                    .arg(b.x)
                                    .arg(b.y)
                                    .arg(b.theta)
                                    .arg(b.uL)
                                    .arg(b.uR)));
        }
        // The car really moved under the controller.
        QVERIFY(std::fabs(expected.last().uL) > 1.0);
    }
};

}  // namespace

QTEST_MAIN(TestLfDevices)
#include "test_lfdevices.moc"
