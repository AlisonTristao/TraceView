#include "furutascene.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace traceview {

namespace {

// Param keys (also the .tvproj "params" keys).
const QString kArmLength = QStringLiteral("arm_length_m");
const QString kPendulumLength = QStringLiteral("pendulum_length_m");
const QString kAzimuth = QStringLiteral("azimuth_deg");
const QString kElevation = QStringLiteral("elevation_deg");
const QString kLabels = QStringLiteral("labels");
const QString kTrail = QStringLiteral("trail");
const QString kAntialiasing = QStringLiteral("antialiasing");

const QColor kPaper(255, 255, 255);
const QColor kInk(0, 0, 0);
const QColor kGuide(85, 85, 85);
const QColor kTrailColor(120, 120, 120);
// The backdrop: a soft sky, a lit patch of floor under the machine and the
// shadows of its parts cast straight down onto it.
const QColor kSkyTop(252, 253, 255);
const QColor kSkyBottom(221, 227, 235);
const QColor kStage(255, 255, 255);
const QColor kShadow(40, 50, 70);

// Degrees per pixel of drag.
constexpr double kOrbitPerPixel = 0.4;

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

V3 operator+(const V3& a, const V3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
V3 operator-(const V3& a, const V3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
V3 operator*(double k, const V3& a) {
    return {k * a.x, k * a.y, k * a.z};
}
double dot(const V3& a, const V3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 normalized(const V3& a) {
    const double n = std::sqrt(dot(a, a));
    return n > 0.0 ? (1.0 / n) * a : V3{0, 0, 1};
}

double wrapAngle(double a) {
    return std::remainder(a, 2.0 * M_PI);
}

// The orthographic camera: world metres to widget pixels and back.
struct Camera {
    V3 toViewer;  // unit, from the scene toward the camera
    V3 right;
    V3 up;
    double scale = 1.0;  // px/m
    QPointF origin;      // where the world origin lands

    QPointF project(const V3& p) const {
        return {origin.x() + scale * dot(p, right), origin.y() - scale * dot(p, up)};
    }
    // Depth toward the camera: larger is nearer.
    double depth(const V3& p) const {
        return dot(p, toViewer);
    }
    // The world point at depth 0 under a screen point; the ray through it
    // runs along toViewer.
    V3 unproject(const QPointF& screen) const {
        return ((screen.x() - origin.x()) / scale) * right +
               (-(screen.y() - origin.y()) / scale) * up;
    }
};

// The mechanism's dimensions from the arm length L and the pendulum length
// l, measured on the paper's figure (pendulo_furuta.drawio): a wide base
// whose top edge is chamfered, a flat collar, a long thin shaft, the motor
// on top of it with the arm leaving its side, the pendulum's joint, the rod
// and the mass.
//
// The joint, along the arm's axis: the arm runs straight to the pendulum's
// hub, which turns with theta2 and holds the rod, radially, in the plane of
// the pendulum (at the distance L from the motor's axis). The parts only
// touch, face to face or the rod on the hub's skin, never through each
// other, so which one is in front is the same everywhere they overlap on
// screen.
struct Geometry {
    double L = 0.085, l = 0.13;
    double baseRadius = 0.0, baseHeight = 0.0;
    double topRadius = 0.0, topZ = 0.0;  // the base's face, inside the chamfer
    double collarRadius = 0.0, collarTop = 0.0;
    double shaftRadius = 0.0, shaftTop = 0.0;
    double motorRadius = 0.0, motorTop = 0.0;
    double armZ = 0.0, armRadius = 0.0;
    double hubRadius = 0.0, hubHalf = 0.0;
    double rodRadius = 0.0, massRadius = 0.0;

    Geometry(double armLength, double pendulumLength) : L(armLength), l(pendulumLength) {
        baseRadius = 0.42 * L;
        baseHeight = 0.74 * L;
        topRadius = 0.367 * L;
        topZ = baseHeight + 0.055 * L;
        collarRadius = 0.156 * L;
        collarTop = topZ + 0.075 * L;
        shaftRadius = 0.045 * L;
        shaftTop = collarTop + 0.30 * L;
        motorRadius = 0.122 * L;
        motorTop = shaftTop + 0.22 * L;
        armZ = shaftTop + 0.06 * L;
        armRadius = 0.033 * L;
        hubRadius = 0.065 * L;
        hubHalf = 0.05 * L;
        rodRadius = 0.03 * L;
        massRadius = 0.122 * L;
    }

    V3 armDirection(double theta1) const {
        return {std::cos(theta1), std::sin(theta1), 0.0};
    }
    // The pivot: the pendulum's hub, on the arm's axis at L.
    V3 pivot(double theta1) const {
        return V3{0, 0, armZ} + L * armDirection(theta1);
    }
    // Along the pendulum, 0 upright, > 0 toward increasing theta1.
    V3 pendulumDirection(double theta1, double theta2) const {
        const V3 tangent{-std::sin(theta1), std::cos(theta1), 0.0};
        return std::sin(theta2) * tangent + V3{0, 0, std::cos(theta2)};
    }
    V3 mass(double theta1, double theta2) const {
        return pivot(theta1) + l * pendulumDirection(theta1, theta2);
    }
};

// A circle of radius r around `center` in the plane normal to `axis`.
QPolygonF circleOnScreen(const Camera& camera, const V3& center, const V3& axis, double r,
                         int segments = 64) {
    const V3 n = normalized(axis);
    const V3 helper = std::fabs(n.z) < 0.9 ? V3{0, 0, 1} : V3{1, 0, 0};
    const V3 e1 = normalized(cross(n, helper));
    const V3 e2 = cross(n, e1);
    QPolygonF polygon;
    polygon.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        const double a = 2.0 * M_PI * i / segments;
        polygon.append(camera.project(center + r * (std::cos(a) * e1 + std::sin(a) * e2)));
    }
    return polygon;
}

double crossZ(const QPointF& o, const QPointF& a, const QPointF& b) {
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

// Andrew's monotone chain.
QPolygonF convexHull(QVector<QPointF> points) {
    std::sort(points.begin(), points.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    if (points.size() < 3) {
        return QPolygonF(points);
    }
    QVector<QPointF> hull(2 * points.size());
    int k = 0;
    for (const QPointF& p : points) {
        while (k >= 2 && crossZ(hull[k - 2], hull[k - 1], p) <= 0) {
            --k;
        }
        hull[k++] = p;
    }
    for (int i = int(points.size()) - 2, t = k + 1; i >= 0; --i) {
        while (k >= t && crossZ(hull[k - 2], hull[k - 1], points[i]) <= 0) {
            --k;
        }
        hull[k++] = points[i];
    }
    hull.resize(k - 1);
    return QPolygonF(hull);
}

// One solid part: a capped cylinder from a to b or a sphere (centre a).
struct Solid {
    bool sphere = false;
    V3 a, b;
    double r = 0.0;
    double r2 = 0.0;    // the radius at b (a frustum when it differs from r)
    QPolygonF outline;     // on screen
    double centreDepth = 0.0;
};

Solid makeCylinder(const Camera& camera, const V3& a, const V3& b, double r, double r2 = -1.0) {
    Solid s;
    s.a = a;
    s.b = b;
    s.r = r;
    s.r2 = r2 >= 0.0 ? r2 : r;
    QVector<QPointF> all = circleOnScreen(camera, a, b - a, r);
    all += circleOnScreen(camera, b, b - a, s.r2);
    s.outline = convexHull(all);
    s.centreDepth = camera.depth(0.5 * (a + b));
    return s;
}

Solid makeSphere(const Camera& camera, const V3& centre, double r) {
    Solid s;
    s.sphere = true;
    s.a = s.b = centre;
    s.r = r;
    const QPointF c = camera.project(centre);
    const double radius = r * camera.scale;
    for (int i = 0; i < 48; ++i) {
        const double a = 2.0 * M_PI * i / 48;
        s.outline.append(c + QPointF(radius * std::cos(a), radius * std::sin(a)));
    }
    s.centreDepth = camera.depth(centre);
    return s;
}

constexpr double kNoHit = -std::numeric_limits<double>::infinity();

// The depth of the solid's nearest surface under a screen point (kNoHit
// when the ray misses it).
double frontDepth(const Camera& camera, const Solid& s, const QPointF& screen) {
    const V3 w0 = camera.unproject(screen);
    const V3& d = camera.toViewer;
    if (s.sphere) {
        const V3 w = w0 - s.a;
        const double b = dot(w, d);
        const double disc = b * b - (dot(w, w) - s.r * s.r);
        return disc < 0.0 ? kNoHit : -b + std::sqrt(disc);
    }
    const V3 axis = s.b - s.a;
    const double h = std::sqrt(dot(axis, axis));
    if (h <= 0.0) {
        return kNoHit;
    }
    const V3 u = (1.0 / h) * axis;
    const V3 w = w0 - s.a;
    const double du = dot(d, u), wu = dot(w, u);
    const V3 dp = d - du * u, wp = w - wu * u;
    double best = kNoHit;
    // The side.
    const double A = dot(dp, dp);
    if (A > 1e-15) {
        const double B = 2.0 * dot(wp, dp);
        const double side = std::max(s.r, s.r2);  // a frustum: its enclosing cylinder
        const double C = dot(wp, wp) - side * side;
        const double disc = B * B - 4.0 * A * C;
        if (disc >= 0.0) {
            for (double sign : {-1.0, 1.0}) {
                const double t = (-B + sign * std::sqrt(disc)) / (2.0 * A);
                const double along = wu + t * du;
                if (along >= 0.0 && along <= h) {
                    best = std::max(best, t);
                }
            }
        }
    }
    // The caps.
    if (std::fabs(du) > 1e-15) {
        for (double cap : {0.0, h}) {
            const double t = (cap - wu) / du;
            const V3 radial = wp + t * dp;
            const double capRadius = cap == 0.0 ? s.r : s.r2;
            if (dot(radial, radial) <= capRadius * capRadius) {
                best = std::max(best, t);
            }
        }
    }
    return best;
}

// Far to near, decided where two parts overlap on screen: the one whose
// surface is nearer there goes later. Parts that do not overlap keep the
// order of their centres; a cycle (parts through each other) is broken by
// the farthest centre.
std::vector<int> paintOrder(const Camera& camera, const std::vector<Solid>& solids) {
    const int n = int(solids.size());
    std::vector<std::vector<int>> after(static_cast<size_t>(n));  // i before each of after[i]
    std::vector<int> pending(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (!solids[i].outline.boundingRect().intersects(solids[j].outline.boundingRect())) {
                continue;
            }
            QPainterPath pi, pj;
            pi.addPolygon(solids[i].outline);
            pj.addPolygon(solids[j].outline);
            const QPainterPath both = pi.intersected(pj);
            if (both.isEmpty()) {
                continue;
            }
            const QRectF box = both.boundingRect();
            int votes = 0;
            constexpr int kGrid = 7;
            for (int gx = 0; gx < kGrid; ++gx) {
                for (int gy = 0; gy < kGrid; ++gy) {
                    const QPointF p(box.left() + (gx + 0.5) * box.width() / kGrid,
                                    box.top() + (gy + 0.5) * box.height() / kGrid);
                    if (!both.contains(p)) {
                        continue;
                    }
                    const double ti = frontDepth(camera, solids[i], p);
                    const double tj = frontDepth(camera, solids[j], p);
                    if (ti == kNoHit || tj == kNoHit) {
                        continue;
                    }
                    votes += ti > tj ? 1 : (ti < tj ? -1 : 0);
                }
            }
            if (votes > 0) {  // i in front: j first
                after[size_t(j)].push_back(i);
                ++pending[size_t(i)];
            } else if (votes < 0) {
                after[size_t(i)].push_back(j);
                ++pending[size_t(j)];
            }
        }
    }
    std::vector<int> order;
    std::vector<bool> done(static_cast<size_t>(n), false);
    for (int step = 0; step < n; ++step) {
        int pick = -1;
        for (const bool free : {true, false}) {
            for (int i = 0; i < n; ++i) {
                if (done[size_t(i)] || (free && pending[size_t(i)] > 0)) {
                    continue;
                }
                if (pick < 0 || solids[size_t(i)].centreDepth < solids[size_t(pick)].centreDepth) {
                    pick = i;
                }
            }
            if (pick >= 0) {
                break;
            }
        }
        done[size_t(pick)] = true;
        order.push_back(pick);
        for (int next : after[size_t(pick)]) {
            --pending[size_t(next)];
        }
    }
    return order;
}

// Whether a point of the world is in front of every solid along its ray.
bool visible(const Camera& camera, const std::vector<Solid>& solids, const V3& p,
             double tolerance) {
    const double depth = camera.depth(p);
    const QPointF screen = camera.project(p);
    for (const Solid& s : solids) {
        if (!s.outline.boundingRect().contains(screen)) {
            continue;
        }
        if (frontDepth(camera, s, screen) > depth + tolerance) {
            return false;
        }
    }
    return true;
}

// A 3D polyline drawn only where the solids leave it in view. Returns
// whether its last point is visible.
bool drawOccluded(QPainter& painter, const Camera& camera, const std::vector<Solid>& solids,
                  const QVector<V3>& points, const QPen& pen, double tolerance) {
    if (points.size() < 2) {
        return false;
    }
    constexpr int kPieces = 6;
    QPainterPath path;
    bool drawing = false;
    bool lastVisible = false;
    for (int i = 0; i + 1 < points.size(); ++i) {
        for (int k = (i == 0 ? 0 : 1); k <= kPieces; ++k) {
            const V3 p = points[i] + (double(k) / kPieces) * (points[i + 1] - points[i]);
            lastVisible = visible(camera, solids, p, tolerance);
            const QPointF q = camera.project(p);
            if (!lastVisible) {
                drawing = false;
            } else if (!drawing) {
                path.moveTo(q);
                drawing = true;
            } else {
                path.lineTo(q);
            }
        }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(pen);
    painter.drawPath(path);
    return lastVisible;
}

void drawArrowHead(QPainter& painter, const QPointF& tip, const QPointF& direction, double size,
                   const QColor& color) {
    const double n = std::hypot(direction.x(), direction.y());
    if (n <= 0.0) {
        return;
    }
    const QPointF d(direction.x() / n, direction.y() / n);
    const QPointF side(-d.y(), d.x());
    QPolygonF head;
    head << tip << tip - size * d + 0.45 * size * side << tip - size * d - 0.45 * size * side;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawPolygon(head);
}

// A 3D arc with an arrow at its end, hidden where the solids are in front.
void drawArc(QPainter& painter, const Camera& camera, const std::vector<Solid>& solids,
             const QVector<V3>& arc, const QColor& color, double width, double head,
             double tolerance) {
    if (arc.size() < 2) {
        return;
    }
    const QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    if (drawOccluded(painter, camera, solids, arc, pen, tolerance)) {
        const QPointF tip = camera.project(arc.last());
        drawArrowHead(painter, tip, tip - camera.project(arc[arc.size() - 2]), head, color);
    }
}

void drawSolid(QPainter& painter, const Camera& camera, const Solid& s, double penWidth) {
    painter.setPen(QPen(kInk, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(kPaper);
    if (!s.sphere) {
        painter.drawPolygon(s.outline);
        const V3 axis = s.b - s.a;
        const bool showsB = dot(axis, camera.toViewer) >= 0.0;
        if (s.r2 != s.r) {
            // A chamfer seen from above: its slope shows both edges.
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(circleOnScreen(camera, s.a, axis, s.r));
            painter.setBrush(kPaper);
            painter.drawPolygon(circleOnScreen(camera, s.b, axis, s.r2));
            return;
        }
        painter.drawPolygon(circleOnScreen(camera, showsB ? s.b : s.a, axis, s.r));
        return;
    }
    // The sphere: its outline, the near half of the equator, and the
    // figure's highlight arc.
    const QPointF c = camera.project(s.a);
    const double radius = s.r * camera.scale;
    painter.drawEllipse(c, radius, radius);
    QPainterPath near;
    bool drawing = false;
    constexpr int kSegments = 48;
    for (int i = 0; i <= kSegments; ++i) {
        const double a = 2.0 * M_PI * i / kSegments;
        const V3 offset{-std::sin(a), std::cos(a), 0.0};
        const QPointF p = camera.project(s.a + s.r * offset);
        if (dot(offset, camera.toViewer) < 0.0) {
            drawing = false;
        } else if (!drawing) {
            near.moveTo(p);
            drawing = true;
        } else {
            near.lineTo(p);
        }
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(60, 60, 60), penWidth * 0.8));
    painter.drawPath(near);
    painter.setPen(QPen(QColor(90, 90, 90), penWidth * 0.8, Qt::SolidLine, Qt::RoundCap));
    const QRectF box(c.x() - 0.62 * radius, c.y() - 0.62 * radius, 1.24 * radius, 1.24 * radius);
    painter.drawArc(box, 95 * 16, 60 * 16);
}

// "theta" with a subscript, the figure's Times italic, its baseline-left at p.
void drawSymbol(QPainter& painter, const QPointF& p, const QString& base, const QString& sub,
                double size, const QColor& color) {
    QFont font(QStringLiteral("Times New Roman"));
    font.setStyleHint(QFont::Serif);
    font.setItalic(true);
    font.setPixelSize(qMax(9, qRound(size)));
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(p, base);
    const double advance = QFontMetricsF(font).horizontalAdvance(base);
    if (!sub.isEmpty()) {
        font.setPixelSize(qMax(7, qRound(size * 0.65)));
        painter.setFont(font);
        painter.drawText(p + QPointF(advance, 0.28 * size), sub);
    }
}

}  // namespace

FurutaScene::FurutaScene() {
    for (const SceneParam& param : params()) {
        m_params.insert(param.name, param.defaultValue);
    }
}

QList<SceneInput> FurutaScene::inputs() const {
    // Only the two angles: no default topic, so "Bind by name" takes the
    // fields called theta1 and theta2, wherever the device reports them.
    using Kind = SceneInput::Kind;
    return {
        {QStringLiteral("theta1"), QStringLiteral("rad"), Kind::Scalar, QString(),
         QStringLiteral("theta1")},
        {QStringLiteral("theta2"), QStringLiteral("rad"), Kind::Scalar, QString(),
         QStringLiteral("theta2")},
    };
}

QList<SceneParam> FurutaScene::params() const {
    const QString m = QStringLiteral("m");
    const QString deg = QStringLiteral("deg");
    return {
        {kArmLength, QCoreApplication::translate("FurutaScene", "Arm length (L1)"), m, 0.085, 0.01,
         2.0, 3},
        {kPendulumLength, QCoreApplication::translate("FurutaScene", "Pendulum length (l2)"), m,
         0.13, 0.01, 2.0, 3},
        {kAzimuth, QCoreApplication::translate("FurutaScene", "View azimuth"), deg, -45.0, -360.0,
         360.0, 0},
        {kElevation, QCoreApplication::translate("FurutaScene", "View elevation"), deg, 45.0, -10.0,
         89.0, 0},
        // Toggles, off by default: the figure's annotations (the angles' arcs
        // and the lengths' labels) and the mass's recent path.
        {kLabels, QCoreApplication::translate("FurutaScene", "Angles and labels"), QString(), 0.0,
         0.0, 1.0, 0, true},
        {kTrail, QCoreApplication::translate("FurutaScene", "Trail of the mass"), QString(), 0.0,
         0.0, 1.0, 0, true},
        {kAntialiasing, QCoreApplication::translate("FurutaScene", "Antialiasing"), QString(), 1.0,
         0.0, 1.0, 0, true},
    };
}

void FurutaScene::setParam(const QString& name, const QVariant& value) {
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (ok && m_params.contains(name)) {
        m_params[name] = number;
    }
}

double FurutaScene::param(const QString& name) const {
    return m_params.value(name, qQNaN());
}

bool FurutaScene::antialiased() const {
    return param(kAntialiasing) != 0.0;
}

void FurutaScene::update(const SceneFrame& frame) {
    const QString theta1 = QStringLiteral("theta1");
    const QString theta2 = QStringLiteral("theta2");
    for (const SceneRecord& record : frame.records) {
        if (!record.values.contains(theta1) || !record.values.contains(theta2)) {
            continue;
        }
        m_theta1 = record.values.value(theta1);
        m_theta2 = record.values.value(theta2);
        m_hasAngles = true;
        m_trail.append({m_theta1, m_theta2});
        if (m_trail.size() > kTrailLength) {
            m_trail.remove(0, m_trail.size() - kTrailLength);
        }
    }
}

namespace {

Camera makeCamera(const SceneView& view, const Geometry& geo, double azimuthDeg,
                  double elevationDeg) {
    const double a = qDegreesToRadians(azimuthDeg + kOrbitPerPixel * view.pan.x());
    const double e = qDegreesToRadians(qBound(-10.0, elevationDeg + kOrbitPerPixel * view.pan.y(),
                                              89.0));
    Camera camera;
    camera.toViewer = {std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e)};
    camera.right = {-std::sin(a), std::cos(a), 0.0};
    camera.up = {-std::sin(e) * std::cos(a), -std::sin(e) * std::sin(a), std::cos(e)};

    // Fit what the mechanism can reach on screen: the base and the mass
    // anywhere on its sphere around the arm's circle.
    camera.scale = 1.0;
    camera.origin = QPointF(0, 0);
    QRectF box;
    const auto extend = [&](const V3& p) {
        const QPointF q = camera.project(p);
        box = box.isNull() ? QRectF(q, QSizeF(1e-9, 1e-9)) : box.united(QRectF(q, QSizeF(1e-9, 1e-9)));
    };
    for (int i = 0; i < 16; ++i) {
        const double t1 = 2.0 * M_PI * i / 16;
        extend(geo.baseRadius * V3{std::cos(t1), std::sin(t1), 0.0});
        for (int j = 0; j <= 8; ++j) {
            const double t2 = M_PI * j / 8;
            for (double side : {-1.0, 1.0}) {
                const V3 m = geo.mass(t1, side * t2);
                for (const V3& edge : {V3{0, 0, 1}, V3{0, 0, -1}, V3{1, 0, 0}, V3{-1, 0, 0},
                                       V3{0, 1, 0}, V3{0, -1, 0}}) {
                    extend(m + geo.massRadius * edge);
                }
            }
        }
    }
    const double w = view.size.width(), h = view.size.height();
    // The box holds every pose, most of which never happen at once (the
    // mass at the far side of its sphere, toward or away from the camera):
    // fitting it exactly leaves the machine small, so it is drawn larger and
    // only those extremes may leave the view.
    constexpr double kFill = 1.3;
    const double fit = kFill * std::min(0.94 * w / std::max(box.width(), 1e-9),
                                        0.86 * h / std::max(box.height(), 1e-9));
    camera.scale = fit * view.zoom;
    // That box's centre at the widget's centre, a little below.
    const QPointF centre = fit * box.center();
    camera.origin = QPointF(0.5 * w - centre.x(), 0.53 * h - centre.y());
    return camera;
}

// A disc of radius `r` on the floor (z = 0) around (x, y), filled with a
// radial gradient from `inner` at its centre to `outer` at its rim: a
// horizontal circle projects to an axis-aligned ellipse, flattened by the
// view's elevation.
void drawFloorDisc(QPainter& painter, const Camera& camera, double x, double y, double r,
                   const QColor& inner, const QColor& outer) {
    const double radius = r * camera.scale;
    const double flatten = std::max(std::fabs(camera.toViewer.z), 0.05);
    if (radius < 0.5) {
        return;
    }
    QRadialGradient gradient(QPointF(0, 0), radius);
    gradient.setColorAt(0.0, inner);
    gradient.setColorAt(1.0, outer);
    painter.save();
    painter.translate(camera.project({x, y, 0.0}));
    painter.scale(1.0, flatten);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawEllipse(QPointF(0, 0), radius, radius);
    painter.restore();
}

// The sky, the lit floor around the machine and the soft shadows of the
// base and of the mass cast straight down, under everything else. The arm
// and the rod cast none: a line on the floor behind the base showed above
// it, next to the motor, like a stray stroke on the machine.
void drawBackdrop(QPainter& painter, const Camera& camera, const Geometry& geo,
                  const QRectF& area, const V3& mass) {
    QLinearGradient sky(area.topLeft(), area.bottomLeft());
    sky.setColorAt(0.0, kSkyTop);
    sky.setColorAt(1.0, kSkyBottom);
    painter.fillRect(area, sky);

    QColor stageRim = kStage;
    stageRim.setAlpha(0);
    QColor stageCentre = kStage;
    stageCentre.setAlpha(235);
    drawFloorDisc(painter, camera, 0.0, 0.0, 1.25 * (geo.L + geo.l), stageCentre, stageRim);

    const auto shadow = [](double alpha) {
        QColor c = kShadow;
        c.setAlphaF(alpha);
        return c;
    };
    QColor clear = kShadow;
    clear.setAlpha(0);
    // Where the base stands, and under the mass.
    drawFloorDisc(painter, camera, 0.0, 0.0, 1.35 * geo.baseRadius, shadow(0.30), clear);
    drawFloorDisc(painter, camera, mass.x, mass.y, 1.8 * geo.massRadius, shadow(0.22), clear);
}

}  // namespace

QPointF FurutaScene::massOnScreen(const SceneView& view) const {
    const Geometry geo(param(kArmLength), param(kPendulumLength));
    const Camera camera = makeCamera(view, geo, param(kAzimuth), param(kElevation));
    return camera.project(geo.mass(m_theta1, m_theta2));
}

QPointF FurutaScene::pivotOnScreen(const SceneView& view) const {
    const Geometry geo(param(kArmLength), param(kPendulumLength));
    const Camera camera = makeCamera(view, geo, param(kAzimuth), param(kElevation));
    return camera.project(geo.pivot(m_theta1));
}

void FurutaScene::paint(QPainter& painter, const SceneView& view) {
    const bool antialiasing = antialiased();
    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing, antialiasing);
    const QRectF area(QPointF(0, 0), view.size);

    const Geometry geo(param(kArmLength), param(kPendulumLength));
    const Camera camera = makeCamera(view, geo, param(kAzimuth), param(kElevation));
    const double h = view.size.height();
    const double pen = qBound(1.0, h / 450.0, 3.0);
    const double text = qBound(11.0, h / 22.0, 40.0);
    const bool labels = param(kLabels) != 0.0;
    // Lines lying on a surface stay visible.
    const double tolerance = 0.01 * geo.L;

    const double th1 = m_theta1;
    const double th2 = m_theta2;
    const V3 dir = geo.armDirection(th1);
    const V3 pivot = geo.pivot(th1);
    const V3 along = geo.pendulumDirection(th1, th2);
    const V3 mass = geo.mass(th1, th2);
    drawBackdrop(painter, camera, geo, area, mass);

    // The solid parts, painted far to near where they overlap.
    std::vector<Solid> solids;
    const auto vertical = [&](double z0, double z1, double r) {
        solids.push_back(makeCylinder(camera, {0, 0, z0}, {0, 0, z1}, r));
    };
    vertical(0.0, geo.baseHeight, geo.baseRadius);
    solids.push_back(makeCylinder(camera, {0, 0, geo.baseHeight}, {0, 0, geo.topZ},
                                  geo.baseRadius, geo.topRadius));
    vertical(geo.topZ, geo.collarTop, geo.collarRadius);
    vertical(geo.collarTop, geo.shaftTop, geo.shaftRadius);
    vertical(geo.shaftTop, geo.motorTop, geo.motorRadius);
    const V3 armAxis{0, 0, geo.armZ};
    // The arm, from the motor's side straight to the pendulum's hub; the hub;
    // the rod from the hub's skin to the mass.
    solids.push_back(makeCylinder(camera, armAxis + 0.9 * geo.motorRadius * dir,
                                  pivot - geo.hubHalf * dir, geo.armRadius));
    solids.push_back(makeCylinder(camera, pivot - geo.hubHalf * dir, pivot + geo.hubHalf * dir,
                                  geo.hubRadius));
    const double rodRoot =
        std::sqrt(geo.hubRadius * geo.hubRadius - geo.rodRadius * geo.rodRadius);
    solids.push_back(makeCylinder(camera, pivot + rodRoot * along,
                                  mass - 0.9 * geo.massRadius * along, geo.rodRadius));
    solids.push_back(makeSphere(camera, mass, geo.massRadius));
    for (int index : paintOrder(camera, solids)) {
        drawSolid(painter, camera, solids[size_t(index)], pen);
    }

    // The mass's recent path and the controller's predicted one, where the
    // solids leave them in view.
    if (param(kTrail) != 0.0 && m_trail.size() > 1) {
        for (int i = 1; i < m_trail.size(); ++i) {
            QColor color = kTrailColor;
            color.setAlphaF(0.6 * i / m_trail.size());
            drawOccluded(painter, camera, solids,
                         {geo.mass(m_trail[i - 1].theta1, m_trail[i - 1].theta2),
                          geo.mass(m_trail[i].theta1, m_trail[i].theta2)},
                         QPen(color, pen, Qt::SolidLine, Qt::RoundCap), tolerance);
        }
    }

    if (labels) {
        // theta1: from the x axis (dashed) to the arm, in the arm's plane.
        QPen dashed(kGuide, pen, Qt::DashLine);
        dashed.setDashPattern({5, 4});
        drawOccluded(painter, camera, solids,
                     {armAxis + V3{geo.motorRadius, 0, 0}, armAxis + V3{1.45 * geo.L, 0, 0}},
                     dashed, tolerance);
        const double a1 = wrapAngle(th1);
        const double r1 = 0.62 * geo.L;
        if (std::fabs(a1) > 0.03) {
            QVector<V3> arc;
            for (int i = 0; i <= 30; ++i) {
                const double a = a1 * i / 30.0;
                arc.append(armAxis + V3{r1 * std::cos(a), r1 * std::sin(a), 0});
            }
            drawArc(painter, camera, solids, arc, kInk, pen, 6.0 * pen, tolerance);
        }
        // The label below the middle of the arc (of a small one, past it).
        const double mid1 = std::fabs(a1) > 0.6 ? 0.5 * a1 : (a1 >= 0 ? -0.35 : 0.35);
        drawSymbol(painter,
                   camera.project(armAxis + V3{1.1 * r1 * std::cos(mid1), 1.1 * r1 * std::sin(mid1), 0}) +
                       QPointF(0.1 * text, 1.0 * text),
                   QStringLiteral("θ"), QStringLiteral("1"), text, kInk);

        // theta2: from the vertical (dashed) to the pendulum, in its plane.
        drawOccluded(painter, camera, solids, {pivot, pivot + V3{0, 0, 0.85 * geo.l}}, dashed,
                     tolerance);
        const double a2 = wrapAngle(th2);
        const double r2 = 0.42 * geo.l;
        const V3 tangent{-std::sin(th1), std::cos(th1), 0.0};
        if (std::fabs(a2) > 0.03) {
            QVector<V3> arc;
            for (int i = 0; i <= 30; ++i) {
                const double a = a2 * i / 30.0;
                arc.append(pivot + r2 * (std::sin(a) * tangent + V3{0, 0, std::cos(a)}));
            }
            drawArc(painter, camera, solids, arc, kInk, pen, 6.0 * pen, tolerance);
        }
        const double mid2 = 0.5 * a2;
        const QPointF at2 =
            camera.project(pivot + 1.15 * r2 * (std::sin(mid2) * tangent + V3{0, 0, std::cos(mid2)}));
        drawSymbol(painter, at2 + QPointF(-1.3 * text, 0.0), QStringLiteral("θ"),
                   QStringLiteral("2"), text, kInk);

        // L1 beside the arm, l2 beside the pendulum, m2 by the mass.
        const auto above = [&](const V3& from, const V3& to, double offset) {
            const QPointF a = camera.project(from), b = camera.project(to);
            const QPointF d = b - a;
            const double n = std::hypot(d.x(), d.y());
            QPointF normal = n > 0 ? QPointF(-d.y() / n, d.x() / n) : QPointF(0, -1);
            if (normal.y() > 0) {
                normal = -normal;
            }
            return 0.5 * (a + b) + offset * normal;
        };
        drawSymbol(painter, above(armAxis, pivot, 0.9 * text) - QPointF(0.5 * text, 0),
                   QStringLiteral("L"), QStringLiteral("1"), text, kInk);
        const QPointF rodA = camera.project(pivot), rodB = camera.project(mass);
        QPointF rodNormal(-(rodB - rodA).y(), (rodB - rodA).x());
        const double rodLength = std::hypot(rodNormal.x(), rodNormal.y());
        if (rodLength > 0) {
            rodNormal /= rodLength;
        }
        if (rodNormal.x() < 0) {
            rodNormal = -rodNormal;  // right of the rod
        }
        drawSymbol(painter, 0.5 * (rodA + rodB) + 0.9 * text * rodNormal + QPointF(0, 0.3 * text),
                   QStringLiteral("l"), QStringLiteral("2"), text, kInk);
        const QPointF massAt = camera.project(mass);
        const double massPx = geo.massRadius * camera.scale;
        drawSymbol(painter, massAt + QPointF(massPx + 0.2 * text, -0.4 * massPx), QStringLiteral("m"),
                   QStringLiteral("2"), text, kInk);
    }

    // The axes, bottom left, turning with the view.
    {
        const QPointF origin(1.6 * text, h - 2.6 * text);
        const double length = 2.2 * text;
        const struct {
            V3 axis;
            const char* name;
        } axes[] = {{{1, 0, 0}, "x"}, {{0, 1, 0}, "y"}, {{0, 0, 1}, "z"}};
        for (const auto& axis : axes) {
            const QPointF d(dot(axis.axis, camera.right), -dot(axis.axis, camera.up));
            const QPointF tip = origin + length * d;
            painter.setPen(QPen(kInk, pen));
            painter.drawLine(origin, tip);
            drawArrowHead(painter, tip, d, 5.0 * pen, kInk);
            const double n = std::hypot(d.x(), d.y());
            const QPointF label = n > 0.2 ? tip + (0.55 * text / n) * d : tip;
            drawSymbol(painter, label + QPointF(-0.25 * text, 0.35 * text),
                       QString::fromLatin1(axis.name), QString(), 0.85 * text, kInk);
        }
    }
}

}  // namespace traceview
