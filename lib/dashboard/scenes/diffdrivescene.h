#pragma once

#include <QColor>
#include <QHash>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QVector>
#include <QtMath>

#include "dashboard/scenes/diffdrivecamera.h"
#include "dashboard/scenes/scenerenderer.h"

namespace traceview {

// "Line follower": a differential-drive robot on a track, drawn like the
// pygame simulator's run view (car fixed near the bottom right, the world
// rotating and moving around it, minimap, compass, green HUD). It only draws:
// the track comes from the sim.track document, progress from sim.truth, the
// reference points from sim.preview and the predicted path from ctl.diag.
//
// Everything in a frame is drawn from ONE pose, the newest. Points given in
// the car frame of some tick (sim.preview, ctl.diag) are first taken to the
// world with the pose of THAT tick, kept in a short history keyed by tick,
// so they stay on the track instead of lagging behind the car.
class DiffDriveScene final : public SceneRenderer {
public:
    static constexpr char kTypeId[] = "diffdrive";
    // How many ticks of pose history are kept for the car-frame points.
    static constexpr int kPoseHistory = 512;

    DiffDriveScene();

    QList<SceneInput> inputs() const override;
    QList<SceneParam> params() const override;
    void setParam(const QString& name, const QVariant& value) override;
    double param(const QString& name) const;

    void update(const SceneFrame& frame) override;
    void paint(QPainter& painter, const SceneView& view) override;
    bool antialiased() const override;
    void document(const QString& input, const QByteArray& body) override;
    std::optional<SceneAction> keyAction(int key) override;

    // What the frame is drawn from, for the tests and the preview tool.
    DiffDrivePose pose() const;
    QPointF anchor(const SceneView& view) const;
    double scale(const SceneView& view) const;
    QPointF worldToScreen(const QPointF& world, const SceneView& view) const;
    // A car-frame point of tick `tick` on screen: through the world with the
    // pose of that tick (the current pose if it is not in the history).
    QPointF carPointToScreen(const QPointF& carPoint, qint64 tick, const SceneView& view) const;
    int trackPointCount() const;

private:
    struct CarFramePoints {
        qint64 tick = -1;
        QVector<QPointF> points;
    };

    void setTrack(const QVector<QPointF>& points);
    DiffDrivePose poseOfTick(qint64 tick) const;
    QVector<int> trackPointsNear(const QPointF& center, double radius) const;
    double coveragePercent() const;
    double elapsedSeconds() const;

    void paintTrack(QPainter& painter, const SceneView& view) const;
    void paintFloorDots(QPainter& painter, const SceneView& view) const;
    void paintCar(QPainter& painter, const SceneView& view) const;
    void paintMinimap(QPainter& painter, const SceneView& view) const;
    void paintCompass(QPainter& painter, const SceneView& view) const;
    void paintCarFramePoints(QPainter& painter, const SceneView& view, const CarFramePoints& points,
                             const QColor& color, double radius) const;

    QHash<QString, double> m_params;

    QVector<QPointF> m_track;
    QRectF m_trackBounds;
    // Track point indices by 1 m cell, and the 1 m lattice nodes that have a
    // track point closer than kFloorDotClearance (no floor dot there).
    QHash<qint64, QVector<int>> m_trackCells;
    QSet<qint64> m_blockedLatticeNodes;

    DiffDrivePose m_pose;
    bool m_hasPose = false;
    QMap<qint64, DiffDrivePose> m_poseByTick;
    CarFramePoints m_preview;
    CarFramePoints m_prediction;

    int m_nextPoint = -1;
    double m_coverage = qQNaN();
    double m_time = qQNaN();
    quint64 m_firstPoseUs = 0;
    quint64 m_lastPoseUs = 0;
    double m_qps = qQNaN();
    double m_solveMs = qQNaN();
    QString m_controller;
    double m_perturbation = 0.0;
};

}  // namespace traceview
