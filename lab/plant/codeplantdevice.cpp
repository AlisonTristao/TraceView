#include "codeplantdevice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QTimer>
#include <btp/messages.hpp>
#include <btp/profiles/loop.hpp>
#include <algorithm>
#include <btp/telemetry.hpp>
#include <cmath>

#include "lab/code/codefiles.h"
#include "lab/plant/btpproducerport.h"
#include "lab/plant/loopbacktransport.h"
#include "lab/plant/plantmanifest.h"

namespace tracelab {

namespace loopp = btp::profiles::loop;

namespace {

namespace topic {
constexpr quint16 kLoopState = 0x0001;
constexpr quint16 kLoopOutput = 0x0002;
constexpr quint16 kSimTruth = 0x0003;
constexpr quint16 kSimStates = 0x0004;
constexpr quint16 kSimPreview = 0x0005;
constexpr quint16 kSimParams = 0x0006;
constexpr quint16 kDocument = 0x0007;
}  // namespace topic

namespace action {
constexpr quint16 kText = 0x0001;
constexpr quint16 kLoopU = 0x0010;
constexpr quint16 kLoopMode = 0x0011;
constexpr quint16 kSimRun = 0x0013;
constexpr quint16 kSimPause = 0x0014;
constexpr quint16 kSimReset = 0x0015;
constexpr quint16 kSimStep = 0x0016;
constexpr quint16 kSimDisturb = 0x0017;
constexpr quint16 kSimSpeed = 0x0018;
}  // namespace action

constexpr int kMaxPlan = 64;
// How long tick k+1 waits for the plan answering tick k before going on
// with the held input (lfplantdevice.cpp: a stalled controller must not
// freeze the plant; a slow one slows the run down instead).
constexpr qint64 kPlanTimeoutMs = 10000;
constexpr double kMaxBacklogSeconds = 0.25;
constexpr int kReloadDelayMs = 150;
constexpr std::uint32_t kNoCap = 0U;

QString tr(const char* text) {
    return QCoreApplication::translate("tracelab::CodePlantDevice", text);
}

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

btp::FieldRecord array(btp::FieldRecord field, int maxCount) {
    field.flags = std::uint8_t(field.flags | btp::kFieldVariableCount);
    field.element_count = 0;
    field.max_element_count = std::uint16_t(maxCount);
    return field;
}

// An action's PACKED_LE parameters, one double per element, by field name.
struct Params {
    QHash<QString, std::vector<double>> values;
    bool ok = false;
    double scalar(const char* name, double fallback = 0.0) const {
        const auto it = values.constFind(QLatin1String(name));
        return it == values.constEnd() || it->empty() ? fallback : it->front();
    }
};

Params decodeParams(const btp::Catalog& catalog, quint16 actionId, btp::ByteView parameters) {
    Params out;
    const btp::CatalogAction* a = catalog.action(actionId, 1);
    if (a == nullptr) {
        return out;
    }
    if (a->parameter_count == 0) {
        out.ok = true;
        return out;
    }
    btp::SampleReader reader(parameters.data, parameters.size, a->fields, a->parameter_count,
                             btp::kEncodingPackedLe, btp::SampleLayout::BodyOnly);
    btp::SampleValue value{};
    std::size_t index = 0;
    while (reader.next(&value) == btp::SampleStep::Item) {
        std::vector<double> elements;
        if (!value.is_null) {
            for (std::size_t i = 0; i < value.count; ++i) {
                elements.push_back(value.f64(i));
            }
        }
        out.values.insert(QString::fromUtf8(catalog.field_name(*a, index)), elements);
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

// ---------------------------------------------------------------------------
// The plant's code as block 0 of a CodeBlockOwner.

class CodePlantDevice::Blocks final : public CodeBlockOwner {
public:
    explicit Blocks(CodePlantDevice* device) : CodeBlockOwner(device), m_device(device) {}

    code::BlockKind blockKind(int) const override {
        return code::BlockKind::Plant;
    }
    QString blockRole(int block) const override {
        return block == 0 ? CodePlantDevice::tr("Plant") : QString();
    }
    QString blockName(int) const override {
        const CodeBlockSpec& s = m_device->m_spec;
        if (!s.path.isEmpty()) {
            return QFileInfo(s.path).completeBaseName();
        }
        if (const code::CodeTemplate* t = code::CodeLibrary::instance().findTemplate(s.templateId)) {
            return t->name;
        }
        return s.templateId;
    }
    CodeBlockSpec spec(int) const override {
        return m_device->m_spec;
    }
    void setSpec(int, const CodeBlockSpec& spec) override {
        m_device->m_spec = spec;
        m_device->loadPlant();
        emit configChanged();
        emit m_device->settingsChanged();
    }
    void reload(int) override {
        m_device->loadPlant();
    }
    QString source(int) const override {
        return m_device->m_source;
    }
    const code::CodeModule* module(int) const override {
        return m_device->m_plant ? &m_device->m_plant->module() : nullptr;
    }
    QVector<code::CodeMessage> messages(int) const override {
        return m_device->m_messages;
    }
    bool compiled(int) const override {
        return m_device->m_compiled;
    }
    QString runtimeError(int) const override {
        return m_device->m_plant ? m_device->m_plant->runtimeErrorText() : QString();
    }
    QStringList takeLog(int) override {
        return m_device->m_plant ? m_device->m_plant->module().takeLog() : QStringList();
    }
    void setParam(int, const QString& name, double value) override {
        if (m_device->m_plant) {
            m_device->m_plant->module().setParamValue(name, value);
            value = m_device->m_plant->module().paramValue(name);
        }
        m_device->m_spec.params.insert(name, value);
        emit configChanged();
        emit m_device->settingsChanged();
    }
    Status status(int) const override {
        if (!m_device->m_plant) {
            return {3, CodePlantDevice::tr("The plant's code does not compile.")};
        }
        if (!m_device->m_plant->runtimeErrorText().isEmpty()) {
            return {3, CodePlantDevice::tr("Stopped by a fault: reset the simulation once it is fixed.")};
        }
        if (m_device->m_finished) {
            return {1, CodePlantDevice::tr("Run finished.")};
        }
        return m_device->m_running ? Status{1, CodePlantDevice::tr("Running at %1 Hz.").arg(m_device->m_fps)}
                                   : Status{2, CodePlantDevice::tr("Paused.")};
    }
    QString note(int) const override {
        if (!m_device->m_environment) {
            return QString();
        }
        return CodePlantDevice::tr("Lives in: %1. Its functions are declared in the header it names; it reads the "
                  "states %2.")
            .arg(m_device->m_environment->name(),
                 m_device->m_environment->requiredStates().join(QStringLiteral(", ")));
    }
    QVector<SignalRow> signalRows(int) const override {
        QVector<SignalRow> rows;
        const CodePlant* plant = m_device->m_plant.get();
        if (!plant) {
            rows.append({CodePlantDevice::tr("Not compiled."), QString(), false});
            return rows;
        }
        appendSignalRows(&rows, CodePlantDevice::tr("Inputs (loop.u, from the controller)"), plant->inputNames(),
                         plant->appliedInputs(), QStringLiteral("TL_INPUT(%1, \"\", -100, 100);"),
                         QStringLiteral("TL_INPUT(%1, \"\", -100, 100);"));
        appendSignalRows(&rows, CodePlantDevice::tr("States (sim.states)"), plant->stateNames(), plant->states(),
                         QStringLiteral("TL_STATE(%1, \"\", 0);"),
                         QStringLiteral("TL_STATE(%1, \"\", 0);"));
        appendSignalRows(&rows, CodePlantDevice::tr("Outputs (loop.state, what the controller reads)"),
                         plant->outputNames(), plant->outputs(), QStringLiteral("TL_OUTPUT(%1, \"\");"),
                         QStringLiteral("TL_OUTPUT_ARRAY(%1, %2, \"\");"));
        return rows;
    }

private:
    CodePlantDevice* m_device;
};

// ---------------------------------------------------------------------------

CodePlantDevice::CodePlantDevice(const QString& deviceId, quint32 sourceId,
                                 const QJsonObject& config, QObject* parent)
    : LabPlantDevice(parent), m_deviceId(deviceId), m_sourceId(sourceId) {
    m_blocks = new Blocks(this);
    connect(m_blocks, &CodeBlockOwner::configChanged, this, &LabPlantDevice::settingsChanged);
    m_spec = CodeBlockSpec::fromJson(config.value(QStringLiteral("plant")).toObject());
    m_fps = qBound(1, config.value(QStringLiteral("fps")).toInt(m_fps), 1000);
    m_runMode = runModeFromName(config.value(QStringLiteral("runMode")).toString(), m_runMode);
    m_timeScale = qBound(0.01, config.value(QStringLiteral("timeScale")).toDouble(m_timeScale), 100.0);
    m_autoRun = config.value(QStringLiteral("autoRun")).toBool(m_autoRun);
    m_solver = config.value(QStringLiteral("solver")).toString() == QLatin1String("euler")
                   ? CodePlant::Solver::Euler
                   : CodePlant::Solver::Rk4;
    m_substeps = qBound(1, config.value(QStringLiteral("substeps")).toInt(m_substeps), 1000);
    const QJsonObject environment = config.value(QStringLiteral("environment")).toObject();
    const QString environmentId = environment.value(QStringLiteral("id")).toString();
    if (!environmentId.isEmpty()) {
        m_environment = PlantEnvironmentRegistry::instance().create(environmentId);
        if (m_environment) {
            m_environment->setConfig(environment);
        }
    }

    const quint32 bootId = quint32(QDateTime::currentMSecsSinceEpoch() & 0x7fffffff) | 1U;
    static const QByteArray kName = QByteArrayLiteral("TraceLab: Plant");
    m_viewer = new BtpProducerPort(&m_catalog, sourceId, bootId, kName, this);
    m_controller = new BtpProducerPort(&m_catalog, sourceId, bootId, kName, this);
    for (BtpProducerPort* port : {m_viewer, m_controller}) {
        port->setCommandHandler([this](quint16 actionId, btp::ByteView parameters,
                                       btp::NodeActionOutcome* outcome, bool stream) {
            onCommand(actionId, parameters, outcome, stream);
        });
    }
    connect(m_viewer, &BtpProducerPort::subscriptionsChanged, this, [this] {
        publishDocument();
        publishParams();
        publishTruth(m_viewer, false, nextTimestampUs());
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &CodePlantDevice::pump);

    m_watcher = new QFileSystemWatcher(this);
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(kReloadDelayMs);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, m_reloadTimer,
            qOverload<>(&QTimer::start));
    connect(m_reloadTimer, &QTimer::timeout, this, [this] {
        // Only a real change (the editor saves and reloads on its own).
        if (code::CodeLibrary::readFile(m_spec.path) != m_source) {
            loadPlant();
        }
        if (!m_spec.path.isEmpty() && !m_watcher->files().contains(m_spec.path) &&
            QFileInfo::exists(m_spec.path)) {
            m_watcher->addPath(m_spec.path);
        }
    });

    loadPlant();
}

CodePlantDevice::~CodePlantDevice() {
    m_timer->stop();
}

CodeBlockOwner* CodePlantDevice::codeBlocks() const {
    return m_blocks;
}

QString CodePlantDevice::loadSource(QString* fileName) const {
    if (!m_spec.path.isEmpty()) {
        *fileName = m_spec.path;
        return code::CodeLibrary::readFile(m_spec.path);
    }
    if (!m_spec.templateId.isEmpty()) {
        *fileName = m_spec.templateId.section(QLatin1Char('/'), -1) + QStringLiteral(".c");
        return code::CodeLibrary::instance().templateSource(m_spec.templateId);
    }
    fileName->clear();
    return QString();
}

void CodePlantDevice::loadPlant() {
    QString fileName;
    m_source = loadSource(&fileName);
    // Watch the file (a save by rename drops it from the watcher).
    for (const QString& path : m_watcher->files()) {
        if (path != m_spec.path) {
            m_watcher->removePath(path);
        }
    }
    if (!m_spec.path.isEmpty() && QFileInfo::exists(m_spec.path) &&
        !m_watcher->files().contains(m_spec.path)) {
        m_watcher->addPath(m_spec.path);
    }
    if (m_spec.isEmpty() || (!m_spec.path.isEmpty() && !QFileInfo::exists(m_spec.path))) {
        m_messages = {{0, m_spec.isEmpty() ? tr("no plant code")
                                           : tr("file not found: %1").arg(m_spec.path),
                       false}};
        emit m_blocks->blocksChanged();
        return;
    }

    auto plant = std::make_unique<CodePlant>();
    plant->module().setParamOverrides(m_spec.params);
    bool ok = plant->load(m_source, fileName,
                          m_environment ? m_environment->symbols() : QVector<code::HostSymbol>());
    m_messages = plant->module().messages();
    if (!ok && !plant->errorText().isEmpty() &&
        std::none_of(m_messages.cbegin(), m_messages.cend(),
                     [](const code::CodeMessage& m) { return !m.warning; })) {
        m_messages.append({0, plant->errorText(), false});
    }
    if (ok && m_environment) {
        QStringList missing;
        for (const QString& name : m_environment->requiredStates()) {
            if (!plant->hasState(name)) {
                missing.append(name);
            }
        }
        if (!missing.isEmpty()) {
            m_messages.append({0, tr("%1 needs the states %2 (TL_STATE)")
                                      .arg(m_environment->name(), missing.join(QStringLiteral(", "))),
                               false});
            ok = false;
        }
    }
    if (!ok) {
        // The previous version (if any) keeps running.
        emit m_blocks->blocksChanged();
        return;
    }
    m_plant = std::move(plant);
    m_plant->module().context().environment = m_environment.get();
    m_compiled = true;

    // A new manifest when the code declares something else.
    QStringList signature = m_plant->inputNames() + QStringList{QStringLiteral("|")} +
                            m_plant->outputNames() + QStringList{QStringLiteral("|")} +
                            m_plant->stateNames() + QStringList{QStringLiteral("|")} +
                            m_plant->diagNames();
    const bool newManifest = signature != m_declarationSignature;
    m_declarationSignature = signature;
    if (newManifest) {
        rebuildCatalog();
    }
    reset();
    if (newManifest) {
        restartSessions();
    }
    emit m_blocks->blocksChanged();
    emit settingsChanged();
}

const char* CodePlantDevice::keep(const QString& text) {
    m_strings.push_back(text.toUtf8());
    return m_strings.back().constData();
}

void CodePlantDevice::rebuildCatalog() {
    m_catalog.clear();
    m_catalog.set_config_revision_auto();
    m_strings.clear();
    using btp::TelemetryEncoding;
    const auto declared = [this](code::DeclKind kind) {
        return m_plant->module().declarations(kind);
    };

    std::vector<btp::FieldRecord> state = {btp::u32("tick"), btp::enum8("mode")};
    for (const code::Declaration* d : declared(code::DeclKind::Output)) {
        btp::FieldRecord field = btp::f64(keep(d->name), keep(d->unit));
        state.push_back(d->count > 1 ? array(field, d->count) : field);
    }
    std::vector<btp::FieldRecord> output = {btp::u32("ref_tick"), btp::enum8("source")};
    std::vector<btp::FieldRecord> loopU = {btp::u32("ref_tick")};
    for (const code::Declaration* d : declared(code::DeclKind::Input)) {
        btp::FieldRecord field = btp::f64(keep(d->name), keep(d->unit));
        if (d->maximum > d->minimum) {
            field = btp::range(field, d->minimum, d->maximum);
        }
        output.push_back(field);
        loopU.push_back(array(field, kMaxPlan));
    }
    std::vector<btp::FieldRecord> states = {btp::u32("tick")};
    for (const code::Declaration* d : declared(code::DeclKind::State)) {
        states.push_back(btp::f64(keep(d->name), keep(d->unit)));
    }
    for (const code::Declaration* d : declared(code::DeclKind::Diag)) {
        states.push_back(btp::f64(keep(d->name), keep(d->unit)));
    }
    states.push_back(btp::f64("perturbation", "%"));
    std::vector<btp::FieldRecord> truth = {btp::u32("tick"), btp::f64("t", "s")};
    if (m_environment && !m_environment->truthFields().isEmpty()) {
        for (const auto& field : m_environment->truthFields()) {
            truth.push_back(btp::f64(keep(field.first), keep(field.second)));
        }
    } else {
        for (const code::Declaration* d : declared(code::DeclKind::State)) {
            truth.push_back(btp::f64(keep(d->name), keep(d->unit)));
        }
    }
    const std::vector<btp::FieldRecord> params = {btp::f64("ts", "s"), btp::enum8("run_mode"),
                                                  btp::f64("time_scale", "1")};
    m_catalog.add_topic(topic::kLoopState, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                        kTopicLoopState, state.data(), state.size());
    m_catalog.add_topic(topic::kLoopOutput, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                        kTopicLoopOutput, output.data(), output.size());
    m_catalog.add_topic(topic::kSimTruth, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                        kTopicSimTruth, truth.data(), truth.size());
    m_catalog.add_topic(topic::kSimStates, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                        kTopicSimStates, states.data(), states.size());
    if (m_environment && m_environment->previewCapacity() > 0) {
        const int capacity = m_environment->previewCapacity();
        const btp::FieldRecord preview[] = {btp::u32("tick"),
                                            array(btp::f64("px", "m"), capacity),
                                            array(btp::f64("py", "m"), capacity)};
        m_catalog.add_topic(topic::kSimPreview, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                            kTopicSimPreview, preview, std::size(preview));
    }
    m_catalog.add_topic(topic::kSimParams, 1, TelemetryEncoding::PackedLe, true, kNoCap,
                        kTopicSimParams, params.data(), params.size());
    if (m_environment && !m_environment->documentTopic().isEmpty()) {
        m_catalog.add_topic(topic::kDocument, 1, TelemetryEncoding::Utf8, true, kNoCap,
                            keep(QString::fromUtf8(m_environment->documentTopic())), nullptr, 0);
    }

    using btp::ActionEncoding;
    const btp::FieldRecord loopMode[] = {btp::enum8("mode"), btp::boolean("arm")};
    const btp::FieldRecord disturb[] = {btp::range(btp::f64("perturbation", "%"), -100.0, 100.0)};
    const btp::FieldRecord speed[] = {btp::enum8("run_mode"), btp::f64("time_scale", "1")};
    const auto none = [&](quint16 id, const char* name, const char* description) {
        m_catalog.add_action(id, 1, 0, ActionEncoding::Empty, name, nullptr, 0, nullptr, 0,
                             ActionEncoding::Empty, 0, description);
    };
    m_catalog.add_action(action::kLoopU, 1, btp::kActionStream, ActionEncoding::PackedLe,
                         kActionLoopU, loopU.data(), loopU.size(), nullptr, 0,
                         ActionEncoding::Empty, 0, "The plant's inputs for the next ticks");
    m_catalog.add_action(action::kLoopMode, 1, btp::kActionDangerous, ActionEncoding::PackedLe,
                         kActionLoopMode, loopMode, std::size(loopMode), nullptr, 0,
                         ActionEncoding::Empty, 0, "Loop mode",
                         "Let the controller drive the plant?");
    none(action::kSimRun, kActionSimRun, "Run");
    none(action::kSimPause, kActionSimPause, "Pause");
    none(action::kSimReset, kActionSimReset, "Back to the start");
    none(action::kSimStep, kActionSimStep, "One tick");
    m_catalog.add_action(action::kSimDisturb, 1, 0, ActionEncoding::PackedLe, kActionSimDisturb,
                         disturb, std::size(disturb), nullptr, 0, ActionEncoding::Empty, 0,
                         "Disturbance (tl_disturb)");
    m_catalog.add_action(action::kSimSpeed, 1, 0, ActionEncoding::PackedLe, "sim.speed", speed,
                         std::size(speed), nullptr, 0, ActionEncoding::Empty, 0,
                         "Real time, as fast as possible or lockstep");
}

void CodePlantDevice::restartSessions() {
    // Both peers read the new manifest on their next session: end these.
    if (m_viewerLink) {
        m_viewerLink->close();
    }
    if (m_controllerLink) {
        m_controllerLink->close();
    }
}

// ---------------------------------------------------------------------------
// LabPlantDevice

QString CodePlantDevice::typeName() const {
    const QString name = m_blocks->blockName(0);
    if (!name.isEmpty()) {
        return name;
    }
    return m_environment ? m_environment->name() : tr("Plant (C)");
}

QString CodePlantDevice::worldName() const {
    return m_environment ? m_environment->name() : QString();
}

double CodePlantDevice::time() const {
    return m_plant ? m_plant->time() : 0.0;
}

void CodePlantDevice::attachViewer(LoopbackTransport* peer) {
    if (m_viewerLink && m_viewerLink != peer) {
        m_viewerLink->close();
    }
    m_viewerLink = peer;
    if (peer) {
        peer->setParent(this);
    }
    m_viewer->setTransport(peer);
}

void CodePlantDevice::attachController(LoopbackTransport* peer) {
    if (m_controllerLink && m_controllerLink != peer) {
        m_controllerLink->close();
    }
    m_controllerLink = peer;
    if (peer) {
        peer->setParent(this);
    }
    m_controller->setTransport(peer);
}

void CodePlantDevice::setSampleRate(int hz) {
    hz = qBound(1, hz, 1000);
    if (hz == m_fps) {
        return;
    }
    m_fps = hz;
    reset();
    emit settingsChanged();
}

void CodePlantDevice::setRunMode(RunMode mode, double timeScale) {
    m_runMode = mode;
    m_timeScale = qBound(0.01, timeScale, 100.0);
    m_clock.restart();
    m_ticksAtClockStart = double(m_tick);
    publishParams();
    if (m_running) {
        schedule();
    }
    emit settingsChanged();
}

QVector<PlantSetting> CodePlantDevice::settingsSchema() const {
    QVector<PlantSetting> list;
    if (m_plant && !m_plant->isDiscrete()) {
        PlantSetting solver;
        solver.key = QStringLiteral("solver");
        solver.label = tr("Solver");
        solver.toolTip = tr("How plant_deriv() is integrated over each sample period.");
        solver.type = PlantSetting::Type::Enum;
        solver.options = {tr("Euler"), tr("Runge-Kutta 4")};
        solver.value = m_solver == CodePlant::Solver::Euler ? 0 : 1;
        list.append(solver);
        PlantSetting substeps;
        substeps.key = QStringLiteral("substeps");
        substeps.label = tr("Steps per sample");
        substeps.toolTip = tr("Solver steps in each sample period (the input is held over all of "
                              "them).");
        substeps.type = PlantSetting::Type::Int;
        substeps.value = m_substeps;
        substeps.minimum = 1;
        substeps.maximum = 1000;
        list.append(substeps);
    }
    if (m_environment) {
        list += m_environment->settingsSchema();
    }
    return list;
}

void CodePlantDevice::applySettings(const QVariantMap& values) {
    if (values.contains(QStringLiteral("solver"))) {
        m_solver = values.value(QStringLiteral("solver")).toInt() == 0 ? CodePlant::Solver::Euler
                                                                         : CodePlant::Solver::Rk4;
    }
    if (values.contains(QStringLiteral("substeps"))) {
        m_substeps = qBound(1, values.value(QStringLiteral("substeps")).toInt(), 1000);
    }
    if (m_environment) {
        m_environment->applySettings(values);
    }
    reset();
    emit settingsChanged();
}

QVector<QPair<QString, QString>> CodePlantDevice::statusItems() const {
    QVector<QPair<QString, QString>> items;
    if (m_environment) {
        items = m_environment->statusItems();
    }
    if (m_perturbation != 0.0) {
        items.append({tr("perturbation"),
                      tr("%1 %").arg(QLocale().toString(m_perturbation, 'f', 1))});
    }
    return items;
}

QJsonObject CodePlantDevice::config() const {
    QJsonObject json{
        {"plant", m_spec.toJson()},
        {"fps", m_fps},
        {"runMode", runModeName(m_runMode)},
        {"timeScale", m_timeScale},
        {"autoRun", m_autoRun},
        {"solver", m_solver == CodePlant::Solver::Euler ? "euler" : "rk4"},
        {"substeps", m_substeps},
    };
    if (m_environment) {
        QJsonObject environment = m_environment->config();
        environment.insert(QStringLiteral("id"), m_environment->id());
        json.insert(QStringLiteral("environment"), environment);
    }
    return json;
}

// ---------------------------------------------------------------------------
// The clock

void CodePlantDevice::run() {
    if (m_running || m_finished || !m_plant) {
        return;
    }
    m_running = true;
    m_clock.restart();
    m_ticksAtClockStart = double(m_tick);
    m_waitClock.restart();
    emit runningChanged(true);
    schedule();
}

void CodePlantDevice::pause() {
    if (!m_running) {
        return;
    }
    m_running = false;
    m_timer->stop();
    emit runningChanged(false);
}

void CodePlantDevice::reset() {
    m_tick = 0;
    m_finished = false;
    m_plan = Plan{};
    m_appliedRefTick = -1;
    m_appliedSource = loopp::kSourceNone;
    if (m_plant) {
        m_applied = QVector<double>(m_plant->inputNames().size(), 0.0);
        QHash<QString, double> initial;
        if (m_environment) {
            m_environment->reset(1.0 / m_fps, &initial);
        }
        m_plant->setSampleTime(1.0 / m_fps);
        m_plant->setSolver(m_solver, m_substeps);
        m_plant->setDisturbances(m_environment ? m_environment->disturbances(m_perturbation)
                                               : QVector<double>{m_perturbation});
        m_plant->reset(initial);
    }
    m_clock.restart();
    m_ticksAtClockStart = 0.0;
    publishDocument();
    publishParams();
    publishTruth(m_viewer, false, nextTimestampUs());
    emit ticked(0);
    if (m_running) {
        schedule();
    }
}

void CodePlantDevice::step() {
    if (m_running) {
        return;
    }
    tickOnce();
}

void CodePlantDevice::setPerturbation(double percent) {
    m_perturbation = qBound(-100.0, percent, 100.0);
}

bool CodePlantDevice::waitingForPlan() const {
    // As the line follower's plant: real time and as fast as possible wait
    // only once the controller answered at all; lockstep always waits.
    const bool answered = m_plan.refTick >= 0 || m_runMode == RunMode::Lockstep;
    return m_mode == loopp::kModeRemote && m_tick >= 1 && answered && m_plan.refTick < m_tick &&
           m_controller->subscribed(topic::kLoopState) && m_waitClock.elapsed() < kPlanTimeoutMs;
}

int CodePlantDevice::planTimeoutLeftMs() const {
    return int(qMax<qint64>(1, kPlanTimeoutMs - m_waitClock.elapsed()));
}

void CodePlantDevice::schedule() {
    if (!m_running) {
        return;
    }
    if (m_runMode == RunMode::RealTime) {
        const double fps = m_fps * m_timeScale;
        const double due = m_ticksAtClockStart + m_clock.nsecsElapsed() * 1e-9 * fps;
        if (double(m_tick) + 1.0 <= due) {
            m_timer->start(waitingForPlan() ? planTimeoutLeftMs() : 0);
        } else {
            const double waitMs = (double(m_tick) + 1.0 - due) * 1000.0 / fps;
            m_timer->start(qMax(0, int(std::ceil(waitMs))));
        }
        return;
    }
    // Waiting for the plan: its arrival reschedules at once (onCommand).
    m_timer->start(waitingForPlan() ? planTimeoutLeftMs() : 0);
}

void CodePlantDevice::pump() {
    if (!m_running) {
        return;
    }
    if (m_runMode == RunMode::RealTime) {
        const double fps = m_fps * m_timeScale;
        const double due = m_ticksAtClockStart + m_clock.nsecsElapsed() * 1e-9 * fps;
        if (double(m_tick) + 1.0 > due) {
            schedule();
            return;
        }
        if (due - double(m_tick) > kMaxBacklogSeconds * fps) {
            m_clock.restart();
            m_ticksAtClockStart = double(m_tick);
        }
    }
    if (waitingForPlan()) {
        schedule();
        return;
    }
    if (tickOnce()) {
        schedule();
    }
}

bool CodePlantDevice::tickOnce() {
    if (m_finished || !m_plant) {
        return false;
    }
    const long long k = m_tick + 1;
    const int inputs = m_applied.size();
    if (m_mode == loopp::kModeRemote) {
        if (m_plan.refTick >= 0) {
            const long long j = k - m_plan.refTick - 1;
            if (j >= 0 && !m_plan.inputs.empty() && j < (long long)m_plan.inputs[0].size()) {
                for (int i = 0; i < inputs && i < int(m_plan.inputs.size()); ++i) {
                    m_applied[i] = m_plan.inputs[std::size_t(i)][std::size_t(j)];
                }
                m_appliedRefTick = m_plan.refTick;
            }
            // No element for this tick: the last applied input is held.
            m_appliedSource = loopp::kSourceRemote;
        } else {
            m_appliedSource = loopp::kSourceNone;
        }
    } else {
        m_applied.fill(0.0);
        m_appliedSource = loopp::kSourceNone;
    }
    m_plant->setDisturbances(m_environment ? m_environment->disturbances(m_perturbation)
                                           : QVector<double>{m_perturbation});
    if (!m_plant->step(m_applied)) {
        pause();  // a fault in the code: stopped until reset
        emit m_blocks->statusChanged();
        return false;
    }
    m_tick = m_plant->tick();
    if (m_environment) {
        m_environment->afterStep(*m_plant);
    }
    m_waitClock.restart();
    publishTick();
    emit ticked(m_tick);
    if (m_environment && m_environment->finished()) {
        m_finished = true;
        pause();
        emit finished();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Publishing

quint64 CodePlantDevice::nextTimestampUs() {
    const quint64 now = quint64(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    m_lastTimestampUs = qMax(now, m_lastTimestampUs + 1);
    return m_lastTimestampUs;
}

void CodePlantDevice::publishTick() {
    const quint64 us = nextTimestampUs();
    const quint32 tick = quint32(m_tick);
    const code::CodeModule& module = m_plant->module();
    const auto stateFill = [&](btp::SampleWriter& w) {
        w.put_f64(tick);
        w.put_u64(m_mode);
        for (const code::Declaration* d : module.declarations(code::DeclKind::Output)) {
            if (d->count > 1) {
                w.put_array_f64(d->values(), std::size_t(d->count));
            } else {
                w.put_f64(*d->values());
            }
        }
    };
    const auto outputFill = [&](btp::SampleWriter& w) {
        w.put_f64(m_appliedRefTick < 0 ? 0.0 : double(m_appliedRefTick));
        w.put_u64(m_appliedSource);
        for (double value : std::as_const(m_applied)) {
            w.put_f64(value);
        }
    };
    const auto statesFill = [&](btp::SampleWriter& w) {
        w.put_f64(tick);
        for (double value : m_plant->states()) {
            w.put_f64(value);
        }
        for (double value : m_plant->diags()) {
            w.put_f64(value);
        }
        w.put_f64(m_perturbation);
    };
    QVector<double> px;
    QVector<double> py;
    const bool hasPreview = m_environment && m_environment->previewCapacity() > 0;
    if (hasPreview) {
        m_environment->preview(&px, &py);
    }
    const auto previewFill = [&](btp::SampleWriter& w) {
        const int capacity = m_environment->previewCapacity();
        w.put_f64(tick);
        w.put_array_f64(px.constData(), std::size_t(qMin(int(px.size()), capacity)));
        w.put_array_f64(py.constData(), std::size_t(qMin(int(py.size()), capacity)));
    };
    // The controller first (every tick, the preview before the state), then
    // the viewer at its own rates.
    if (hasPreview) {
        m_controller->publish(topic::kSimPreview, previewFill, us);
    }
    m_controller->publish(topic::kLoopState, stateFill, us);
    m_controller->publish(topic::kLoopOutput, outputFill, us);
    if (hasPreview) {
        m_viewer->publishDue(topic::kSimPreview, previewFill, us);
    }
    m_viewer->publishDue(topic::kLoopState, stateFill, us);
    m_viewer->publishDue(topic::kLoopOutput, outputFill, us);
    m_viewer->publishDue(topic::kSimStates, statesFill, us);
    publishTruth(m_viewer, true, us);
}

void CodePlantDevice::publishTruth(BtpProducerPort* port, bool due, quint64 us) {
    if (!m_plant || !port) {
        return;
    }
    const quint32 tick = quint32(m_tick);
    const QVector<double> values = m_environment && !m_environment->truthFields().isEmpty()
                                       ? m_environment->truthValues(*m_plant)
                                       : m_plant->states();
    const double t = m_plant->time();
    const auto fill = [&](btp::SampleWriter& w) {
        w.put_f64(tick);
        w.put_f64(t);
        for (double value : values) {
            w.put_f64(value);
        }
    };
    if (due) {
        port->publishDue(topic::kSimTruth, fill, us);
    } else {
        port->publish(topic::kSimTruth, fill, us);
    }
}

void CodePlantDevice::publishParams() {
    if (!m_viewer || !m_plant) {
        return;
    }
    const auto fill = [&](btp::SampleWriter& w) {
        w.put_f64(1.0 / m_fps);
        w.put_u64(quint64(m_runMode));
        w.put_f64(m_timeScale);
    };
    m_viewer->publish(topic::kSimParams, fill, nextTimestampUs());
}

void CodePlantDevice::publishDocument() {
    if (!m_viewer || !m_environment || m_environment->documentTopic().isEmpty() ||
        !m_viewer->subscribed(topic::kDocument)) {
        return;
    }
    m_viewer->sendText(topic::kDocument, m_environment->document(), nextTimestampUs());
}

// ---------------------------------------------------------------------------
// Commands

void CodePlantDevice::onCommand(quint16 actionId, btp::ByteView parameters,
                                btp::NodeActionOutcome* outcome, bool stream) {
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
            if (m_mode != loopp::kModeRemote || !m_plant) {
                return;  // discarded outside REMOTE (loop profile 3.1)
            }
            const qint64 refTick = qint64(params.scalar("ref_tick", -1));
            if (refTick <= m_plan.refTick) {
                return;  // late
            }
            const QStringList names = m_plant->inputNames();
            std::vector<std::vector<double>> inputs;
            std::size_t steps = std::size_t(-1);
            for (const QString& name : names) {
                inputs.push_back(params.values.value(name));
                steps = qMin(steps, inputs.back().size());
            }
            for (std::vector<double>& plan : inputs) {
                plan.resize(names.isEmpty() ? 0 : steps);
            }
            m_plan.refTick = refTick;
            m_plan.inputs = std::move(inputs);
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
            setRunMode(RunMode(mode), params.scalar("time_scale", m_timeScale));
            return;
        }
        default:
            if (!stream) {
                reject(outcome, btp::ResultError::NotFound, "unknown action");
            }
            return;
    }
}

QString CodePlantDevice::runTextCommand(const QString& line, bool* ok) {
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
            return QStringLiteral("perturbation %1 %").arg(m_perturbation);
        }
    } else if (name == QLatin1String("sim.speed")) {
        if (argument == QLatin1String("fast")) {
            setRunMode(RunMode::AsFastAsPossible, m_timeScale);
            return QStringLiteral("as fast as possible");
        }
        if (argument == QLatin1String("lockstep")) {
            setRunMode(RunMode::Lockstep, m_timeScale);
            return QStringLiteral("lockstep");
        }
        bool number = false;
        const double scale = argument.toDouble(&number);
        if (number && scale > 0) {
            setRunMode(RunMode::RealTime, scale);
            return QStringLiteral("real time x%1").arg(m_timeScale);
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

// ---------------------------------------------------------------------------

PlantEnvironmentRegistry& PlantEnvironmentRegistry::instance() {
    static PlantEnvironmentRegistry registry;
    return registry;
}

void PlantEnvironmentRegistry::registerEnvironment(const Entry& entry) {
    for (const Entry& known : std::as_const(m_entries)) {
        if (known.id == entry.id) {
            return;
        }
    }
    m_entries.append(entry);
}

std::unique_ptr<PlantEnvironment> PlantEnvironmentRegistry::create(const QString& id) const {
    for (const Entry& entry : m_entries) {
        if (entry.id == id && entry.create) {
            return entry.create();
        }
    }
    return nullptr;
}

}  // namespace tracelab
