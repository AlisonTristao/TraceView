#include "diffdrivecamera.h"

#include <cmath>

namespace traceview {

QPointF diffDriveCarAnchor(const QSizeF& size) {
    const double cx = size.width() / 2.0;
    const double cy = size.height() / 2.0;
    return {cx, 1.8 * cy};
}

QPointF diffDriveWorldToScreen(const QPointF& world, const DiffDrivePose& pose,
                               const QPointF& anchor, double scale) {
    const double dx = world.x() - pose.x;
    const double dy = world.y() - pose.y;
    const double c = std::cos(pose.theta);
    const double s = std::sin(pose.theta);
    const double lateral = -dx * s + dy * c;
    const double forward = dx * c + dy * s;
    return {anchor.x() + scale * lateral, anchor.y() - scale * forward};
}

QPointF diffDriveCarToWorld(const QPointF& carPoint, const DiffDrivePose& pose) {
    const double c = std::cos(pose.theta);
    const double s = std::sin(pose.theta);
    // forward * (c, s) + lateral * (-s, c)
    return {pose.x + carPoint.y() * c - carPoint.x() * s,
            pose.y + carPoint.y() * s + carPoint.x() * c};
}

}  // namespace traceview
