#include <QImage>
#include <QPainter>
#include <QtTest>
#include <cmath>

#include "dashboard/scenes/diffdrivecamera.h"
#include "dashboard/scenes/diffdrivescene.h"

using namespace traceview;

namespace {

constexpr double kScale = 300.0;
constexpr double kTolerance = 1e-9;

bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-6) {
    return std::abs(a.x() - b.x()) < tolerance && std::abs(a.y() - b.y()) < tolerance;
}

// Car frame of `pose`, computed here on its own (x lateral right, y forward)
// rather than with the scene's helpers.
QPointF toCarFrame(const QPointF& world, const DiffDrivePose& pose) {
    const QPointF heading(std::cos(pose.theta), std::sin(pose.theta));
    const QPointF right(-std::sin(pose.theta), std::cos(pose.theta));
    const QPointF d = world - QPointF(pose.x, pose.y);
    return {QPointF::dotProduct(d, right), QPointF::dotProduct(d, heading)};
}

// The visual angle of `p` around `center` on a y-down screen,
// counter-clockwise positive.
double screenAngle(const QPointF& p, const QPointF& center) {
    return std::atan2(-(p.y() - center.y()), p.x() - center.x());
}

SceneRecord poseRecord(qint64 tick, const DiffDrivePose& pose) {
    SceneRecord record;
    record.timestampUs = quint64(tick) * 12500;
    record.values = {{"tick", double(tick)}, {"x", pose.x}, {"y", pose.y}, {"theta", pose.theta}};
    return record;
}

}  // namespace

class TestDiffDriveScene : public QObject {
    Q_OBJECT

private slots:
    void carAnchorIsFixedFractionOfTheView();
    void worldPointUnderTheCarMapsToTheAnchor();
    void forwardMotionMovesTheWorldDown();
    void rightTurnRotatesTheWorldCounterClockwise();
    void closedFormMatchesThePygameStepRule();
    void carFrameRoundTrip();
    void previewWithPoseOfItsTickLandsOnTheWorldPoint();
    void unknownTickFallsBackToTheCurrentPose();
    void keysAskForDisturbance();
    void paintsWithoutData();
};

void TestDiffDriveScene::carAnchorIsFixedFractionOfTheView() {
    QVERIFY(near(diffDriveCarAnchor(QSizeF(1000, 800)), QPointF(500, 720)));
}

void TestDiffDriveScene::worldPointUnderTheCarMapsToTheAnchor() {
    const DiffDrivePose pose{2.0, -3.0, 0.7};
    const QPointF anchor(500, 400);
    QVERIFY(near(diffDriveWorldToScreen({2.0, -3.0}, pose, anchor, kScale), anchor, kTolerance));

    // The same through the scene, with a current pose fed in.
    DiffDriveScene scene;
    SceneFrame frame;
    frame.records = {poseRecord(1, pose)};
    frame.values = frame.records.first().values;
    scene.update(frame);
    SceneView view;
    view.size = QSizeF(1000, 800);
    QVERIFY(near(scene.worldToScreen({2.0, -3.0}, view), diffDriveCarAnchor(view.size)));
}

void TestDiffDriveScene::forwardMotionMovesTheWorldDown() {
    const QPointF anchor(500, 400);
    const DiffDrivePose before{1.0, 1.0, 0.4};
    const double step = 0.1;
    const DiffDrivePose after{before.x + step * std::cos(before.theta),
                              before.y + step * std::sin(before.theta), before.theta};
    const QPointF world(2.0, 1.5);
    const QPointF a = diffDriveWorldToScreen(world, before, anchor, kScale);
    const QPointF b = diffDriveWorldToScreen(world, after, anchor, kScale);
    QVERIFY(std::abs(b.x() - a.x()) < 1e-6);
    QVERIFY(std::abs((b.y() - a.y()) - step * kScale) < 1e-6);

    // Straight ahead is straight up.
    const QPointF ahead(before.x + std::cos(before.theta), before.y + std::sin(before.theta));
    QVERIFY(
        near(diffDriveWorldToScreen(ahead, before, anchor, kScale), anchor + QPointF(0, -kScale)));
}

void TestDiffDriveScene::rightTurnRotatesTheWorldCounterClockwise() {
    // omega = r/L * (w_l - w_r) > 0 is a right turn and grows theta.
    const QPointF anchor(500, 400);
    const DiffDrivePose before{0.0, 0.0, 0.3};
    const double delta = 0.05;
    const DiffDrivePose after{before.x, before.y, before.theta + delta};
    for (const QPointF& world : {QPointF(1.0, 0.2), QPointF(-0.5, 2.0), QPointF(0.3, -1.1)}) {
        const QPointF a = diffDriveWorldToScreen(world, before, anchor, kScale);
        const QPointF b = diffDriveWorldToScreen(world, after, anchor, kScale);
        double turned = screenAngle(b, anchor) - screenAngle(a, anchor);
        turned = std::remainder(turned, 2.0 * M_PI);
        QVERIFY2(std::abs(turned - delta) < 1e-9, qPrintable(QString::number(turned)));
    }
}

void TestDiffDriveScene::closedFormMatchesThePygameStepRule() {
    // pygame: p' = R(-w Ts)(p - C) + C + S * (v Ts sin(w Ts), v Ts cos(w Ts)),
    // y down, R(phi) = [[cos, -sin], [sin, cos]] -- after an Euler step of
    // the plant (forward v Ts along the old heading, then turn by w Ts).
    const QPointF c(680, 720);
    const double v = 0.8;
    const double w = 1.7;
    const double ts = 0.0125;
    DiffDrivePose pose{0.4, -0.2, -0.9};
    const QPointF world(1.2, 0.7);
    QPointF pygame = diffDriveWorldToScreen(world, pose, c, kScale);
    for (int k = 0; k < 40; ++k) {
        const double phi = -w * ts;
        const QPointF d = pygame - c;
        pygame = QPointF(std::cos(phi) * d.x() - std::sin(phi) * d.y(),
                         std::sin(phi) * d.x() + std::cos(phi) * d.y()) +
                 c + kScale * QPointF(v * ts * std::sin(w * ts), v * ts * std::cos(w * ts));
        pose = {pose.x + v * ts * std::cos(pose.theta), pose.y + v * ts * std::sin(pose.theta),
                pose.theta + w * ts};
        QVERIFY(near(diffDriveWorldToScreen(world, pose, c, kScale), pygame, 1e-6));
    }
}

void TestDiffDriveScene::carFrameRoundTrip() {
    const DiffDrivePose pose{3.0, -1.0, 2.2};
    const QPointF world(3.4, -0.6);
    QVERIFY(near(diffDriveCarToWorld(toCarFrame(world, pose), pose), world, kTolerance));
    // A car-frame point at the current pose is drawn at C + S * (x, -y).
    const QPointF anchor(100, 100);
    const QPointF car(0.05, 0.3);
    QVERIFY(near(diffDriveWorldToScreen(diffDriveCarToWorld(car, pose), pose, anchor, kScale),
                 anchor + QPointF(0.05 * kScale, -0.3 * kScale)));
}

void TestDiffDriveScene::previewWithPoseOfItsTickLandsOnTheWorldPoint() {
    // sim.preview of tick 5 is in the car frame of tick 5; by the time it is
    // drawn the car is at tick 6. Through the pose history it must land on
    // the world point it describes, drawn with the current pose.
    const DiffDrivePose pose5{1.0, 2.0, 0.5};
    const DiffDrivePose pose6{1.01, 2.004, 0.53};
    const QPointF world(1.6, 2.3);
    const QPointF previewPoint = toCarFrame(world, pose5);

    DiffDriveScene scene;
    SceneFrame frame;
    SceneRecord preview;
    preview.timestampUs = 5 * 12500;
    preview.values = {{"preview_tick", 5.0}};
    preview.arrays = {{"preview_x", {previewPoint.x()}}, {"preview_y", {previewPoint.y()}}};
    frame.records = {poseRecord(5, pose5), preview, poseRecord(6, pose6)};
    frame.values = frame.records.last().values;
    scene.update(frame);

    SceneView view;
    view.size = QSizeF(1280, 720);
    QVERIFY(near(scene.carPointToScreen(previewPoint, 5, view), scene.worldToScreen(world, view)));
    // Drawn with the current pose instead, it would sit elsewhere (the
    // pygame "one frame late" misalignment).
    const QPointF naive = diffDriveWorldToScreen(diffDriveCarToWorld(previewPoint, pose6), pose6,
                                                 scene.anchor(view), scene.scale(view));
    QVERIFY(!near(naive, scene.worldToScreen(world, view), 1.0));

    // The predicted path (ctl.diag, ref_tick) goes through the same history.
    QVERIFY(near(scene.carPointToScreen(toCarFrame(world, pose6), 6, view),
                 scene.worldToScreen(world, view)));
}

void TestDiffDriveScene::unknownTickFallsBackToTheCurrentPose() {
    const DiffDrivePose pose{0.0, 0.0, 1.0};
    DiffDriveScene scene;
    SceneFrame frame;
    frame.records = {poseRecord(10, pose)};
    frame.values = frame.records.first().values;
    scene.update(frame);
    SceneView view;
    view.size = QSizeF(800, 600);
    const QPointF car(0.1, 0.2);
    QVERIFY(near(scene.carPointToScreen(car, 99, view),
                 scene.anchor(view) + QPointF(0.1, -0.2) * scene.scale(view)));
}

void TestDiffDriveScene::keysAskForDisturbance() {
    DiffDriveScene scene;
    QCOMPARE(scene.keyAction(Qt::Key_P)->action, QString("sim.disturb"));
    QCOMPARE(scene.keyAction(Qt::Key_P)->argument, 10.0);
    QCOMPARE(scene.keyAction(Qt::Key_O)->argument, 5.0);
    QCOMPARE(scene.keyAction(Qt::Key_R)->argument, 0.0);
    scene.setParam("perturbation_step", 2.5);
    QCOMPARE(scene.keyAction(Qt::Key_O)->argument, -2.5);
    QVERIFY(!scene.keyAction(Qt::Key_Escape));
}

void TestDiffDriveScene::paintsWithoutData() {
    // No pose, no track: the background, car and HUD still draw.
    DiffDriveScene scene;
    scene.update({});
    QImage image(640, 360, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    SceneView view;
    view.size = image.size();
    {
        QPainter painter(&image);
        scene.paint(painter, view);
    }
    QCOMPARE(image.pixelColor(5, 100), QColor(255, 255, 255));
    // The car's centre is solid blue.
    const QPoint c = diffDriveCarAnchor(view.size).toPoint();
    QCOMPARE(image.pixelColor(c), QColor(0, 0, 200));
}

QTEST_MAIN(TestDiffDriveScene)
#include "test_diffdrivescene.moc"
