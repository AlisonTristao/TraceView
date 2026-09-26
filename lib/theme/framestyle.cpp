#include "traceview/framestyle.h"

#include <QCoreApplication>
#include <QPolygonF>
#include <QRegularExpression>
#include <QtMath>

#include "traceview/thememanager.h"

namespace traceview {

namespace {

FrameStyle makeRoundedFrame() {
    // Every field at its struct default -- the original TraceView look.
    return FrameStyle{};
}

FrameStyle makeSquareFrame() {
    FrameStyle style;
    style.id = FrameStyleId::Square;
    style.corner = FrameCorner::Square;
    style.cornerSize = 0.0;
    style.borderWidth = 1.0;
    style.controlRadiusScale = 0.0;
    return style;
}

FrameStyle makeBorderlessFrame() {
    FrameStyle style;
    style.id = FrameStyleId::Borderless;
    style.borderWidth = 0.0;
    style.idleOutline = false;
    return style;
}

FrameStyle makeChamferedFrame() {
    FrameStyle style;
    style.id = FrameStyleId::Chamfered;
    style.corner = FrameCorner::Chamfer;
    style.cornerSize = 8.0;
    // QSS can't cut a corner, so controls go square -- closer to a
    // chamfer than a curve is.
    style.controlRadiusScale = 0.0;
    return style;
}

// Corners cut at 45 degrees, `cut` px along each edge; a corner passed as
// false stays square.
QPainterPath chamferedRect(const QRectF& r, qreal cut, bool topLeft, bool topRight,
                           bool bottomLeft, bool bottomRight) {
    cut = qMin(cut, qMin(r.width(), r.height()) / 2.0);
    QPolygonF polygon;
    auto corner = [&](bool cutIt, const QPointF& point, const QPointF& in, const QPointF& out) {
        if (cutIt) {
            polygon << in << out;
        } else {
            polygon << point;
        }
    };
    corner(topLeft, r.topLeft(), QPointF(r.left(), r.top() + cut),
           QPointF(r.left() + cut, r.top()));
    corner(topRight, r.topRight(), QPointF(r.right() - cut, r.top()),
           QPointF(r.right(), r.top() + cut));
    corner(bottomRight, r.bottomRight(), QPointF(r.right(), r.bottom() - cut),
           QPointF(r.right() - cut, r.bottom()));
    corner(bottomLeft, r.bottomLeft(), QPointF(r.left() + cut, r.bottom()),
           QPointF(r.left(), r.bottom() - cut));
    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();
    return path;
}

}  // namespace

QString frameStyleIdString(FrameStyleId id) {
    switch (id) {
        case FrameStyleId::Square:
            return QStringLiteral("square");
        case FrameStyleId::Borderless:
            return QStringLiteral("borderless");
        case FrameStyleId::Chamfered:
            return QStringLiteral("chamfered");
        case FrameStyleId::Rounded:
            break;
    }
    return QStringLiteral("rounded");
}

FrameStyleId frameStyleFromId(const QString& id) {
    for (FrameStyleId style : allFrameStyles()) {
        if (frameStyleIdString(style) == id) {
            return style;
        }
    }
    return FrameStyleId::Rounded;
}

QString frameStyleDisplayName(FrameStyleId id) {
    switch (id) {
        case FrameStyleId::Square:
            return QCoreApplication::translate("FrameStyle", "Square");
        case FrameStyleId::Borderless:
            return QCoreApplication::translate("FrameStyle", "Borderless");
        case FrameStyleId::Chamfered:
            return QCoreApplication::translate("FrameStyle", "Chamfered");
        case FrameStyleId::Rounded:
            break;
    }
    return QCoreApplication::translate("FrameStyle", "Rounded");
}

QVector<FrameStyleId> allFrameStyles() {
    return {FrameStyleId::Rounded, FrameStyleId::Square, FrameStyleId::Borderless,
            FrameStyleId::Chamfered};
}

const FrameStyle& frameStyle(FrameStyleId id) {
    static const FrameStyle rounded = makeRoundedFrame();
    static const FrameStyle square = makeSquareFrame();
    static const FrameStyle borderless = makeBorderlessFrame();
    static const FrameStyle chamfered = makeChamferedFrame();
    switch (id) {
        case FrameStyleId::Square:
            return square;
        case FrameStyleId::Borderless:
            return borderless;
        case FrameStyleId::Chamfered:
            return chamfered;
        case FrameStyleId::Rounded:
            break;
    }
    return rounded;
}

QPainterPath partiallyRoundedRect(const QRectF& r, qreal radius, bool roundTopLeft,
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

QPainterPath frameShapePath(const QRectF& r, const FrameStyle& style, bool topLeft,
                            bool topRight, bool bottomLeft, bool bottomRight) {
    switch (style.corner) {
        case FrameCorner::Square: {
            QPainterPath path;
            path.addRect(r);
            return path;
        }
        case FrameCorner::Chamfer:
            return chamferedRect(r, style.cornerSize, topLeft, topRight, bottomLeft,
                                 bottomRight);
        case FrameCorner::Round:
            break;
    }
    return partiallyRoundedRect(r, style.cornerSize, topLeft, topRight, bottomLeft, bottomRight);
}

QPainterPath currentFramePath(const QRectF& r, bool topLeft, bool topRight, bool bottomLeft,
                              bool bottomRight) {
    return frameShapePath(r, ThemeManager::instance().currentFrameStyle(), topLeft, topRight,
                          bottomLeft, bottomRight);
}

QString scaleStyleSheetRadii(const QString& css, double scale) {
    if (qFuzzyCompare(scale, 1.0)) {
        return css;
    }
    static const QRegularExpression radius(
        QStringLiteral(R"((border(?:-(?:top|bottom)-(?:left|right))?-radius:\s*)(\d+(?:\.\d+)?)px)"));
    QString result;
    result.reserve(css.size());
    qsizetype last = 0;
    auto it = radius.globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        result += css.mid(last, match.capturedStart() - last);
        const int scaled = qRound(match.captured(2).toDouble() * scale);
        result += match.captured(1) + QString::number(scaled) + QStringLiteral("px");
        last = match.capturedEnd();
    }
    result += css.mid(last);
    return result;
}

}  // namespace traceview
