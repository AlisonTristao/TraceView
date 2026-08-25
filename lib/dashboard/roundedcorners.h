#pragma once

#include <QPainterPath>
#include <QRectF>

namespace traceview {

// Corner radius for large custom-painted containers -- DashboardCell's
// outer border, and (matching it) whatever a DashboardWidget paints as its
// own opaque fill/border underneath that outline. See "Corner radius" in
// docs/VISUAL_IDENTITY.md. QSS-driven controls (buttons, inputs, combo
// boxes) use a separate, smaller 4px radius baked directly into
// stylesheet.cpp instead -- this constant is only for the two things named
// above, which both need to land on the exact same curve or a straight
// corner shows through/past the rounded one.
constexpr qreal kContainerCornerRadius = 12.0;

// A rounded rect where any corner can be forced square instead. Built by
// unioning a fully-rounded path with a square patch over each corner that
// should stay sharp (path.simplified() merges the overlapping subpaths into
// one clean outline/region) rather than hand-rolling QPainterPath::arcTo
// signs per corner.
inline QPainterPath partiallyRoundedRect(const QRectF& r, qreal radius, bool roundTopLeft,
                                         bool roundTopRight, bool roundBottomLeft,
                                         bool roundBottomRight) {
    QPainterPath path;
    // The square patches below overlap the base rounded rectangle and must
    // form a union. QPainterPath defaults to OddEvenFill, which turns those
    // overlaps into radius-sized holes (visible as little square outlines
    // below DashboardCell's header). WindingFill keeps the overlap filled.
    path.setFillRule(Qt::WindingFill);
    path.addRoundedRect(r, radius, radius);
    if (roundTopLeft && roundTopRight && roundBottomLeft && roundBottomRight) {
        // No square corners to weld on -- return the plain rounded rect as-is.
        // simplified() flattens curves to a polygon and refits them, which
        // visibly facets the arc (especially at a 12px radius); only pay
        // that cost when a square patch actually needs unioning in below.
        return path;
    }
    if (!roundTopLeft) {
        path.addRect(QRectF(r.left(), r.top(), radius, radius));
    }
    if (!roundTopRight) {
        path.addRect(QRectF(r.right() - radius, r.top(), radius, radius));
    }
    if (!roundBottomLeft) {
        path.addRect(QRectF(r.left(), r.bottom() - radius, radius, radius));
    }
    if (!roundBottomRight) {
        path.addRect(QRectF(r.right() - radius, r.bottom() - radius, radius, radius));
    }
    return path.simplified();
}

}  // namespace traceview
