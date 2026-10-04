#include "trackenvironment.h"

#include <QCoreApplication>
#include <QLocale>
#include <cmath>
#include <limits>

#include "lab/benchmarks/linefollower/plant/lfsensors.h"
#include "lab/benchmarks/linefollower/plant/trackprogress.h"
#include "lab/code/codehostapi.h"
#include "lab/plant/codeplant.h"

namespace tracelab::lf {

namespace {

constexpr double kHalfPi = 1.57079632679489661923;
// sim.track must fit one BTP message (lfplantdevice.cpp).
constexpr int kMaxTrackDocumentPoints = 2400;
constexpr int kMaxPreview = 256;

QString tr(const char* text) {
    return QCoreApplication::translate("TrackEnvironment", text);
}

const TrackEnvironment* current() {
    code::CodeContext* context = code::currentContext();
    return context ? static_cast<const TrackEnvironment*>(context->environment) : nullptr;
}

extern "C" {

double tl_track_distance(double x, double y) {
    const TrackEnvironment* environment = current();
    return environment ? environment->distance(x, y) : std::numeric_limits<double>::infinity();
}

double tl_track_line_half_width() {
    const TrackEnvironment* environment = current();
    return environment ? environment->lineHalfWidth() : 0.0;
}

}  // extern "C"

}  // namespace

TrackEnvironment::TrackEnvironment() = default;
TrackEnvironment::~TrackEnvironment() = default;

QString TrackEnvironment::id() const {
    return QString::fromLatin1(kId);
}

QString TrackEnvironment::name() const {
    return tr("Line-follower track");
}

QVector<code::HostSymbol> TrackEnvironment::symbols() const {
    return {
        {"tl_track_distance", reinterpret_cast<const void*>(&tl_track_distance)},
        {"tl_track_line_half_width", reinterpret_cast<const void*>(&tl_track_line_half_width)},
    };
}

QStringList TrackEnvironment::requiredStates() const {
    return {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("theta")};
}

QVector<PlantSetting> TrackEnvironment::settingsSchema() const {
    QVector<PlantSetting> list;
    PlantSetting seed;
    seed.key = QStringLiteral("seed");
    seed.label = tr("Track seed");
    seed.toolTip = tr("-1 draws a random track on every reset.");
    seed.type = PlantSetting::Type::Int;
    seed.value = int(qBound(qint64(-1), qint64(m_trackParams.seed), qint64(2147483647)));
    seed.minimum = -1;
    seed.maximum = 2147483647;
    list.append(seed);
    PlantSetting type;
    type.key = QStringLiteral("trackType");
    type.label = tr("Track type");
    type.type = PlantSetting::Type::Enum;
    type.options = {tr("Lemniscate"), tr("Circle")};
    type.value = m_trackParams.type == TrackType::Circle ? 1 : 0;
    list.append(type);
    PlantSetting horizon;
    horizon.key = QStringLiteral("horizon");
    horizon.label = tr("Preview points");
    horizon.toolTip = tr("How many future track points sim.preview carries (TL_PREVIEW).");
    horizon.type = PlantSetting::Type::Int;
    horizon.value = m_horizon;
    horizon.minimum = 2;
    horizon.maximum = kMaxPreview;
    list.append(horizon);
    PlantSetting spacing;
    spacing.key = QStringLiteral("futureSpacing");
    spacing.label = tr("Preview spacing");
    spacing.unit = tr(" points");
    spacing.toolTip = tr("Track points between two preview points (about 1 cm each).");
    spacing.type = PlantSetting::Type::Int;
    spacing.value = m_futureSpacing;
    spacing.minimum = 1;
    spacing.maximum = 100;
    list.append(spacing);
    return list;
}

void TrackEnvironment::applySettings(const QVariantMap& values) {
    if (values.contains(QStringLiteral("seed"))) {
        m_trackParams.seed = values.value(QStringLiteral("seed")).toLongLong();
    }
    if (values.contains(QStringLiteral("trackType"))) {
        m_trackParams.type = values.value(QStringLiteral("trackType")).toInt() == 1
                                 ? TrackType::Circle
                                 : TrackType::Lemniscate;
    }
    if (values.contains(QStringLiteral("horizon"))) {
        m_horizon = qBound(2, values.value(QStringLiteral("horizon")).toInt(), kMaxPreview);
    }
    if (values.contains(QStringLiteral("futureSpacing"))) {
        m_futureSpacing = qMax(1, values.value(QStringLiteral("futureSpacing")).toInt());
    }
}

QJsonObject TrackEnvironment::config() const {
    return {
        {"seed", qint64(m_trackParams.seed)},
        {"trackType", m_trackParams.type == TrackType::Circle ? "circle" : "lemniscate"},
        {"horizon", m_horizon},
        {"futureSpacing", m_futureSpacing},
    };
}

void TrackEnvironment::setConfig(const QJsonObject& config) {
    m_trackParams.seed = config.value(QStringLiteral("seed")).toInteger(m_trackParams.seed);
    m_trackParams.type = config.value(QStringLiteral("trackType")).toString() ==
                                 QLatin1String("circle")
                             ? TrackType::Circle
                             : TrackType::Lemniscate;
    m_horizon = qBound(2, config.value(QStringLiteral("horizon")).toInt(m_horizon), kMaxPreview);
    m_futureSpacing = qMax(1, config.value(QStringLiteral("futureSpacing")).toInt(m_futureSpacing));
}

void TrackEnvironment::reset(double ts, QHash<QString, double>* initialStates) {
    m_ts = ts;
    // A new track when its settings changed (or a random one is asked for).
    if (!m_hasTrack || m_builtSeed != m_trackParams.seed || m_builtType != m_trackParams.type ||
        m_trackParams.seed < 0) {
        m_track = generateTrack(m_trackParams);
        m_hasTrack = true;
        m_builtSeed = m_trackParams.seed;
        m_builtType = m_trackParams.type;
        // The line's geometry for tl_track_distance(): the native array's
        // own segment grid (lfsensors.h), its defaults' margins.
        m_line = std::make_unique<LineSensorArray>(m_track.x, m_track.y, LineArrayParams{});
    }
    m_stride = qMax(1, int((m_track.size() + kMaxTrackDocumentPoints - 1) /
                           kMaxTrackDocumentPoints));
    TrackProgressParams progress;
    progress.futureCount = m_horizon;
    progress.futureSpacing = m_futureSpacing;
    m_progress = std::make_unique<TrackProgress>(m_track.x, m_track.y, progress);
    m_progress->reset();
    // The start: the first track point, heading -pi/2 (LineFollowerPlant).
    m_pose = {};
    if (m_track.size() > 0) {
        m_pose = {m_track.x[0], m_track.y[0], -kHalfPi};
    }
    m_previous = m_pose;
    m_steps = 0;
    initialStates->insert(QStringLiteral("x"), m_pose.x);
    initialStates->insert(QStringLiteral("y"), m_pose.y);
    initialStates->insert(QStringLiteral("theta"), m_pose.theta);
}

void TrackEnvironment::afterStep(const CodePlant& plant) {
    m_previous = m_pose;
    m_pose = {plant.state(QStringLiteral("x")), plant.state(QStringLiteral("y")),
              plant.state(QStringLiteral("theta"))};
    ++m_steps;
    if (m_progress) {
        m_progress->update(m_pose);
    }
}

bool TrackEnvironment::finished() const {
    return m_progress && m_progress->finished();
}

QVector<double> TrackEnvironment::disturbances(double value) const {
    return {value, -value};
}

int TrackEnvironment::previewCapacity() const {
    return kMaxPreview;
}

void TrackEnvironment::preview(QVector<double>* x, QVector<double>* y) const {
    x->clear();
    y->clear();
    if (!m_progress) {
        return;
    }
    for (const CarFramePoint& p : m_progress->futurePoints()) {
        x->append(p.x);
        y->append(p.y);
    }
}

QVector<QPair<QString, QString>> TrackEnvironment::truthFields() const {
    return {{QStringLiteral("x"), QStringLiteral("m")},
            {QStringLiteral("y"), QStringLiteral("m")},
            {QStringLiteral("theta"), QStringLiteral("rad")},
            {QStringLiteral("next_point"), QString()},
            {QStringLiteral("coverage"), QStringLiteral("%")},
            {QStringLiteral("lateral_error"), QStringLiteral("m")},
            {QStringLiteral("speed"), QStringLiteral("m/s")},
            {QStringLiteral("omega"), QStringLiteral("rad/s")}};
}

QVector<double> TrackEnvironment::truthValues(const CodePlant& plant) const {
    Q_UNUSED(plant);
    // Speed and yaw rate of the last step, from the pose (the plant's own
    // states may be anything): the displacement along the old heading.
    const double dx = m_pose.x - m_previous.x;
    const double dy = m_pose.y - m_previous.y;
    const double speed = m_steps == 0 ? 0.0
                                      : (dx * std::cos(m_previous.theta) +
                                         dy * std::sin(m_previous.theta)) / m_ts;
    const double omega = m_steps == 0 ? 0.0 : (m_pose.theta - m_previous.theta) / m_ts;
    return {m_pose.x,
            m_pose.y,
            m_pose.theta,
            m_progress ? double(m_progress->nextPoint() / m_stride) : 0.0,
            m_progress ? qBound(0.0, m_progress->coverage(), 100.0) : 0.0,
            m_progress && m_steps > 0 ? m_progress->lateralError() : 0.0,
            speed,
            omega};
}

QByteArray TrackEnvironment::documentTopic() const {
    return QByteArrayLiteral("sim.track");
}

QByteArray TrackEnvironment::document() const {
    // Compact JSON (lfplantdevice.cpp): 0.1 mm is plenty.
    QByteArray body;
    body.reserve(int(m_track.size() / std::size_t(m_stride)) * 20 + 64);
    body += "{\"points\":[";
    for (std::size_t i = 0; i < m_track.size(); i += std::size_t(m_stride)) {
        if (i != 0) {
            body += ',';
        }
        body += '[';
        body += QByteArray::number(m_track.x[i], 'f', 4);
        body += ',';
        body += QByteArray::number(m_track.y[i], 'f', 4);
        body += ']';
    }
    body += "],\"closed\":true,\"stride\":";
    body += QByteArray::number(m_stride);
    body += '}';
    return body;
}

QVector<QPair<QString, QString>> TrackEnvironment::statusItems() const {
    return {{tr("covered"),
             tr("%1 %").arg(QLocale().toString(m_progress ? m_progress->coverage() : 0.0, 'f', 1))}};
}

double TrackEnvironment::distance(double x, double y) const {
    return m_line ? m_line->distanceToLine({x, y}) : std::numeric_limits<double>::infinity();
}

double TrackEnvironment::lineHalfWidth() const {
    return LineArrayParams{}.lineHalfWidth;
}

void registerTrackEnvironment() {
    PlantEnvironmentRegistry::instance().registerEnvironment(
        {QString::fromLatin1(TrackEnvironment::kId), tr("Line-follower track"),
         [] { return std::make_unique<TrackEnvironment>(); }});
}

}  // namespace tracelab::lf
