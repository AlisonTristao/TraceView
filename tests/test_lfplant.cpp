// The line-follower plant (lab/benchmarks/linefollower/plant): the motors
// follow car_dynamics.py's discrete equations; integrating the car's world
// pose is the pygame simulator's track transform (a literal port of
// Track.step / car_pose / to_car_frame here); the Cluster reach rule and the
// future points in the car frame; and, when line_follower_simulator is
// checked out next to this repository, a replay of its saved runs (the
// logged inputs fed back open loop must give the logged wheel speeds, pose,
// coverage and lateral error).

#include <QDir>
#include <QFile>
#include <QHash>
#include <QTextStream>
#include <QtTest/QtTest>
#include <cmath>

#include "lab/benchmarks/linefollower/plant/cardynamics.h"
#include "lab/benchmarks/linefollower/plant/linefollowerplant.h"
#include "lab/benchmarks/linefollower/plant/trackgenerator.h"
#include "lab/benchmarks/linefollower/plant/trackprogress.h"

using namespace tracelab::lf;

namespace {

// graphics_elements.Track as the simulator moves it: world pixels, the
// track's angle a, the car fixed at screen point C.
struct PygameWorld {
    double scale = 300.0;
    int half = 50;  // LENGTH // 2
    double tx = 0.0;
    double ty = 0.0;
    double angle = 0.0;
    double cx = 1.36 * 700 + std::cos(30.0 * M_PI / 180.0);  // Car center, MEDIUM screen
    double cy = 1.8 * 375;

    void start(double x0, double y0) {
        tx = (x0 + half) * scale;
        ty = (y0 + half) * scale;
        angle = 0.0;
    }
    // SimulatorController.step: track.step(-dx * S, -dy * S, -angle).
    void step(const CarDynamics::Space& space) {
        const double mx = -space.dx * scale;
        const double my = -space.dy * scale;
        angle += -space.angle;  // Shape._rotate
        const double c = std::cos(-angle);
        const double s = std::sin(-angle);
        tx += mx * c - my * s;  // Shape._move
        ty += mx * s + my * c;
    }
    // SimulatorController.car_pose().
    Pose pose() const {
        return {tx / scale - half, ty / scale - half,
                std::atan2(-std::cos(angle), -std::sin(angle))};
    }
    // A world point (m) on screen (Track.draw + Cluster._to_screen), then
    // to_car_frame.
    Point2 carFrame(const Point2& world) const {
        const double wx = (world.x + half) * scale - tx;
        const double wy = (world.y + half) * scale - ty;
        const double c = std::cos(angle);
        const double s = std::sin(angle);
        const double sx = cx + wx * c - wy * s;
        const double sy = cy + wx * s + wy * c;
        return {(sx - cx) / scale, (cy - sy) / scale};
    }
};

double angleDiff(double a, double b) {
    return std::fabs(std::atan2(std::sin(a - b), std::cos(a - b)));
}

// A straight track along +x from the origin, a point every `ds` metres.
Track straightTrack(int count, double ds) {
    Track t;
    for (int i = 0; i < count; ++i) {
        t.x.push_back(i * ds);
        t.y.push_back(0.0);
    }
    return t;
}

struct CsvTable {
    QHash<QString, int> column;
    QVector<QVector<double>> rows;
    double at(int row, const char* name) const { return rows[row][column.value(name)]; }
};

bool readCsv(const QString& path, CsvTable* table) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QTextStream in(&file);
    const QStringList header = in.readLine().split(QLatin1Char(','));
    for (int i = 0; i < header.size(); ++i) {
        table->column.insert(header[i].trimmed(), i);
    }
    while (!in.atEnd()) {
        const QStringList cells = in.readLine().split(QLatin1Char(','));
        if (cells.size() < header.size()) {
            continue;
        }
        QVector<double> row;
        for (const QString& c : cells) {
            row.append(c.toDouble());
        }
        table->rows.append(row);
    }
    return true;
}

}  // namespace

class TestLfPlant : public QObject {
    Q_OBJECT

private slots:
    void motorsFollowTheDiscreteEquations() {
        CarDynamicsParams p;
        p.ts = 1.0 / 80;
        CarDynamics car(p);
        const double aL = std::exp(-p.ts / (p.accommodationTimeLeft / 5));
        const double aR = std::exp(-p.ts / (p.accommodationTimeRight / 5));
        QCOMPARE(car.left().a(), aL);
        QCOMPARE(car.left().b(), p.keLeft * (1 - aL));
        QCOMPARE(car.left().c(), p.kq * (1 - aL));
        QCOMPARE(car.right().a(), aR);

        double yL = 0.0;
        double yR = 0.0;
        const double inputs[][3] = {{50, 30, 0}, {150, -20, 5}, {-130, 100, -10}, {0, 0, 20}};
        for (const auto& in : inputs) {
            car.step(in[0], in[1], in[2], -in[2]);
            const auto sat = [](double u) { return std::max(-100.0, std::min(100.0, u)); };
            yL = aL * yL + p.keLeft * (1 - aL) * sat(in[0]) + p.kq * (1 - aL) * in[2];
            yR = aR * yR + p.keRight * (1 - aR) * sat(in[1]) + p.kq * (1 - aR) * -in[2];
            QCOMPARE(car.left().y(), yL);
            QCOMPARE(car.right().y(), yR);
            const double w1 = 2 * M_PI * p.wheelsRpm / (60 * 100);
            QCOMPARE(car.speed(), (yL + yR) * (w1 * p.wheelsRadius / 2));
            QCOMPARE(car.omega(), (yL - yR) * (w1 * p.wheelsRadius / p.wheelsDistance));
            QCOMPARE(car.wheelsSpeed().first, yL * w1);
        }
    }

    void plantPerturbationIsLeftPlusRightMinus() {
        LineFollowerPlant plant(straightTrack(2000, 0.01), {});
        plant.setPerturbation(20.0);
        QVERIFY(plant.step(0.0, 0.0));
        QVERIFY(plant.car().left().y() > 0.0);
        QCOMPARE(plant.car().right().y(), -plant.car().left().y() * plant.car().right().c() /
                                              plant.car().left().c());
        QVERIFY(plant.data().omega > 0.0);  // left faster: turns right
    }

    // Integrating the world pose == moving the pygame track, step by step.
    void poseIntegrationMatchesPygameTransform() {
        CarDynamicsParams p;
        CarDynamics car(p);
        PygameWorld world;
        const Point2 start{32.66108407002782, 2.435753513724177};
        world.start(start.x, start.y);
        Pose pose{start.x, start.y, -M_PI / 2};
        QVERIFY(std::fabs(world.pose().theta - pose.theta) < 1e-15);

        const Point2 probes[] = {{33.0, 2.0}, {32.5, 1.2}, {31.0, 3.5}};
        double maxPos = 0.0;
        double maxTheta = 0.0;
        double maxFrame = 0.0;
        for (int k = 0; k < 400; ++k) {
            const double uL = 60 + 40 * std::sin(k * 0.05);
            const double uR = 55 - 45 * std::cos(k * 0.031);
            car.step(uL, uR, k > 200 ? 10.0 : 0.0, k > 200 ? -10.0 : 0.0);
            world.step(car.space());
            pose = integratePose(pose, car.speed(), car.omega(), p.ts);
            const Pose ref = world.pose();
            maxPos = qMax(maxPos, qMax(std::fabs(ref.x - pose.x), std::fabs(ref.y - pose.y)));
            maxTheta = qMax(maxTheta, angleDiff(ref.theta, pose.theta));
            for (const Point2& probe : probes) {
                const Point2 a = world.carFrame(probe);
                const Point2 b = worldToCar(probe, pose);
                maxFrame = qMax(maxFrame, qMax(std::fabs(a.x - b.x), std::fabs(a.y - b.y)));
            }
        }
        qInfo("400 steps: max |pose| error %.3g m, |theta| %.3g rad, car frame %.3g m", maxPos,
              maxTheta, maxFrame);
        QVERIFY(maxPos < 1e-9);
        QVERIFY(maxTheta < 1e-12);
        QVERIFY(maxFrame < 1e-9);
        // The car moved and turned (the comparison is not trivial).
        QVERIFY(std::hypot(pose.x - start.x, pose.y - start.y) > 0.5);
    }

    void carFrameRoundTrip() {
        const Pose pose{1.0, -2.0, 0.7};
        const Point2 world{1.3, -1.1};
        const CarFramePoint car = worldToCar(world, pose);
        const Point2 back = carToWorld(car, pose);
        QVERIFY(std::fabs(back.x - world.x) < 1e-12 && std::fabs(back.y - world.y) < 1e-12);
        // A point straight ahead is (0, d); one on the right side is (d, 0).
        const Pose east{0.0, 0.0, 0.0};
        const CarFramePoint ahead = worldToCar({2.0, 0.0}, east);
        QVERIFY(std::fabs(ahead.x) < 1e-15 && std::fabs(ahead.y - 2.0) < 1e-15);
        const CarFramePoint right = worldToCar({0.0, 1.0}, east);  // y down: +y is the right
        QVERIFY(std::fabs(right.x - 1.0) < 1e-15 && std::fabs(right.y) < 1e-15);
    }

    void reachArea() {
        const double d = 0.15;
        QVERIFY(inReachArea({0.0, -0.01}, d));
        QVERIFY(inReachArea({0.149, -0.149}, d));
        QVERIFY(inReachArea({-0.149, -0.149}, d));
        QVERIFY(!inReachArea({0.0, 0.0}, d));    // on the axle: not yet
        QVERIFY(!inReachArea({0.0, 0.01}, d));   // ahead
        QVERIFY(!inReachArea({0.0, -0.15}, d));  // strict
        QVERIFY(!inReachArea({0.16, -0.05}, d));
    }

    void reachRuleAndFuturePoints() {
        // Straight track along +x, the car on point 0 heading along it.
        const double ds = 0.01;
        TrackProgressParams params;
        params.futureCount = 5;
        params.futureSpacing = 3;
        TrackProgress progress(straightTrack(3000, ds).x, straightTrack(3000, ds).y, params);
        Pose pose{0.0, 0.0, 0.0};
        progress.update(pose);
        QCOMPARE(progress.nextPoint(), 0);  // point 0 is on the axle (forward 0, not < 0)
        // Move 5.5 cm forward: points 0..5 are behind the axle, within L_w.
        pose.x = 0.055;
        progress.update(pose);
        QCOMPARE(progress.nextPoint(), 6);
        QCOMPARE(progress.coverage(), 6.0 / 3000 * 100);
        const std::vector<CarFramePoint>& future = progress.futurePoints();
        QCOMPARE(int(future.size()), 5);
        for (int j = 1; j <= 5; ++j) {
            const double expectedForward = (6 + 3 * j) * ds - 0.055;
            QVERIFY(std::fabs(future[std::size_t(j - 1)].x) < 1e-12);
            QVERIFY(std::fabs(future[std::size_t(j - 1)].y - expectedForward) < 1e-12);
        }
        // The current point (6) is ahead; lateral error 0. Shift the car
        // 2 cm to its left (-y in a y-down world heading +x): the track is
        // 2 cm to the right, lateral error +0.02.
        pose.y = -0.02;
        progress.update(pose);
        QVERIFY(std::fabs(progress.lateralError() - 0.02) < 1e-12);
        // A jump forward of 1 m: only points still behind the axle within
        // L_w count, and they must be reached in order -- point 6 is now
        // 0.95 m behind, so nothing advances.
        pose = {1.055, 0.0, 0.0};
        progress.update(pose);
        QCOMPARE(progress.nextPoint(), 6);
    }

    void cellsFollowGenerateCluster() {
        // generate_cluster(): first cell with |p - i| < h, h = 200 / 300.
        const std::vector<double> x{0.0, 0.5, 0.34, -0.34, 10.9};
        const std::vector<double> y{0.0, 0.2, -0.33, 0.66, -3.4};
        TrackProgress progress(x, y, {});
        // x = 0.0: i = 0 (i = -1 fails: 0 < -1 + 0.667 is false).
        QCOMPARE(progress.cellRow(0), 50);
        QCOMPARE(progress.cellCol(0), 50);
        QCOMPARE(progress.cellRow(1), 50);  // 0.5: i = 0 (0.5 < 0.667)
        QCOMPARE(progress.cellRow(2), 50);  // 0.34 -> 0
        QCOMPARE(progress.cellRow(3), 49);  // -0.34 -> -1 (-0.34 < -1 + 0.667)
        QCOMPARE(progress.cellCol(3), 50);  // 0.66 -> 0 (0.66 < 0.667)
        QCOMPARE(progress.cellRow(4), 61);  // 10.9 -> 11 (10.9 < 10 + 0.667 fails)
        QCOMPARE(progress.cellCol(4), 46);  // -3.4 -> -4 (-3.4 < -4 + 0.667 = -3.333)
    }

    // The saved Python runs, replayed open loop.
    void replayPythonRuns_data() {
        QTest::addColumn<QString>("run");
        const QDir dir(QStringLiteral(TRACELAB_LF_PYTHON_DIR "/runs/compare"));
        const QStringList files = dir.entryList({QStringLiteral("*.csv")}, QDir::Files);
        for (const QString& f : files) {
            if (!f.endsWith(QLatin1String("_track.csv"))) {
                QTest::newRow(qPrintable(f)) << dir.filePath(f);
            }
        }
        if (files.isEmpty()) {
            QTest::newRow("missing") << QString();
        }
    }

    void replayPythonRuns() {
        QFETCH(QString, run);
        if (run.isEmpty()) {
            QSKIP("line_follower_simulator/runs/compare not found (set TRACELAB_LF_PYTHON_DIR)");
        }
        CsvTable table;
        QVERIFY(readCsv(run, &table));
        QVERIFY(!table.rows.isEmpty());

        TrackParams trackParams;  // seed 1112, lemniscate, config.py defaults
        PlantParams params;       // config.py car / motors defaults, 80 Hz
        params.progress.futureCount = 45;
        params.progress.futureSpacing = 3;
        LineFollowerPlant plant(generateTrack(trackParams), params);

        double uL = 0.0;
        double uR = 0.0;
        double maxWheel = 0.0;
        double maxPos = 0.0;
        double maxTheta = 0.0;
        double maxLateral = 0.0;
        int coverageMismatches = 0;
        int firstMismatch = -1;
        for (int k = 0; k < table.rows.size(); ++k) {
            plant.setPerturbation(table.at(k, "perturbation"));
            QVERIFY2(plant.step(uL, uR), qPrintable(QStringLiteral("finished early at %1").arg(k)));
            const StepData& d = plant.data();
            maxWheel = qMax(maxWheel, qMax(std::fabs(d.wheelSpeedLeft - table.at(k, "w_l")),
                                           std::fabs(d.wheelSpeedRight - table.at(k, "w_r"))));
            maxPos = qMax(maxPos, qMax(std::fabs(plant.pose().x - table.at(k, "x")),
                                       std::fabs(plant.pose().y - table.at(k, "y"))));
            maxTheta = qMax(maxTheta, angleDiff(plant.pose().theta, table.at(k, "heading")));
            maxLateral = qMax(maxLateral, std::fabs(d.lateralError - table.at(k, "lateral_error")));
            if (plant.progress().coverage() != table.at(k, "coverage")) {
                if (firstMismatch < 0) {
                    firstMismatch = k;
                }
                ++coverageMismatches;
            }
            uL = table.at(k, "u_l");
            uR = table.at(k, "u_r");
        }
        qInfo("%s: %d steps, max |w| error %.3g rad/s, |pose| %.3g m, |theta| %.3g rad, "
              "|lateral| %.3g m, coverage mismatches %d (first at %d)",
              qPrintable(QFileInfo(run).fileName()), int(table.rows.size()), maxWheel, maxPos,
              maxTheta, maxLateral, coverageMismatches, firstMismatch);
        QVERIFY(maxWheel < 1e-9);
        QVERIFY(maxPos < 1e-6);
        QVERIFY(maxTheta < 1e-9);
        QVERIFY(maxLateral < 1e-6);
        QCOMPARE(coverageMismatches, 0);
        // A run that got to the end of the track stopped because the next
        // step passed the last point: so must this one.
        if (table.at(int(table.rows.size()) - 1, "coverage") > 99.9) {
            plant.setPerturbation(table.at(int(table.rows.size()) - 1, "perturbation"));
            QVERIFY(!plant.step(uL, uR));
            QVERIFY(plant.finished());
        }
    }
};

QTEST_GUILESS_MAIN(TestLfPlant)
#include "test_lfplant.moc"
