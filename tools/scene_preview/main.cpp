// Standalone visual harness for the TraceLab line-follower scene -- NOT a Qt
// Test. A synthetic plant (a lemniscate track, a car driving along it with a
// small weave, a reference preview and a fake predicted path) feeds a real
// SceneWidget through the same TelemetryConsumer calls MainWindow makes, so
// the look can be checked without the simulation engine. See
// tests/test_diffdrivescene.cpp for the camera and alignment checks.
//
//   scene_preview                       interactive window (P/O/R, wheel, drag)
//   scene_preview --screenshot <png>    renders a few seconds, saves, exits

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QVector>
#include <QtMath>
#include <cmath>
#include <cstdio>

#include "dashboard/widgetconfigeditor.h"
#include "lab/scenes/diffdrivecamera.h"
#include "lab/scenes/diffdrivescene.h"
#include "lab/scenes/sceneregistry.h"
#include "lab/scenes/scenewidget.h"

using namespace tracelab;

namespace {

constexpr quint32 kSourceId = 0x00000001;
constexpr quint16 kTruthTopic = 0x0010;
constexpr quint16 kPreviewTopic = 0x0011;
constexpr quint16 kDiagTopic = 0x0012;
constexpr quint16 kTrackTopic = 0x0013;
constexpr quint16 kInfoTopic = 0x0014;

constexpr double kSamplePeriod = 0.0125;  // 80 Hz
constexpr double kSpeed = 0.6;            // m/s
constexpr double kPointSpacing = 0.005;   // m between track points
constexpr int kPreviewCount = 10;
constexpr int kPreviewSpacing = 12;  // track points
constexpr int kPredictionCount = 20;

QJsonObject binding(quint16 topicId, const QString& topicName, int fieldId,
                    const QString& fieldName) {
    SceneBinding b;
    b.deviceId = QStringLiteral("preview");
    b.sourceId = kSourceId;
    b.topicId = topicId;
    b.topicName = topicName;
    b.fieldId = fieldId;
    b.fieldName = fieldName;
    return sceneBindingToJson(b);
}

QJsonObject sceneConfig() {
    const QString truth = QStringLiteral("sim.truth");
    const QString preview = QStringLiteral("sim.preview");
    const QString diag = QStringLiteral("ctl.diag");
    QJsonObject bind;
    bind["tick"] = binding(kTruthTopic, truth, 0, "tick");
    bind["x"] = binding(kTruthTopic, truth, 1, "x");
    bind["y"] = binding(kTruthTopic, truth, 2, "y");
    bind["theta"] = binding(kTruthTopic, truth, 3, "theta");
    bind["next_point"] = binding(kTruthTopic, truth, 4, "next_point");
    bind["coverage"] = binding(kTruthTopic, truth, 5, "coverage");
    bind["t"] = binding(kTruthTopic, truth, 6, "t");
    bind["preview_tick"] = binding(kPreviewTopic, preview, 0, "tick");
    bind["preview_x"] = binding(kPreviewTopic, preview, 1, "px");
    bind["preview_y"] = binding(kPreviewTopic, preview, 2, "py");
    bind["pred_tick"] = binding(kDiagTopic, diag, 0, "ref_tick");
    bind["pred_x"] = binding(kDiagTopic, diag, 1, "pred_x");
    bind["pred_y"] = binding(kDiagTopic, diag, 2, "pred_y");
    bind["qps"] = binding(kDiagTopic, diag, 3, "iterations");
    bind["solve_ms"] = binding(kDiagTopic, diag, 4, "solve_ms");
    bind["track"] = binding(kTrackTopic, "sim.track", -1, QString());
    bind["controller"] = binding(kInfoTopic, "ctl.info", -1, QString());
    QJsonObject config;
    config["scene"] = QString::fromLatin1(DiffDriveScene::kTypeId);
    config["bind"] = bind;
    return config;
}

// A lemniscate of Bernoulli, resampled to a point every kPointSpacing.
QVector<QPointF> lemniscateTrack() {
    constexpr double kHalfWidth = 3.0;
    constexpr int kDenseSamples = 200000;
    QVector<QPointF> points;
    QPointF last;
    for (int i = 0; i <= kDenseSamples; ++i) {
        const double t = 2.0 * M_PI * i / kDenseSamples;
        const double d = 1.0 + std::sin(t) * std::sin(t);
        const QPointF p(kHalfWidth * std::cos(t) / d, kHalfWidth * std::sin(t) * std::cos(t) / d);
        if (points.isEmpty() || std::hypot(p.x() - last.x(), p.y() - last.y()) >= kPointSpacing) {
            points.append(p);
            last = p;
        }
    }
    return points;
}

// The fake plant: the car rides the track at kSpeed with a weave of a few
// centimetres, so the reference points are visibly off the car's axis.
class SyntheticPlant {
public:
    explicit SyntheticPlant(SceneWidget* scene) : m_scene(scene), m_track(lemniscateTrack()) {
        QJsonArray points;
        for (const QPointF& p : m_track) {
            points.append(QJsonArray{p.x(), p.y()});
        }
        const QJsonObject document{{"points", points}, {"closed", true}};
        m_scene->onTextSample(kSourceId, kTrackTopic, 0,
                              QString::fromUtf8(QJsonDocument(document).toJson()));
        m_scene->onTextSample(kSourceId, kInfoTopic, 0, QStringLiteral("qLPV MPC"));
    }

    void tick() {
        const double t = m_tick * kSamplePeriod;
        const quint64 us = quint64(t * 1e6);
        const int n = m_track.size();
        const double s = kSpeed * t / kPointSpacing;
        const int index = int(s) % n;
        const QPointF ahead = m_track[(index + 4) % n] - m_track[index];
        const double heading = std::atan2(ahead.y(), ahead.x());
        const double weave = 0.03 * std::sin(2.0 * M_PI * 0.4 * t);
        DiffDrivePose pose{m_track[index].x() - weave * std::sin(heading),
                           m_track[index].y() + weave * std::cos(heading),
                           heading + 0.15 * std::cos(2.0 * M_PI * 0.4 * t)};
        const int nextPoint = qMin(int(s) + 1, n);

        // sim.preview first, as the plant does: the track points ahead, in
        // the car frame of this tick.
        QVector<float> px;
        QVector<float> py;
        for (int k = 1; k <= kPreviewCount; ++k) {
            const QPointF world = m_track[(index + k * kPreviewSpacing) % n];
            const QPointF car = toCarFrame(world, pose);
            px.append(float(car.x()));
            py.append(float(car.y()));
        }
        field(kPreviewTopic, 0, us, double(m_tick));
        array(kPreviewTopic, 1, us, px);
        array(kPreviewTopic, 2, us, py);

        field(kTruthTopic, 0, us, double(m_tick));
        field(kTruthTopic, 1, us, pose.x);
        field(kTruthTopic, 2, us, pose.y);
        field(kTruthTopic, 3, us, pose.theta);
        field(kTruthTopic, 4, us, double(nextPoint));
        field(kTruthTopic, 5, us, 100.0 * nextPoint / n);
        field(kTruthTopic, 6, us, t);

        // ctl.diag answers the previous tick: its path is in that tick's car
        // frame, which is what the scene's pose history is for.
        if (m_tick > 0) {
            QVector<float> predX;
            QVector<float> predY;
            for (int k = 1; k <= kPredictionCount; ++k) {
                const QPointF world = m_track[(m_lastIndex + k * 6) % n];
                const QPointF car = toCarFrame(world, m_lastPose);
                const double drift = 0.004 * k;
                predX.append(float(car.x() * (1.0 - 0.02 * k) + drift));
                predY.append(float(car.y()));
            }
            const quint64 diagUs = us + 2000;
            field(kDiagTopic, 0, diagUs, double(m_tick - 1));
            array(kDiagTopic, 1, diagUs, predX);
            array(kDiagTopic, 2, diagUs, predY);
            field(kDiagTopic, 3, diagUs, 6.0 + (m_tick % 3));
            field(kDiagTopic, 4, diagUs, 1.8 + 0.3 * std::sin(t));
        }
        m_lastPose = pose;
        m_lastIndex = index;
        ++m_tick;
    }

private:
    static QPointF toCarFrame(const QPointF& world, const DiffDrivePose& pose) {
        const double dx = world.x() - pose.x;
        const double dy = world.y() - pose.y;
        const double c = std::cos(pose.theta);
        const double sn = std::sin(pose.theta);
        return {-dx * sn + dy * c, dx * c + dy * sn};
    }

    void field(quint16 topicId, quint16 fieldId, quint64 us, double value) {
        m_scene->onFieldSample({kSourceId, topicId, fieldId, 0}, us, value);
    }
    void array(quint16 topicId, quint16 fieldId, quint64 us, const QVector<float>& values) {
        m_scene->onArraySample({kSourceId, topicId, fieldId, 0}, us, values);
    }

    SceneWidget* m_scene;
    QVector<QPointF> m_track;
    quint64 m_tick = 0;
    DiffDrivePose m_lastPose;
    int m_lastIndex = 0;
};

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("TraceViewTools"));
    QApplication::setApplicationName(QStringLiteral("scene_preview"));
    registerBuiltinScenes(SceneRegistry::instance());

    QString screenshotPath;
    const QStringList args = QApplication::arguments();
    const int flag = args.indexOf(QStringLiteral("--screenshot"));
    if (flag >= 0 && flag + 1 < args.size()) {
        screenshotPath = args.at(flag + 1);
    }

    SceneWidget scene;
    scene.setWindowTitle(QStringLiteral("TraceLab scene preview"));
    scene.setConfig(sceneConfig());
    scene.resize(1280, 720);
    SyntheticPlant plant(&scene);

    if (!screenshotPath.isEmpty()) {
        // Five simulated seconds, painting now and then like the dashboard.
        for (int i = 0; i < 400; ++i) {
            plant.tick();
            if (i % 8 == 0) {
                scene.grab();
            }
        }
        if (!scene.grab().save(screenshotPath)) {
            std::fprintf(stderr, "could not save %s\n", qPrintable(screenshotPath));
            return 1;
        }
        std::printf("saved %s\n", qPrintable(screenshotPath));
        return 0;
    }

    QObject::connect(&scene, &SceneWidget::actionRequested,
                     [](const QString& action, double argument) {
                         std::printf("%s %.1f\n", qPrintable(action), argument);
                         std::fflush(stdout);
                     });
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, [&plant] { plant.tick(); });
    timer.start(int(kSamplePeriod * 1000));
    scene.show();
    return QApplication::exec();
}
