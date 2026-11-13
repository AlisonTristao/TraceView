#pragma once

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QtMath>

#include "dashboard/scenes/scenerenderer.h"

namespace traceview {

// "Furuta pendulum": the rotary inverted pendulum drawn like the paper's
// figure (pendulo_furuta.drawio) -- base, motor, shaft, the arm turning by
// theta1 and the pendulum swinging by theta2 at its tip, in line art over a
// soft sky and a lit floor with the parts' shadows cast on it, with the axes.
// An orthographic camera looks at it from an azimuth and an elevation; drag
// to orbit, the wheel zooms, a double click goes back to the figure's view.
//
// It reads theta1 and theta2 only, bound by name to whichever topic of the
// device reports them.
//
// Conventions (the plant's): theta1 turns counterclockwise seen from above,
// from the x axis; theta2 = 0 is upright and > 0 tilts the pendulum toward
// increasing theta1.
class FurutaScene final : public SceneRenderer {
public:
    static constexpr char kTypeId[] = "furuta";
    // Ticks of the mass's trail kept.
    static constexpr int kTrailLength = 60;

    FurutaScene();

    QList<SceneInput> inputs() const override;
    QList<SceneParam> params() const override;
    void setParam(const QString& name, const QVariant& value) override;
    double param(const QString& name) const;

    void update(const SceneFrame& frame) override;
    void paint(QPainter& painter, const SceneView& view) override;
    bool antialiased() const override;

    // What a frame is drawn from, for the tests.
    double theta1() const {
        return m_theta1;
    }
    double theta2() const {
        return m_theta2;
    }
    bool hasAngles() const {
        return m_hasAngles;
    }
    int trailSize() const {
        return m_trail.size();
    }
    // The screen positions of the pendulum's mass and of its pivot (the arm's
    // tip) for `view`.
    QPointF massOnScreen(const SceneView& view) const;
    QPointF pivotOnScreen(const SceneView& view) const;

private:
    QHash<QString, double> m_params;

    bool m_hasAngles = false;
    double m_theta1 = 0.0;
    double m_theta2 = M_PI;
    struct TrailPoint {
        double theta1;
        double theta2;
    };
    QVector<TrailPoint> m_trail;
};

}  // namespace traceview
