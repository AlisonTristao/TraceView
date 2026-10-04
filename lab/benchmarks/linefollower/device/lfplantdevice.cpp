#include "dashboard/perfprobe.h"
#include "lfplantdevice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QTimer>
#include <btp/profiles/loop.hpp>
#include <btp/telemetry.hpp>
#include <cmath>

#include "lab/plant/btpproducerport.h"
#include "lab/plant/loopbacktransport.h"
#include "lab/plant/plantmanifest.h"

namespace tracelab::lf {

namespace loopp = btp::profiles::loop;

namespace {

// sim.track must fit one logical BTP message: btp::Endpoint cuts frames at
// the ESP-NOW size whatever the link (250 octets, 210 of payload) and a
// message has at most 255 fragments, so ~53 KB. ~17 bytes a point.
constexpr int kMaxTrackDocumentPoints = 2400;
// How long tick k+1 waits for the plan answering tick k before going on
// with the held input (a controller that stopped answering must not freeze
// the plant). Every run mode waits: running ticks on a stale plan because the
// controller is slower than the clock (a Debug qLPV MPC takes over a second
// a step) made the car leave the track. A slow controller slows the
// simulation down instead.
constexpr qint64 kPlanTimeoutMs = 10000;
// Real time: further behind than this and the clock restarts from here (slow
// motion) instead of catching up in a burst.
constexpr double kMaxBacklogSeconds = 0.25;

QString runModeName(RunMode mode) {
    switch (mode) {
        case RunMode::AsFastAsPossible:
            return QStringLiteral("fast");
        case RunMode::Lockstep:
            return QStringLiteral("lockstep");
        case RunMode::RealTime:
            break;
    }
    return QStringLiteral("realtime");
}

RunMode runModeFromName(const QString& name, RunMode fallback) {
    if (name == QLatin1String("fast")) {
        return RunMode::AsFastAsPossible;
    }
    if (name == QLatin1String("lockstep")) {
        return RunMode::Lockstep;
    }
    if (name == QLatin1String("realtime")) {
        return RunMode::RealTime;
    }
    return fallback;
}

// Reads an action's PACKED_LE parameters into one double per element, by
// field name (arrays whole).
struct Params {
    QHash<QString, std::vector<double>> values;
    bool ok = false;

    double scalar(const char* name, double fallback = 0.0) const {
        const auto it = values.constFind(QLatin1String(name));
        return it == values.constEnd() || it->empty() ? fallback : it->front();
    }
    bool has(const char* name) const {
        return values.contains(QLatin1String(name));
    }
};

Params decodeParams(const btp::Catalog& catalog, quint16 actionId, btp::ByteView parameters) {
    Params out;
    const btp::CatalogAction* action = catalog.action(actionId, 1);
    if (action == nullptr) {
        return out;
    }
    if (action->parameter_count == 0) {
        out.ok = true;
        return out;
    }
    btp::SampleReader reader(parameters.data, parameters.size, action->fields,
                             action->parameter_count, btp::kEncodingPackedLe,
                             btp::SampleLayout::BodyOnly);
    btp::SampleValue value{};
    std::size_t index = 0;
    while (reader.next(&value) == btp::SampleStep::Item) {
        std::vector<double> elements;
        if (!value.is_null) {
            for (std::size_t i = 0; i < value.count; ++i) {
                elements.push_back(value.f64(i));
            }
        }
        out.values.insert(QString::fromUtf8(catalog.field_name(*action, index)), elements);
        ++index;
    }
    out.ok = reader.finish() == btp::MessageError::Ok;
    return out;
}

void reject(btp::NodeActionOutcome* outcome, btp::ResultError error, const char* message) {
    outcome->status = static_cast<std::uint8_t>(btp::ResultStatus::Rejected);
    outcome->error_code = static_cast<std::uint16_t>(error);
    outcome->message = message;
}

}  // namespace

PlantSettings PlantSettings::fromJson(const QJsonObject& json) {
    PlantSettings s;
    s.track.seed = json.value("seed").toInteger(s.track.seed);
    s.track.type = json.value("trackType").toString() == QLatin1String("circle")
                       ? TrackType::Circle
                       : TrackType::Lemniscate;
    s.fps = qBound(1, json.value("fps").toInt(s.fps), 1000);
    s.horizon = qBound(2, json.value("horizon").toInt(s.horizon), kMaxPreview);
    s.futureSpacing = qMax(1, json.value("futureSpacing").toInt(s.futureSpacing));
    s.sensorNoise = json.value("sensorNoise").toBool(s.sensorNoise);
    s.sensorSeed = quint64(json.value("sensorSeed").toInteger(qint64(s.sensorSeed)));
    s.runMode = runModeFromName(json.value("runMode").toString(), s.runMode);
    s.timeScale = qBound(0.01, json.value("timeScale").toDouble(s.timeScale), 100.0);
    s.autoRun = json.value("autoRun").toBool(s.autoRun);
    return s;
}

QJsonObject PlantSettings::toJson() const {
    return QJsonObject{
        {"seed", qint64(track.seed)},
        {"trackType", track.type == TrackType::Circle ? "circle" : "lemniscate"},
        {"fps", fps},
        {"horizon", horizon},
        {"futureSpacing", futureSpacing},
        {"sensorNoise", sensorNoise},
        {"sensorSeed", qint64(sensorSeed)},
        {"runMode", runModeName(runMode)},
        {"timeScale", timeScale},
        {"autoRun", autoRun},
    };
}

PlantParams PlantSettings::plantParams() const {
    // The same as the harness's LineFollowerRunner: Ts = 1 / fps, N future
    // points, the sensors seeded apart from the track.
    PlantParams params;
    params.car.ts = 1.0 / fps;
    params.progress.futureCount = horizon;
    params.progress.futureSpacing = futureSpacing;
    if (sensorNoise) {
        params.sensors = noisySensorParams(params.sensors);
    }
    params.sensors.seed = sensorSeed;
    return params;
}

LinePlantDevice::LinePlantDevice(quint32 sourceId, const PlantSettings& settings, QObject* parent)
    : LabPlantDevice(parent), m_sourceId(sourceId), m_settings(settings) {
    buildPlantCatalog(m_catalog);
    m_catalog.set_config_revision_auto();
    rebuildPlant();

    // A fresh boot_id per device instance, like a robot's per-boot counter.
    quint32 bootId = quint32(QDateTime::currentMSecsSinceEpoch() & 0x7fffffff) | 1U;
    static const QByteArray kName = QByteArrayLiteral("TraceLab: Line follower");
    m_viewer = new BtpProducerPort(&m_catalog, sourceId, bootId, kName, this);
    m_controller = new BtpProducerPort(&m_catalog, sourceId, bootId, kName, this);
    for (BtpProducerPort* port : {m_viewer, m_controller}) {
        port->setCommandHandler([this, port](quint16 actionId, btp::ByteView parameters,
                                             btp::NodeActionOutcome* outcome, bool stream) {
            onCommand(port, actionId, parameters, outcome, stream);
        });
    }
    connect(m_viewer, &BtpProducerPort::subscriptionsChanged, this, [this] {
        publishTrack();
        publishParams();
        publishTruth(m_viewer, false, nextTimestampUs());
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &LinePlantDevice::pump);
}

LinePlantDevice::~LinePlantDevice() = default;

double LinePlantDevice::perturbation() const {
    return m_plant->perturbation();
}

QString LinePlantDevice::typeName() const {
    return QCoreApplication::translate("LinePlantDevice", "Line follower");
}

double LinePlantDevice::time() const {
    return m_plant->time();
}

void LinePlantDevice::setSampleRate(int hz) {
    hz = qBound(1, hz, 1000);
    if (hz == m_settings.fps) {
        return;
    }
    PlantSettings s = m_settings;
    s.fps = hz;
    setSettings(s);
    reset();
}

QVector<PlantSetting> LinePlantDevice::settingsSchema() const {
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("LinePlantDevice", text);
    };
    QVector<PlantSetting> list;
    PlantSetting seed;
    seed.key = QStringLiteral("seed");
    seed.label = tr("Track seed");
    seed.toolTip = tr("-1 draws a random track on every reset.");
    seed.type = PlantSetting::Type::Int;
    seed.value = int(qBound(qint64(-1), qint64(m_settings.track.seed), qint64(2147483647)));
    seed.minimum = -1;
    seed.maximum = 2147483647;
    list.append(seed);
    PlantSetting type;
    type.key = QStringLiteral("trackType");
    type.label = tr("Track type");
    type.type = PlantSetting::Type::Enum;
    type.options = {tr("Lemniscate"), tr("Circle")};
    type.value = m_settings.track.type == TrackType::Circle ? 1 : 0;
    list.append(type);
    PlantSetting noise;
    noise.key = QStringLiteral("sensorNoise");
    noise.label = tr("Sensor noise");
    noise.toolTip = tr("Synthetic noise on the gyro and the line sensors (the encoders always "
                       "count whole ticks).");
    noise.type = PlantSetting::Type::Bool;
    noise.value = m_settings.sensorNoise;
    list.append(noise);
    PlantSetting horizon;
    horizon.key = QStringLiteral("horizon");
    horizon.label = tr("Preview points");
    horizon.toolTip = tr("How many future track points sim.preview carries (TL_PREVIEW).");
    horizon.type = PlantSetting::Type::Int;
    horizon.value = m_settings.horizon;
    horizon.minimum = 2;
    horizon.maximum = kMaxPreview;
    list.append(horizon);
    PlantSetting spacing;
    spacing.key = QStringLiteral("futureSpacing");
    spacing.label = tr("Preview spacing");
    spacing.unit = tr(" points");
    spacing.toolTip = tr("Track points between two preview points (about 1 cm each).");
    spacing.type = PlantSetting::Type::Int;
    spacing.value = m_settings.futureSpacing;
    spacing.minimum = 1;
    spacing.maximum = 100;
    list.append(spacing);
    return list;
}

void LinePlantDevice::applySettings(const QVariantMap& values) {
    PlantSettings s = m_settings;
    if (values.contains(QStringLiteral("seed"))) {
        s.track.seed = values.value(QStringLiteral("seed")).toLongLong();
    }
    if (values.contains(QStringLiteral("trackType"))) {
        s.track.type = values.value(QStringLiteral("trackType")).toInt() == 1
                           ? TrackType::Circle
                           : TrackType::Lemniscate;
    }
    if (values.contains(QStringLiteral("sensorNoise"))) {
        s.sensorNoise = values.value(QStringLiteral("sensorNoise")).toBool();
    }
    if (values.contains(QStringLiteral("horizon"))) {
        s.horizon = qBound(2, values.value(QStringLiteral("horizon")).toInt(), kMaxPreview);
    }
    if (values.contains(QStringLiteral("futureSpacing"))) {
        s.futureSpacing = qMax(1, values.value(QStringLiteral("futureSpacing")).toInt());
    }
    setSettings(s);
    reset();
}

QVector<QPair<QString, QString>> LinePlantDevice::statusItems() const {
    const QLocale locale;
    QVector<QPair<QString, QString>> items;
    items.append({QCoreApplication::translate("LinePlantDevice", "covered"),
                  QCoreApplication::translate("LinePlantDevice", "%1 %")
                      .arg(locale.toString(m_plant->progress().coverage(), 'f', 1))});
    if (perturbation() != 0.0) {
        items.append({QCoreApplication::translate("LinePlantDevice", "perturbation"),
                      QCoreApplication::translate("LinePlantDevice", "%1 %")
                          .arg(locale.toString(perturbation(), 'f', 1))});
    }
    return items;
}

void LinePlantDevice::setSettings(const PlantSettings& settings) {
    m_settings = settings;
    setRunMode(settings.runMode, settings.timeScale);
    publishParams();
    emit settingsChanged();
}

void LinePlantDevice::setRunMode(RunMode mode, double timeScale) {
    m_settings.runMode = mode;
    m_settings.timeScale = qBound(0.01, timeScale, 100.0);
    m_clock.restart();
    m_ticksAtClockStart = double(m_tick);
    publishParams();
    if (m_running) {
        schedule();
    }
    emit settingsChanged();
}

void LinePlantDevice::attachViewer(LoopbackTransport* peer) {
    if (m_viewerLink && m_viewerLink != peer) {
        m_viewerLink->close();
    }
    m_viewerLink = peer;
    if (peer) {
        peer->setParent(this);
    }
    m_viewer->setTransport(peer);
}

void LinePlantDevice::attachController(LoopbackTransport* peer) {
    if (m_controllerLink && m_controllerLink != peer) {
        m_controllerLink->close();
    }
    m_controllerLink = peer;
    if (peer) {
        peer->setParent(this);
    }
    m_controller->setTransport(peer);
}

void LinePlantDevice::rebuildPlant() {
    const double perturbation = m_plant ? m_plant->perturbation() : 0.0;
    const bool newTrack = !m_plant || m_trackSeed != m_settings.track.seed ||
                          m_trackType != m_settings.track.type ||
                          m_plant->params().car.ts != 1.0 / m_settings.fps;
    Track track = newTrack ? generateTrack(m_settings.track) : m_plant->track();
    m_trackSeed = m_settings.track.seed;
    m_trackType = m_settings.track.type;
    m_trackStride = qMax(1, int((track.size() + kMaxTrackDocumentPoints - 1) /
                                kMaxTrackDocumentPoints));
    m_plant = std::make_unique<LineFollowerPlant>(std::move(track), m_settings.plantParams());
    m_plant->setPerturbation(perturbation);
    m_tick = 0;
    m_finished = false;
    m_plan = Plan{};
    m_applied = {0.0, 0.0};
    m_appliedRefTick = -1;
    m_appliedSource = loopp::kSourceNone;
}

void LinePlantDevice::run() {
    if (m_running || m_finished) {
        return;
    }
    m_running = true;
    m_clock.restart();
    m_ticksAtClockStart = double(m_tick);
    m_waitClock.restart();
    emit runningChanged(true);
    schedule();
}

void LinePlantDevice::pause() {
    if (!m_running) {
        return;
    }
    m_running = false;
    m_timer->stop();
    emit runningChanged(false);
}

void LinePlantDevice::reset() {
    rebuildPlant();
    m_clock.restart();
    m_ticksAtClockStart = 0.0;
    publishTrack();
    publishParams();
    publishTruth(m_viewer, false, nextTimestampUs());
    emit ticked(0);
    if (m_running) {
        schedule();
    }
}

void LinePlantDevice::step() {
    if (m_running) {
        return;
    }
    tickOnce();
}

void LinePlantDevice::setPerturbation(double percent) {
    m_plant->setPerturbation(qBound(-100.0, percent, 100.0));
}

bool LinePlantDevice::waitingForPlan() const {
    // Real time and as fast as possible wait only once the controller has
    // answered at all: before its first plan (still connecting, subscribing,
    // waiting for its first preview) the plant runs on the held input instead
    // of stalling for the timeout. Lockstep always waits (step for step
    // reproducible).
    const bool answered = m_plan.refTick >= 0 || m_settings.runMode == RunMode::Lockstep;
    return m_mode == loopp::kModeRemote && m_tick >= 1 && answered &&
           m_plan.refTick < m_tick && m_controller->subscribed(topic::kLoopState) && m_waitClock.elapsed() < kPlanTimeoutMs;
}

int LinePlantDevice::planTimeoutLeftMs() const {
    return int(qMax<qint64>(1, kPlanTimeoutMs - m_waitClock.elapsed()));
}

static qint64 g_timerStartNs = 0;  // TEMP perf
static int g_timerAskMs = -1;
void LinePlantDevice::schedule() {
    if (!m_running) {
        return;
    }
    struct Note {
        QTimer* t;
        ~Note() {
            if (t->isActive()) {
                g_timerStartNs = perf::nowNs();
                g_timerAskMs = t->interval();
                perf::count(g_timerAskMs == 0 ? "plant.timer0" : g_timerAskMs > 100 ? "plant.timerWaitPlan" : "plant.timerN");
            }
        }
    } note{m_timer};
    switch (m_settings.runMode) {
        case RunMode::RealTime: {
            const double fps = m_settings.fps * m_settings.timeScale;
            // Tick k is due once `due` reaches k (pump() checks the same):
            // one period after the last tick, not two.
            const double due = m_ticksAtClockStart + m_clock.nsecsElapsed() * 1e-9 * fps;
            if (double(m_tick) + 1.0 <= due) {
                m_timer->start(waitingForPlan() ? planTimeoutLeftMs() : 0);
            } else {
                const double waitMs = (double(m_tick) + 1.0 - due) * 1000.0 / fps;
                m_timer->start(qMax(0, int(std::ceil(waitMs))));
            }
            break;
        }
        case RunMode::AsFastAsPossible:
        case RunMode::Lockstep:
            // Waiting for the plan: its arrival reschedules at once
            // (onCommand), the timer only covers the timeout. Polling here
            // ran at the Windows timer resolution (~15.6 ms, not 1 ms) and
            // held the simulation to about half of real time.
            m_timer->start(waitingForPlan() ? planTimeoutLeftMs() : 0);
            break;
    }
}

void LinePlantDevice::pump() {
    perf::count("plant.pump");
    if (g_timerAskMs > 0 && g_timerAskMs <= 100) {
        perf::add("plant.timerLate", perf::nowNs() - g_timerStartNs - qint64(g_timerAskMs) * 1'000'000);
    }
    g_timerAskMs = -1;
    if (!m_running) {
        return;
    }
    if (m_settings.runMode == RunMode::RealTime) {
        const double fps = m_settings.fps * m_settings.timeScale;
        const double due = m_ticksAtClockStart + m_clock.nsecsElapsed() * 1e-9 * fps;
        if (double(m_tick) + 1.0 > due) {
            schedule();
            return;
        }
        if (due - double(m_tick) > kMaxBacklogSeconds * fps) {
            m_clock.restart();
            m_ticksAtClockStart = double(m_tick);
        }
        if (waitingForPlan()) {
            schedule();
            return;
        }
    } else if (waitingForPlan()) {
        schedule();
        return;
    }
    if (tickOnce()) {
        schedule();
    }
}

bool LinePlantDevice::tickOnce() {
    PERF_SCOPE("plant.tick");
    {
        static qint64 last = 0;
        const qint64 now = perf::nowNs();
        if (last != 0) {
            perf::add("plant.gap", now - last);
            const qint64 g = (now - last) / 1'000'000;
            perf::count(g < 3 ? "plant.gap[0,3)" : g < 8 ? "plant.gap[3,8)" : g < 11 ? "plant.gap[8,11)"
                        : g < 14 ? "plant.gap[11,14)" : g < 20 ? "plant.gap[14,20)" : "plant.gap[20+)");
        }
        last = now;
    }
    if (m_finished) {
        return false;
    }
    const long long k = m_tick + 1;
    if (m_mode == loopp::kModeRemote) {
        if (m_plan.refTick >= 0) {
            const long long j = k - m_plan.refTick - 1;
            if (j >= 0 && j < (long long)m_plan.uLeft.size()) {
                m_applied = {m_plan.uLeft[std::size_t(j)], m_plan.uRight[std::size_t(j)]};
                m_appliedRefTick = m_plan.refTick;
            }
            // No element for this tick: the last applied input is held.
            m_appliedSource = loopp::kSourceRemote;
        } else {
            m_appliedSource = loopp::kSourceNone;
        }
    } else {
        m_applied = {0.0, 0.0};
        m_appliedSource = loopp::kSourceNone;
    }
    if (!m_plant->step(m_applied[0], m_applied[1])) {
        m_finished = true;
        pause();
        emit finished();
        return false;
    }
    m_tick = m_plant->tick();
    m_waitClock.restart();
    publishTick();
    emit ticked(m_tick);
    return true;
}

quint64 LinePlantDevice::nextTimestampUs() {
    // Wall clock, strictly increasing: a widget groups the fields of one
    // sample by (topic, timestamp), so two ticks in one microsecond must not
    // share one.
    const quint64 now = quint64(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    m_lastTimestampUs = qMax(now, m_lastTimestampUs + 1);
    return m_lastTimestampUs;
}

void LinePlantDevice::publishTick() {
    PERF_SCOPE("plant.publish");
    const quint64 us = nextTimestampUs();
    const StepData& data = m_plant->data();
    const SensorReadings& measured = m_plant->measured();
    const quint32 tick = quint32(m_tick);

    std::vector<double> px;
    std::vector<double> py;
    px.reserve(data.futurePoints.size());
    py.reserve(data.futurePoints.size());
    for (const CarFramePoint& p : data.futurePoints) {
        px.push_back(p.x);
        py.push_back(p.y);
    }
    const auto preview = [&](btp::SampleWriter& w) {
        w.put_f64(tick);
        w.put_array_f64(px.data(), qMin<std::size_t>(px.size(), kMaxPreview));
        w.put_array_f64(py.data(), qMin<std::size_t>(py.size(), kMaxPreview));
    };
    const auto state = [&](btp::SampleWriter& w) {
        w.put_f64(tick);
        w.put_u64(m_mode);
        w.put_f64(measured.wheelSpeedLeft);
        w.put_f64(measured.wheelSpeedRight);
        w.put_f64(measured.gyro);
        w.put_array_f64(measured.line.data(), qMin<std::size_t>(measured.line.size(),
                                                                 std::size_t(kLineSensors)));
    };
    const auto output = [&](btp::SampleWriter& w) {
        w.put_f64(m_appliedRefTick < 0 ? 0.0 : double(m_appliedRefTick));
        w.put_u64(m_appliedSource);
        w.put_f64(m_applied[0]);
        w.put_f64(m_applied[1]);
    };
    const Pose& pose = m_plant->pose();
    const CarDynamics& car = m_plant->car();
    const auto states = [&](btp::SampleWriter& w) {
        const auto wheels = car.wheelsSpeed();
        w.put_f64(tick);
        w.put_f64(wheels.first);
        w.put_f64(wheels.second);
        w.put_f64(car.omega());
        w.put_f64(pose.x);
        w.put_f64(pose.y);
        w.put_f64(pose.theta);
        w.put_f64(m_plant->perturbation());
        w.put_f64(measured.wheelSpeedLeft);
        w.put_f64(measured.wheelSpeedRight);
    };

    // The controller first (it gets every tick, the preview before the
    // state), then the viewer at its own rates.
    m_controller->publish(topic::kSimPreview, preview, us);
    perf::stateSentNs = perf::nowNs();
    m_controller->publish(topic::kLoopState, state, us);
    m_controller->publish(topic::kLoopOutput, output, us);
    m_viewer->publishDue(topic::kSimPreview, preview, us);
    m_viewer->publishDue(topic::kLoopState, state, us);
    m_viewer->publishDue(topic::kLoopOutput, output, us);
    m_viewer->publishDue(topic::kSimStates, states, us);
    publishTruth(m_viewer, true, us);
}

void LinePlantDevice::publishTruth(BtpProducerPort* port, bool due, quint64 us) {
    const Pose& pose = m_plant->pose();
    const StepData& data = m_plant->data();
    const TrackProgress& progress = m_plant->progress();
    const quint32 tick = quint32(m_tick);
    const auto truth = [&](btp::SampleWriter& w) {
        w.put_f64(tick);
        w.put_f64(m_plant->time());
        w.put_f64(pose.x);
        w.put_f64(pose.y);
        w.put_f64(pose.theta);
        w.put_f64(double(progress.nextPoint() / m_trackStride));
        w.put_f64(qBound(0.0, progress.coverage(), 100.0));
        w.put_f64(m_tick == 0 ? 0.0 : data.lateralError);
        w.put_f64(m_tick == 0 ? 0.0 : data.speed);
        w.put_f64(m_tick == 0 ? 0.0 : data.omega);
    };
    if (due) {
        port->publishDue(topic::kSimTruth, truth, us);
    } else {
        port->publish(topic::kSimTruth, truth, us);
    }
}

void LinePlantDevice::publishParams() {
    if (!m_viewer) {
        return;
    }
    const auto params = [&](btp::SampleWriter& w) {
        w.put_f64(1.0 / m_settings.fps);
        w.put_i64(m_settings.track.seed);
        w.put_u64(quint64(m_settings.track.type));
        w.put_bool(m_settings.sensorNoise);
        w.put_f64(double(m_settings.sensorSeed));
        w.put_u64(quint64(m_settings.runMode));
        w.put_f64(m_settings.timeScale);
        w.put_f64(m_settings.horizon);
        w.put_f64(m_plant->params().car.wheelsDistance);
    };
    m_viewer->publish(topic::kSimParams, params, nextTimestampUs());
}

void LinePlantDevice::publishTrack() {
    if (!m_viewer || !m_viewer->subscribed(topic::kSimTrack)) {
        return;
    }
    // Compact JSON written by hand: 0.1 mm is plenty, and QJsonDocument's
    // full doubles would not fit the stride's budget.
    const Track& track = m_plant->track();
    QByteArray body;
    body.reserve(int(track.size() / std::size_t(m_trackStride)) * 20 + 64);
    body += "{\"points\":[";
    for (std::size_t i = 0; i < track.size(); i += std::size_t(m_trackStride)) {
        if (i != 0) {
            body += ',';
        }
        body += '[';
        body += QByteArray::number(track.x[i], 'f', 4);
        body += ',';
        body += QByteArray::number(track.y[i], 'f', 4);
        body += ']';
    }
    body += "],\"closed\":true,\"stride\":";
    body += QByteArray::number(m_trackStride);
    body += '}';
    m_viewer->sendText(topic::kSimTrack, body, nextTimestampUs());
}

void LinePlantDevice::onCommand(BtpProducerPort* port, quint16 actionId,
                                btp::ByteView parameters, btp::NodeActionOutcome* outcome,
                                bool stream) {
    Q_UNUSED(port);
    if (actionId == action::kText) {
        bool ok = false;
        const QString line = QString::fromUtf8(reinterpret_cast<const char*>(parameters.data),
                                               qsizetype(parameters.size));
        // The answer outlives this call (the node sends it after return).
        static thread_local QByteArray answer;
        answer = runTextCommand(line.trimmed(), &ok).toUtf8();
        if (!ok) {
            reject(outcome, btp::ResultError::InvalidArgument, answer.constData());
        } else {
            outcome->message = answer.constData();
        }
        return;
    }
    const Params params = decodeParams(m_catalog, actionId, parameters);
    if (!params.ok) {
        if (!stream) {
            reject(outcome, btp::ResultError::MalformedPayload, "malformed parameters");
        }
        return;
    }
    switch (actionId) {
        case action::kLoopU: {
            if (m_mode != loopp::kModeRemote) {
                return;  // discarded outside REMOTE (loop profile 3.1)
            }
            const qint64 refTick = qint64(params.scalar("ref_tick", -1));
            if (refTick <= m_plan.refTick) {
                return;  // late
            }
            const auto& left = params.values.value(QStringLiteral("u_l"));
            const auto& right = params.values.value(QStringLiteral("u_r"));
            const std::size_t n = qMin(left.size(), right.size());
            m_plan.refTick = refTick;
            if (perf::planSentNs != 0) {
                perf::add("plan.transit(ctl->plant)", perf::nowNs() - perf::planSentNs);
            }
            m_plan.uLeft.assign(left.begin(), left.begin() + qint64(n));
            m_plan.uRight.assign(right.begin(), right.begin() + qint64(n));
            if (m_running && refTick >= m_tick) {
                schedule();  // the tick that waited for this plan, now if due
            }
            return;
        }
        case action::kLoopMode: {
            const int mode = int(params.scalar("mode"));
            const bool arm = params.scalar("arm") != 0.0;
            if (mode == loopp::kModeOff) {
                m_mode = loopp::kModeOff;
                m_plan = Plan{};
            } else if (mode == loopp::kModeRemote && arm) {
                m_mode = loopp::kModeRemote;
            } else if (mode == loopp::kModeRemote) {
                reject(outcome, btp::ResultError::NotAuthorized, "REMOTE needs arm");
            } else {
                reject(outcome, btp::ResultError::InvalidArgument, "mode not supported");
            }
            return;
        }
        case action::kLoopConfig: {
            PlantSettings s = m_settings;
            if (params.has("seed")) {
                s.track.seed = std::int64_t(params.scalar("seed"));
            }
            if (params.has("track_type")) {
                s.track.type = params.scalar("track_type") == 1.0 ? TrackType::Circle
                                                                  : TrackType::Lemniscate;
            }
            if (params.has("sensor_noise")) {
                s.sensorNoise = params.scalar("sensor_noise") != 0.0;
            }
            if (params.has("sensor_seed")) {
                s.sensorSeed = quint64(params.scalar("sensor_seed"));
            }
            setSettings(s);
            return;
        }
        case action::kSimRun:
            run();
            return;
        case action::kSimPause:
            pause();
            return;
        case action::kSimReset:
            reset();
            return;
        case action::kSimStep:
            step();
            return;
        case action::kSimDisturb:
            setPerturbation(params.scalar("perturbation"));
            return;
        case action::kSimSpeed: {
            const int mode = int(params.scalar("run_mode"));
            if (mode < 0 || mode > 2) {
                reject(outcome, btp::ResultError::InvalidArgument, "run_mode 0..2");
                return;
            }
            setRunMode(RunMode(mode), params.scalar("time_scale", m_settings.timeScale));
            return;
        }
        default:
            if (!stream) {
                reject(outcome, btp::ResultError::NotFound, "unknown action");
            }
            return;
    }
}

QString LinePlantDevice::runTextCommand(const QString& line, bool* ok) {
    const QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    *ok = true;
    const QString name = words.value(0);
    const QString argument = words.value(1);
    if (name == QLatin1String(kActionSimRun)) {
        run();
        return QStringLiteral("running");
    }
    if (name == QLatin1String(kActionSimPause)) {
        pause();
        return QStringLiteral("paused");
    }
    if (name == QLatin1String(kActionSimReset)) {
        reset();
        return QStringLiteral("reset");
    }
    if (name == QLatin1String(kActionSimStep)) {
        step();
        return QStringLiteral("tick %1").arg(m_tick);
    }
    if (name == QLatin1String(kActionSimDisturb)) {
        bool number = false;
        const double value = argument.toDouble(&number);
        if (number) {
            setPerturbation(value);
            return QStringLiteral("perturbation %1 %").arg(m_plant->perturbation());
        }
    } else if (name == QLatin1String("sim.speed")) {
        if (argument == QLatin1String("fast")) {
            setRunMode(RunMode::AsFastAsPossible, m_settings.timeScale);
            return QStringLiteral("as fast as possible");
        }
        if (argument == QLatin1String("lockstep")) {
            setRunMode(RunMode::Lockstep, m_settings.timeScale);
            return QStringLiteral("lockstep");
        }
        bool number = false;
        const double scale = argument.toDouble(&number);
        if (number && scale > 0) {
            setRunMode(RunMode::RealTime, scale);
            return QStringLiteral("real time x%1").arg(m_settings.timeScale);
        }
    } else if (name == QLatin1String(kActionLoopMode)) {
        if (argument == QLatin1String("remote")) {
            m_mode = loopp::kModeRemote;
            return QStringLiteral("REMOTE");
        }
        if (argument == QLatin1String("off")) {
            m_mode = loopp::kModeOff;
            m_plan = Plan{};
            return QStringLiteral("OFF");
        }
    }
    *ok = false;
    return QStringLiteral("unknown command: %1").arg(line);
}

}  // namespace tracelab::lf
