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

// Extra radius DashboardWidget::contentFillPath() could add on top of
// kContainerCornerRadius to chase an exact sub-pixel match with
// DashboardCell's outline stroke (which is itself built from a rect inset
// by half its own animated 1-2px width, so its arc technically starts
// turning 0.5-1px earlier than a plain radius-kContainerCornerRadius arc
// on the true, uninset bounds would). Left at 0 -- tuning this chased a
// cosmetic, sub-pixel mismatch (a faint fragment of the border's own color
// right at the corner) through several values and each one either
// undercorrected (the fragment) or overcorrected into something worse (a
// visible gap around the widget, or the corner overshooting past the
// outline) -- not worth it. 0 is the exact geometry DashboardCell's
// updateContentMask() has always used for the *mask* (see
// docs/VISUAL_IDENTITY.md "Corner radius" for the back-and-forth this
// constant went through before landing back here).
constexpr qreal kBorderCurveInset = 0.0;

// A rounded rect where any corner can be forced square instead. Built by
// unioning a fully-rounded path with a square patch over each corner that
// should stay sharp (path.simplified() merges the overlapping subpaths into
// one clean outline/region) rather than hand-rolling QPainterPath::arcTo
// signs per corner.
inline QPainterPath partiallyRoundedRect(const QRectF& r, qreal radius, bool roundTopLeft, bool roundTopRight,
                                          bool roundBottomLeft, bool roundBottomRight) {
    QPainterPath path;
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

} // namespace traceview
