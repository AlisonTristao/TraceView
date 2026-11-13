#pragma once

#include <QPointF>
#include <QSizeF>

namespace traceview {

// The line follower's world and camera (the pygame run view). The world is
// the plant's: metres, with the plant's heading convention -- theta grows
// when the car turns right (omega = r/L * (w_l - w_r) > 0), so the world is
// drawn y-down, as the minimap shows it. The car's heading is the world
// direction (cos theta, sin theta) and its right-hand side is
// (-sin theta, cos theta).
struct DiffDrivePose {
    double x = 0.0;
    double y = 0.0;
    double theta = 0.0;
};

// The car's fixed screen point C = (cx, 1.8 cy), cx, cy = half the
// widget size. The car always points up from there; the world moves.
QPointF diffDriveCarAnchor(const QSizeF& size);

// A world point (m) on screen, with the car at `pose` drawn at `anchor` and
// `scale` px/m: the car-frame position of the point (lateral right, forward)
// lands at anchor + scale * (lateral, -forward). Driving forward moves the
// world down the screen; turning right rotates it counter-clockwise.
QPointF diffDriveWorldToScreen(const QPointF& world, const DiffDrivePose& pose,
                               const QPointF& anchor, double scale);

// A car-frame point of `pose` (x lateral right, y forward, m) -- what
// sim.preview and ctl.diag carry -- in world metres.
QPointF diffDriveCarToWorld(const QPointF& carPoint, const DiffDrivePose& pose);

}  // namespace traceview
