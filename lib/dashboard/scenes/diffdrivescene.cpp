#include "diffdrivescene.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace traceview {

namespace {

// Param keys (also the .tvproj "params" keys).
const QString kScale = QStringLiteral("scale_px_per_m");
const QString kWheelDistance = QStringLiteral("wheel_distance_m");
const QString kSensorDistance = QStringLiteral("sensor_distance_m");
const QString kSensorCount = QStringLiteral("sensor_count");
const QString kSensorSpacing = QStringLiteral("sensor_spacing_m");
const QString kLineRadius = QStringLiteral("line_radius_m");
const QString kRenderRadius = QStringLiteral("render_radius_m");
const QString kPerturbationStep = QStringLiteral("perturbation_step");
const QString kAntialiasing = QStringLiteral("antialiasing");

// A floor dot is left out where a track point is this close (m).
constexpr double kFloorDotClearance = 0.667;
// Without a track, the minimap shows +-50 m around the origin (pygame's).
constexpr double kDefaultMinimapHalfExtent = 50.0;

const QColor kBackground(255, 255, 255);
const QColor kTrack(0, 0, 0);
const QColor kTrackReached(100, 100, 100);
const QColor kFloorDot(50, 50, 50);
const QColor kCar(0, 0, 200);
const QColor kSensor(150, 150, 150);
const QColor kReference(200, 0, 0);
const QColor kPrediction(0, 120, 255);
const QColor kHud(0, 200, 0);
const QColor kInfo(0, 0, 0);
const QColor kCompassPointer(255, 0, 0);
const QColor kCompassNorth(200, 0, 0);

qint64 cellKey(qint64 ix, qint64 iy) {
    return (ix << 32) ^ (iy & 0xffffffffLL);
}

// The pygame layout was drawn for a 1080 px tall window: fonts and the HUD
// pieces scale with the widget height.
double hudScale(const SceneView& view) {
    return view.size.height() / 1080.0;
}

QFont hudFont(const SceneView& view) {
    QFont font(QStringLiteral("Courier New"));
    font.setStyleHint(QFont::TypeWriter);
    font.setBold(true);
    font.setPixelSize(qMax(10, qRound(25.0 * hudScale(view))));
    return font;
}

enum class Anchor { Left, Center, Right };

// `top` is the text's top edge, x its left edge, centre or right edge.
void drawHudText(QPainter& painter, const QString& text, double x, double top, Anchor anchor,
                 const QColor& color) {
    const QFontMetricsF metrics(painter.font());
    const double width = metrics.horizontalAdvance(text);
    double left = x;
    if (anchor == Anchor::Center) {
        left = x - width / 2.0;
    } else if (anchor == Anchor::Right) {
        left = x - width;
    }
    painter.setPen(color);
    painter.drawText(QPointF(left, top + metrics.ascent()), text);
}

}  // namespace

DiffDriveScene::DiffDriveScene() {
    for (const SceneParam& param : params()) {
        m_params.insert(param.name, param.defaultValue);
    }
}

QList<SceneInput> DiffDriveScene::inputs() const {
    using Kind = SceneInput::Kind;
    const QString truth = QStringLiteral("sim.truth");
    const QString preview = QStringLiteral("sim.preview");
    const QString diag = QStringLiteral("ctl.diag");
    return {
        {QStringLiteral("x"), QStringLiteral("m"), Kind::Scalar, truth, QStringLiteral("x")},
        {QStringLiteral("y"), QStringLiteral("m"), Kind::Scalar, truth, QStringLiteral("y")},
        {QStringLiteral("theta"), QStringLiteral("rad"), Kind::Scalar, truth,
         QStringLiteral("theta")},
        {QStringLiteral("tick"), QString(), Kind::Scalar, truth, QStringLiteral("tick"), true},
        {QStringLiteral("next_point"), QString(), Kind::Scalar, truth, QStringLiteral("next_point"),
         true},
        {QStringLiteral("coverage"), QStringLiteral("%"), Kind::Scalar, truth,
         QStringLiteral("coverage"), true},
        {QStringLiteral("t"), QStringLiteral("s"), Kind::Scalar, truth, QStringLiteral("t"), true},
        {QStringLiteral("track"), QString(), Kind::Text, QStringLiteral("sim.track"), QString()},
        {QStringLiteral("preview_tick"), QString(), Kind::Scalar, preview, QStringLiteral("tick"),
         true},
        {QStringLiteral("preview_x"), QStringLiteral("m"), Kind::Array, preview,
         QStringLiteral("px"), true},
        {QStringLiteral("preview_y"), QStringLiteral("m"), Kind::Array, preview,
         QStringLiteral("py"), true},
        {QStringLiteral("pred_tick"), QString(), Kind::Scalar, diag, QStringLiteral("ref_tick"),
         true},
        {QStringLiteral("pred_x"), QStringLiteral("m"), Kind::Array, diag, QStringLiteral("pred_x"),
         true},
        {QStringLiteral("pred_y"), QStringLiteral("m"), Kind::Array, diag, QStringLiteral("pred_y"),
         true},
        {QStringLiteral("qps"), QString(), Kind::Scalar, diag, QStringLiteral("iterations"), true},
        {QStringLiteral("solve_ms"), QStringLiteral("ms"), Kind::Scalar, diag,
         QStringLiteral("solve_ms"), true},
        {QStringLiteral("controller"), QString(), Kind::Text, QStringLiteral("ctl.info"), QString(),
         true},
    };
}

QList<SceneParam> DiffDriveScene::params() const {
    const QString m = QStringLiteral("m");
    return {
        {kScale, QCoreApplication::translate("DiffDriveScene", "Scale"), QStringLiteral("px/m"),
         300.0, 10.0, 5000.0, 0},
        {kWheelDistance, QCoreApplication::translate("DiffDriveScene", "Wheel distance"), m, 0.15,
         0.01, 2.0, 3},
        {kSensorDistance, QCoreApplication::translate("DiffDriveScene", "Sensor distance"), m, 0.15,
         0.0, 2.0, 3},
        {kSensorCount, QCoreApplication::translate("DiffDriveScene", "Sensor count"), QString(),
         15.0, 1.0, 64.0, 0},
        {kSensorSpacing, QCoreApplication::translate("DiffDriveScene", "Sensor spacing"), m, 0.008,
         0.001, 0.1, 3},
        {kLineRadius, QCoreApplication::translate("DiffDriveScene", "Line radius"), m, 0.02, 0.001,
         0.2, 3},
        {kRenderRadius, QCoreApplication::translate("DiffDriveScene", "Render radius"), m, 4.3, 0.5,
         50.0, 1},
        {kPerturbationStep, QCoreApplication::translate("DiffDriveScene", "Perturbation step"),
         QStringLiteral("%"), 5.0, 0.1, 100.0, 1},
        // Smoother shapes and text; off is cheaper to paint (pygame's look).
        {kAntialiasing, QCoreApplication::translate("DiffDriveScene", "Antialiasing"), QString(),
         1.0, 0.0, 1.0, 0, true},
    };
}

void DiffDriveScene::setParam(const QString& name, const QVariant& value) {
    bool ok = false;
    const double number = value.toDouble(&ok);
    if (ok && m_params.contains(name)) {
        m_params[name] = number;
    }
}

bool DiffDriveScene::antialiased() const {
    return param(kAntialiasing) != 0.0;
}

double DiffDriveScene::param(const QString& name) const {
    return m_params.value(name, qQNaN());
}

void DiffDriveScene::update(const SceneFrame& frame) {
    const auto carFrame = [](const SceneRecord& record, const QString& tickInput,
                             const QString& xInput, const QString& yInput, CarFramePoints* out) {
        if (!record.arrays.contains(xInput) || !record.arrays.contains(yInput)) {
            return;
        }
        const QVector<double> xs = record.arrays.value(xInput);
        const QVector<double> ys = record.arrays.value(yInput);
        out->tick = record.values.contains(tickInput) ? qint64(record.values.value(tickInput)) : -1;
        out->points.clear();
        const int count = qMin(xs.size(), ys.size());
        for (int i = 0; i < count; ++i) {
            out->points.append({xs[i], ys[i]});
        }
    };

    for (const SceneRecord& record : frame.records) {
        const bool hasPose = record.values.contains(QStringLiteral("x")) &&
                             record.values.contains(QStringLiteral("y")) &&
                             record.values.contains(QStringLiteral("theta"));
        if (hasPose) {
            const DiffDrivePose pose{record.values.value(QStringLiteral("x")),
                                     record.values.value(QStringLiteral("y")),
                                     record.values.value(QStringLiteral("theta"))};
            if (record.values.contains(QStringLiteral("tick"))) {
                const qint64 tick = qint64(record.values.value(QStringLiteral("tick")));
                // A tick going back is a sim.reset: the old history and the
                // elapsed time no longer apply.
                if (!m_poseByTick.isEmpty() && tick < m_poseByTick.lastKey()) {
                    m_poseByTick.clear();
                    m_firstPoseUs = 0;
                }
                m_poseByTick.insert(tick, pose);
                while (m_poseByTick.size() > kPoseHistory) {
                    m_poseByTick.erase(m_poseByTick.begin());
                }
            }
            if (m_firstPoseUs == 0) {
                m_firstPoseUs = record.timestampUs;
            }
            m_lastPoseUs = record.timestampUs;
        }
        carFrame(record, QStringLiteral("preview_tick"), QStringLiteral("preview_x"),
                 QStringLiteral("preview_y"), &m_preview);
        carFrame(record, QStringLiteral("pred_tick"), QStringLiteral("pred_x"),
                 QStringLiteral("pred_y"), &m_prediction);
    }

    // The newest values, whichever topics they came from.
    const QHash<QString, double>& values = frame.values;
    if (values.contains(QStringLiteral("x")) && values.contains(QStringLiteral("y")) &&
        values.contains(QStringLiteral("theta"))) {
        m_pose = {values.value(QStringLiteral("x")), values.value(QStringLiteral("y")),
                  values.value(QStringLiteral("theta"))};
        m_hasPose = true;
    }
    m_nextPoint = values.contains(QStringLiteral("next_point"))
                      ? int(values.value(QStringLiteral("next_point")))
                      : -1;
    m_coverage = values.value(QStringLiteral("coverage"), qQNaN());
    m_time = values.value(QStringLiteral("t"), qQNaN());
    m_qps = values.value(QStringLiteral("qps"), qQNaN());
    m_solveMs = values.value(QStringLiteral("solve_ms"), qQNaN());
}

void DiffDriveScene::document(const QString& input, const QByteArray& body) {
    if (input == QLatin1String("controller")) {
        m_controller = QString::fromUtf8(body).trimmed();
        return;
    }
    if (input != QLatin1String("track")) {
        return;
    }
    // {"points": [[x, y], ...], "closed": true}, metres.
    const QJsonArray array =
        QJsonDocument::fromJson(body).object().value(QLatin1String("points")).toArray();
    QVector<QPointF> points;
    points.reserve(array.size());
    for (const QJsonValue& value : array) {
        const QJsonArray pair = value.toArray();
        if (pair.size() >= 2) {
            points.append({pair.at(0).toDouble(), pair.at(1).toDouble()});
        }
    }
    setTrack(points);
}

std::optional<SceneAction> DiffDriveScene::keyAction(int key) {
    const double step = param(kPerturbationStep);
    switch (key) {
        case Qt::Key_P:
            m_perturbation += step;
            break;
        case Qt::Key_O:
            m_perturbation -= step;
            break;
        case Qt::Key_R:
            m_perturbation = 0.0;
            break;
        default:
            return std::nullopt;
    }
    return SceneAction{QStringLiteral("sim.disturb"), m_perturbation};
}

DiffDrivePose DiffDriveScene::pose() const {
    if (m_hasPose || m_track.size() < 2) {
        return m_pose;
    }
    // No pose yet: sit on the start of the track, facing along it.
    const QPointF d = m_track[1] - m_track[0];
    return {m_track[0].x(), m_track[0].y(), std::atan2(d.y(), d.x())};
}

QPointF DiffDriveScene::anchor(const SceneView& view) const {
    return diffDriveCarAnchor(view.size) + view.pan;
}

double DiffDriveScene::scale(const SceneView& view) const {
    return param(kScale) * view.zoom;
}

QPointF DiffDriveScene::worldToScreen(const QPointF& world, const SceneView& view) const {
    return diffDriveWorldToScreen(world, pose(), anchor(view), scale(view));
}

QPointF DiffDriveScene::carPointToScreen(const QPointF& carPoint, qint64 tick,
                                         const SceneView& view) const {
    return worldToScreen(diffDriveCarToWorld(carPoint, poseOfTick(tick)), view);
}

int DiffDriveScene::trackPointCount() const {
    return m_track.size();
}

void DiffDriveScene::setTrack(const QVector<QPointF>& points) {
    m_track = points;
    m_trackCells.clear();
    m_blockedLatticeNodes.clear();
    m_trackBounds = QRectF();
    if (points.isEmpty()) {
        return;
    }
    double minX = points[0].x();
    double maxX = minX;
    double minY = points[0].y();
    double maxY = minY;
    const int reach = int(std::ceil(kFloorDotClearance));
    for (int i = 0; i < points.size(); ++i) {
        const QPointF& p = points[i];
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
        m_trackCells[cellKey(qint64(std::floor(p.x())), qint64(std::floor(p.y())))].append(i);
        const qint64 nx = qint64(std::round(p.x()));
        const qint64 ny = qint64(std::round(p.y()));
        for (qint64 ix = nx - reach; ix <= nx + reach; ++ix) {
            for (qint64 iy = ny - reach; iy <= ny + reach; ++iy) {
                if (std::hypot(double(ix) - p.x(), double(iy) - p.y()) < kFloorDotClearance) {
                    m_blockedLatticeNodes.insert(cellKey(ix, iy));
                }
            }
        }
    }
    m_trackBounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

DiffDrivePose DiffDriveScene::poseOfTick(qint64 tick) const {
    const auto it = m_poseByTick.constFind(tick);
    return it != m_poseByTick.constEnd() ? it.value() : pose();
}

QVector<int> DiffDriveScene::trackPointsNear(const QPointF& center, double radius) const {
    QVector<int> indices;
    const qint64 x0 = qint64(std::floor(center.x() - radius));
    const qint64 x1 = qint64(std::floor(center.x() + radius));
    const qint64 y0 = qint64(std::floor(center.y() - radius));
    const qint64 y1 = qint64(std::floor(center.y() + radius));
    for (qint64 ix = x0; ix <= x1; ++ix) {
        for (qint64 iy = y0; iy <= y1; ++iy) {
            const auto it = m_trackCells.constFind(cellKey(ix, iy));
            if (it == m_trackCells.constEnd()) {
                continue;
            }
            for (const int index : it.value()) {
                const QPointF d = m_track[index] - center;
                if (QPointF::dotProduct(d, d) <= radius * radius) {
                    indices.append(index);
                }
            }
        }
    }
    std::sort(indices.begin(), indices.end());
    return indices;
}

double DiffDriveScene::coveragePercent() const {
    if (!qIsNaN(m_coverage)) {
        return m_coverage;
    }
    if (m_nextPoint >= 0 && !m_track.isEmpty()) {
        return 100.0 * m_nextPoint / m_track.size();
    }
    return 0.0;
}

double DiffDriveScene::elapsedSeconds() const {
    if (!qIsNaN(m_time)) {
        return m_time;
    }
    return m_lastPoseUs > m_firstPoseUs ? double(m_lastPoseUs - m_firstPoseUs) / 1e6 : 0.0;
}

void DiffDriveScene::paint(QPainter& painter, const SceneView& view) {
    painter.save();
    const bool antialiasing = antialiased();
    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing, antialiasing);
    painter.fillRect(QRectF(QPointF(0, 0), view.size), kBackground);

    paintTrack(painter, view);
    paintFloorDots(painter, view);
    paintCar(painter, view);
    paintMinimap(painter, view);

    paintCompass(painter, view);

    const double lineRadius = param(kLineRadius) * scale(view);
    paintCarFramePoints(painter, view, m_preview, kReference, 0.5 * lineRadius);
    paintCarFramePoints(painter, view, m_prediction, kPrediction, qMax(1.0, 0.25 * lineRadius));

    // HUD: one value per line, left-aligned in the top-left corner (the
    // pygame layout spread them around the screen and joined the controller
    // info with " | ").
    painter.setFont(hudFont(view));
    const double k = hudScale(view);
    const double left = 12.0 * k;
    double top = 8.0 * k;
    const double lineHeight = QFontMetricsF(painter.font()).height();
    const auto line = [&](const QString& text, const QColor& color) {
        drawHudText(painter, text, left, top, Anchor::Left, color);
        top += lineHeight;
    };
    if (!qIsNaN(m_qps) || !qIsNaN(m_solveMs)) {
        if (!m_controller.isEmpty()) {
            line(m_controller, kInfo);
        }
        if (!qIsNaN(m_qps)) {
            line(QStringLiteral("QPs: %1").arg(qRound(m_qps)), kInfo);
        }
        if (!qIsNaN(m_solveMs)) {
            line(QStringLiteral("solve: %1 ms").arg(m_solveMs, 0, 'f', 2), kInfo);
        }
    }
    line(QStringLiteral("fps: %1").arg(view.renderFps, 0, 'f', 1), kHud);
    const double coverage = coveragePercent();
    const double elapsed = elapsedSeconds();
    const double score = elapsed > 0.0 ? 100.0 * coverage / elapsed : 0.0;
    line(QCoreApplication::translate("DiffDriveScene", "score: %1").arg(score, 0, 'f', 2), kHud);
    line(QCoreApplication::translate("DiffDriveScene", "covered: %1%").arg(coverage, 0, 'f', 2),
         kHud);
    // The plant's own world metres (pygame showed metres + 50, y down).
    const DiffDrivePose current = pose();
    line(QStringLiteral("x: %1").arg(current.x, 0, 'f', 2), kHud);
    line(QStringLiteral("y: %1").arg(current.y, 0, 'f', 2), kHud);
    painter.restore();
}

void DiffDriveScene::paintTrack(QPainter& painter, const SceneView& view) const {
    if (m_track.isEmpty()) {
        return;
    }
    const DiffDrivePose current = pose();
    const double radius = param(kLineRadius) * scale(view);
    const QVector<int> near = trackPointsNear({current.x, current.y}, param(kRenderRadius));
    // One polyline per run of consecutive points, stroked as wide as the line
    // with round caps and joins: the same look as a dot stamped on every
    // point joined by strokes, in one call per run instead of two per point.
    // Reached points (index < next_point) first, gray, the rest over them.
    // The segment into next_point belongs to the reached run, as in index
    // order.
    const auto strokeRuns = [&](bool reached, const QColor& color) {
        painter.setPen(QPen(color, 2.0 * radius, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        QPolygonF run;
        const auto flush = [&] {
            if (run.size() == 1) {
                painter.drawPoint(run.first());
            } else if (run.size() > 1) {
                painter.drawPolyline(run);
            }
            run.clear();
        };
        for (int k = 0; k < near.size(); ++k) {
            const int index = near[k];
            if ((index < m_nextPoint) != reached) {
                // The first unreached point closes the reached run.
                if (reached && !run.isEmpty() && near[k - 1] == index - 1) {
                    run.append(worldToScreen(m_track[index], view));
                }
                flush();
                continue;
            }
            if (k > 0 && near[k - 1] != index - 1) {
                flush();
            }
            run.append(worldToScreen(m_track[index], view));
        }
        flush();
    };
    strokeRuns(true, kTrackReached);
    strokeRuns(false, kTrack);
}


void DiffDriveScene::paintFloorDots(QPainter& painter, const SceneView& view) const {
    const DiffDrivePose current = pose();
    const double radius = param(kRenderRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(kFloorDot);
    for (qint64 ix = qint64(std::ceil(current.x - radius));
         ix <= qint64(std::floor(current.x + radius)); ++ix) {
        for (qint64 iy = qint64(std::ceil(current.y - radius));
             iy <= qint64(std::floor(current.y + radius)); ++iy) {
            if (std::hypot(double(ix) - current.x, double(iy) - current.y) > radius ||
                m_blockedLatticeNodes.contains(cellKey(ix, iy))) {
                continue;
            }
            painter.drawEllipse(worldToScreen(QPointF(double(ix), double(iy)), view), 2.0, 2.0);
        }
    }
}

void DiffDriveScene::paintCar(QPainter& painter, const SceneView& view) const {
    const QPointF c = anchor(view);
    const double s = scale(view);
    const double r = param(kWheelDistance) * s;
    const QPolygonF triangle{c + QPointF(0.866 * r, 0.5 * r), c + QPointF(0.0, -r),
                             c + QPointF(-0.866 * r, 0.5 * r)};
    painter.setPen(Qt::NoPen);
    painter.setBrush(kCar);
    painter.drawPolygon(triangle);

    const double sensorY = c.y() - param(kSensorDistance) * s;
    const double halfLength = param(kSensorCount) * param(kSensorSpacing) * s / 2.0;
    painter.setPen(QPen(kSensor, 2.0, Qt::SolidLine, Qt::FlatCap));
    painter.drawLine(QPointF(c.x() - halfLength, sensorY), QPointF(c.x() + halfLength, sensorY));
}

void DiffDriveScene::paintMinimap(QPainter& painter, const SceneView& view) const {
    const double k = hudScale(view);
    // Bottom-left corner.
    const double halfWidth = 100.0 * k;
    const double halfHeight = 75.0 * k;
    const QPointF center(12.0 * k + halfWidth, view.size.height() - 12.0 * k - halfHeight);
    const QRectF rect(center.x() - halfWidth, center.y() - halfHeight, 2 * halfWidth,
                      2 * halfHeight);

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(kBackground);
    painter.drawRect(rect.adjusted(1, 1, -1, -1));
    painter.setClipRect(rect);

    // Fit the track's bounding box, one scale for both axes, y down.
    QPointF origin(0.0, 0.0);
    double factor = qMin(halfWidth, halfHeight) / kDefaultMinimapHalfExtent;
    if (!m_track.isEmpty()) {
        origin = m_trackBounds.center();
        const double extentX = qMax(m_trackBounds.width() / 2.0, 1e-3);
        const double extentY = qMax(m_trackBounds.height() / 2.0, 1e-3);
        factor = 0.9 * qMin(halfWidth / extentX, halfHeight / extentY);
    }
    const auto toMap = [&](const QPointF& p) { return center + (p - origin) * factor; };

    painter.setBrush(kTrack);
    const int step = qMax(1, qRound(param(kScale) / 10.0));
    for (int i = 0; i < m_track.size(); i += step) {
        painter.drawEllipse(toMap(m_track[i]), 1.0, 1.0);
    }
    const DiffDrivePose current = pose();
    painter.setBrush(kReference);
    painter.drawEllipse(toMap({current.x, current.y}), 5.0 * k, 5.0 * k);
    painter.restore();
}

void DiffDriveScene::paintCompass(QPainter& painter, const SceneView& view) const {
    const double k = hudScale(view);
    // Bottom-right corner; 52 px reaches the N/E/S/W labels, plus a margin.
    const double radius = 40.0 * k;
    const double reach = 52.0 * k + 20.0 * k;
    const QPointF center(view.size.width() - reach, view.size.height() - reach);
    // Points up at heading 0 and turns clockwise as the car turns right
    // (screen y down, so the screen angle grows clockwise too).
    const double a = pose().theta - M_PI / 2.0;

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 2.0));
    painter.drawEllipse(center, radius, radius);
    QPolygonF star;
    for (int i = 0; i < 8; ++i) {
        const double angle = a + i * M_PI / 4.0;
        const double r = (i % 2 == 0) ? radius : 16.0 * k;
        star.append(center + QPointF(std::cos(angle), std::sin(angle)) * r);
    }
    painter.drawPolygon(star);
    painter.setPen(QPen(kCompassPointer, 3.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(center, center + QPointF(std::cos(a), std::sin(a)) * radius);

    QFont font = painter.font();
    font.setFamily(QStringLiteral("Arial"));
    font.setStyleHint(QFont::SansSerif);
    font.setBold(true);
    font.setPixelSize(qMax(8, qRound(17.0 * k)));
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const double offset = 52.0 * k;
    const struct {
        const char* label;
        QPointF direction;
        QColor color;
    } labels[] = {{"N", {0, -1}, kCompassNorth},
                  {"E", {1, 0}, Qt::black},
                  {"S", {0, 1}, Qt::black},
                  {"W", {-1, 0}, Qt::black}};
    for (const auto& label : labels) {
        const QString text = QString::fromLatin1(label.label);
        const QPointF at = center + label.direction * offset;
        painter.setPen(label.color);
        painter.drawText(QPointF(at.x() - metrics.horizontalAdvance(text) / 2.0,
                                 at.y() + (metrics.ascent() - metrics.descent()) / 2.0),
                         text);
    }
    painter.restore();
}

void DiffDriveScene::paintCarFramePoints(QPainter& painter, const SceneView& view,
                                         const CarFramePoints& points, const QColor& color,
                                         double radius) const {
    if (points.points.isEmpty()) {
        return;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const QPointF& point : points.points) {
        painter.drawEllipse(carPointToScreen(point, points.tick, view), radius, radius);
    }
}

}  // namespace traceview
