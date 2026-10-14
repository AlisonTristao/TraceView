#include "labdevices.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QTimer>

#include "lab/code/codefiles.h"
#include "lab/control/codecontrolleradapter.h"
#include "lab/control/codesignalfilter.h"
#include "lab/control/controllerhost.h"
#include "lab/plant/codeplantdevice.h"
#include "lab/plant/endpointtransport.h"
#include "lab/plant/labplantdevice.h"
#include "lab/plant/loopbacktransport.h"

namespace tracelab {

namespace {

constexpr int kFindPlantIntervalMs = 500;
// Editors save in bursts (truncate, write, rename): one compile after them.
constexpr int kReloadDelayMs = 150;

}  // namespace

quint32 labDeviceSourceId(const QString& deviceId, const char* typeId) {
    quint32 hash = 2166136261u;
    const QByteArray bytes = QByteArray(typeId) + '/' + deviceId.toUtf8();
    for (char byte : bytes) {
        hash ^= quint8(byte);
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

// ---------------------------------------------------------------------------

LabControllerDevice::LabControllerDevice(const QString& deviceId, const QJsonObject& config,
                                         QObject* parent)
    : CodeBlockOwner(parent), m_deviceId(deviceId) {
    m_host = new ControllerHost(labDeviceSourceId(deviceId, kLabControllerDeviceType), this);
    connect(m_host, &ControllerHost::plantReadyChanged, this, &LabControllerDevice::statusChanged);
    connect(m_host, &ControllerHost::configurationChanged, this,
            &LabControllerDevice::statusChanged);
    // New ctl.diag fields reach TraceView on its next session: start one.
    connect(m_host, &ControllerHost::diagSchemaChanged, this, [this] {
        if (m_viewerLink) {
            m_viewerLink->close();
        }
    });

    m_watcher = new QFileSystemWatcher(this);
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(kReloadDelayMs);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (!m_pendingReloads.contains(path)) {
            m_pendingReloads.append(path);
        }
        m_reloadTimer->start();
    });
    connect(m_reloadTimer, &QTimer::timeout, this, [this] {
        const QStringList paths = m_pendingReloads;
        m_pendingReloads.clear();
        for (const QString& path : paths) {
            const QString canonical = QFileInfo(path).absoluteFilePath();
            if (QFileInfo(m_controller.spec.path).absoluteFilePath() == canonical) {
                reload(kController);
            }
            for (int i = 0; i < m_filterBlocks.size(); ++i) {
                if (QFileInfo(m_filterBlocks[i].spec.path).absoluteFilePath() == canonical) {
                    reload(i);
                }
            }
        }
        watchFiles();  // a save by rename drops the file from the watcher
    });

    m_plantDeviceId = config.value(QStringLiteral("plantDeviceId")).toString();
    m_controller.spec = CodeBlockSpec::fromJson(config.value(QStringLiteral("controller")).toObject());
    for (const QJsonValue& value : config.value(QStringLiteral("filters")).toArray()) {
        Block block;
        block.spec = CodeBlockSpec::fromJson(value.toObject());
        m_filterBlocks.append(block);
        m_filterSpecs.append(block.spec);
    }
    loadFilters();
    loadController();
    watchFiles();

    m_findTimer = new QTimer(this);
    m_findTimer->setInterval(kFindPlantIntervalMs);
    connect(m_findTimer, &QTimer::timeout, this, &LabControllerDevice::linkPlant);
    connect(&LabDeviceRegistry::instance(), &LabDeviceRegistry::devicesChanged, this,
            &LabControllerDevice::linkPlant);
    m_findTimer->start();
    linkPlant();
}

LabControllerDevice::~LabControllerDevice() {
    // Nothing may call back into this half-destroyed object: the registry
    // announces this very destruction (QObject::destroyed), and the host and
    // the links emit as they go down with it.
    disconnect(&LabDeviceRegistry::instance(), nullptr, this, nullptr);
    disconnect(m_host, nullptr, this, nullptr);
    if (m_plant) {
        disconnect(m_plant, nullptr, this, nullptr);
    }
    m_findTimer->stop();
}

QJsonObject LabControllerDevice::config() const {
    QJsonObject json;
    json.insert(QStringLiteral("plantDeviceId"), m_plantDeviceId);
    json.insert(QStringLiteral("controller"), m_controller.spec.toJson());
    QJsonArray filters;
    for (const Block& block : m_filterBlocks) {
        filters.append(block.spec.toJson());
    }
    json.insert(QStringLiteral("filters"), filters);
    return json;
}

void LabControllerDevice::setPlantDeviceId(const QString& deviceId) {
    if (deviceId == m_plantDeviceId) {
        return;
    }
    m_plantDeviceId = deviceId;
    m_plant = nullptr;
    linkPlant();
    emit configChanged();
}

LabPlantDevice* LabControllerDevice::plant() const {
    return m_plant;
}

LabControllerDevice::Block& LabControllerDevice::block(int index) {
    return index == kController ? m_controller : m_filterBlocks[index];
}

const LabControllerDevice::Block& LabControllerDevice::block(int index) const {
    return index == kController ? m_controller : m_filterBlocks[index];
}

CodeBlockSpec LabControllerDevice::spec(int index) const {
    if (index != kController && (index < 0 || index >= m_filterBlocks.size())) {
        return {};
    }
    return block(index).spec;
}

void LabControllerDevice::setSpec(int index, const CodeBlockSpec& spec) {
    if (index == kController) {
        m_controller.spec = spec;
        loadController();
    } else if (index >= 0 && index < m_filterBlocks.size()) {
        m_filterBlocks[index].spec = spec;
        m_filterSpecs[index] = spec;
        loadFilters();
    } else {
        return;
    }
    watchFiles();
    emit configChanged();
}

void LabControllerDevice::addFilter(const CodeBlockSpec& spec) {
    Block block;
    block.spec = spec;
    m_filterBlocks.append(block);
    m_filterSpecs.append(spec);
    loadFilters();
    watchFiles();
    emit configChanged();
}

void LabControllerDevice::removeFilter(int index) {
    if (index < 0 || index >= m_filterBlocks.size()) {
        return;
    }
    m_filterBlocks.removeAt(index);
    m_filterSpecs.removeAt(index);
    loadFilters();
    watchFiles();
    emit configChanged();
}

void LabControllerDevice::moveFilter(int from, int to) {
    if (from < 0 || from >= m_filterBlocks.size() || to < 0 || to >= m_filterBlocks.size() ||
        from == to) {
        return;
    }
    m_filterBlocks.move(from, to);
    m_filterSpecs.move(from, to);
    loadFilters();
    emit configChanged();
}

void LabControllerDevice::reload(int index) {
    if (index == kController) {
        loadController();
        return;
    }
    if (index < 0 || index >= m_filterBlocks.size()) {
        return;
    }
    // Only this filter: the others keep their state.
    Block& entry = m_filterBlocks[index];
    QString fileName;
    entry.source = loadSource(entry.spec, &fileName);
    auto filter = std::make_unique<CodeSignalFilter>();
    filter->module().setParamOverrides(entry.spec.params);
    const bool ok = filter->load(entry.source, fileName);
    entry.messages = filter->module().messages();
    if (ok) {
        filter->reset();
        std::vector<std::unique_ptr<SignalFilter>> filters = m_host->takeFilters();
        if (index < int(filters.size())) {
            filters[std::size_t(index)] = std::move(filter);
        }
        m_host->setFilters(std::move(filters));
        entry.compiled = true;
    }
    emit blocksChanged();
}

QString LabControllerDevice::loadSource(const CodeBlockSpec& spec, QString* fileName) const {
    if (!spec.path.isEmpty()) {
        *fileName = spec.path;
        QString error;
        const QString text = code::CodeLibrary::readFile(spec.path, &error);
        return text;
    }
    if (!spec.templateId.isEmpty()) {
        *fileName = spec.templateId.section(QLatin1Char('/'), -1) + QStringLiteral(".c");
        return code::CodeLibrary::instance().templateSource(spec.templateId);
    }
    fileName->clear();
    return QString();
}

void LabControllerDevice::loadController() {
    QString fileName;
    m_controller.source = loadSource(m_controller.spec, &fileName);
    if (m_controller.spec.isEmpty()) {
        m_controller.messages.clear();
        m_controller.compiled = false;
        m_host->setAdapter(nullptr);
        emit blocksChanged();
        return;
    }
    if (!m_controller.spec.path.isEmpty() && !QFileInfo::exists(m_controller.spec.path)) {
        m_controller.messages = {
            {0, QCoreApplication::translate("LabControllerDevice", "file not found: %1")
                    .arg(m_controller.spec.path),
             false}};
        m_controller.compiled = false;
        m_host->setAdapter(nullptr);
        emit blocksChanged();
        return;
    }
    auto adapter = std::make_unique<CodeControllerAdapter>();
    adapter->module().setParamOverrides(m_controller.spec.params);
    const bool ok = adapter->load(m_controller.source, fileName);
    m_controller.messages = adapter->module().messages();
    if (ok) {
        adapter->setSampleTime(m_plant ? m_plant->sampleTime() : m_host->sampleTime());
        adapter->reset();
        m_host->setAdapter(std::move(adapter));
        m_controller.compiled = true;
    } else if (!controller()) {
        m_controller.compiled = false;
    }
    emit blocksChanged();
}

void LabControllerDevice::loadFilters() {
    std::vector<std::unique_ptr<SignalFilter>> previous = m_host->takeFilters();
    std::vector<std::unique_ptr<SignalFilter>> filters;
    for (int i = 0; i < m_filterBlocks.size(); ++i) {
        Block& entry = m_filterBlocks[i];
        QString fileName;
        entry.source = loadSource(entry.spec, &fileName);
        auto filter = std::make_unique<CodeSignalFilter>();
        filter->module().setParamOverrides(entry.spec.params);
        entry.compiled = filter->load(entry.source, fileName);
        entry.messages = filter->module().messages();
        if (entry.compiled) {
            filter->reset();
        }
        // One that does not compile stays in the chain: the loop stays open
        // and the card says why.
        filters.push_back(std::move(filter));
    }
    m_host->setFilters(std::move(filters));
    emit blocksChanged();
}

void LabControllerDevice::watchFiles() {
    QStringList wanted;
    if (!m_controller.spec.path.isEmpty()) {
        wanted.append(m_controller.spec.path);
    }
    for (const Block& entry : std::as_const(m_filterBlocks)) {
        if (!entry.spec.path.isEmpty()) {
            wanted.append(entry.spec.path);
        }
    }
    const QStringList watched = m_watcher->files();
    for (const QString& path : watched) {
        if (!wanted.contains(path)) {
            m_watcher->removePath(path);
        }
    }
    for (const QString& path : std::as_const(wanted)) {
        if (!m_watcher->files().contains(path) && QFileInfo::exists(path)) {
            m_watcher->addPath(path);
        }
    }
}

const code::CodeModule* LabControllerDevice::module(int index) const {
    if (index == kController) {
        CodeControllerAdapter* adapter = controller();
        return adapter ? &adapter->module() : nullptr;
    }
    CodeSignalFilter* f = filter(index);
    return f ? &f->module() : nullptr;
}

CodeControllerAdapter* LabControllerDevice::controller() const {
    return dynamic_cast<CodeControllerAdapter*>(m_host->adapter());
}

CodeSignalFilter* LabControllerDevice::filter(int index) const {
    const auto& filters = m_host->filters();
    if (index < 0 || index >= int(filters.size())) {
        return nullptr;
    }
    return dynamic_cast<CodeSignalFilter*>(filters[std::size_t(index)].get());
}

QVector<code::CodeMessage> LabControllerDevice::messages(int index) const {
    if (index != kController && (index < 0 || index >= m_filterBlocks.size())) {
        return {};
    }
    return block(index).messages;
}

bool LabControllerDevice::compiled(int index) const {
    if (index != kController && (index < 0 || index >= m_filterBlocks.size())) {
        return false;
    }
    return block(index).compiled;
}

QString LabControllerDevice::runtimeError(int index) const {
    if (index == kController) {
        CodeControllerAdapter* adapter = controller();
        return adapter ? adapter->runtimeErrorText() : QString();
    }
    CodeSignalFilter* f = filter(index);
    return f ? f->runtimeErrorText() : QString();
}

QString LabControllerDevice::blockName(int index) const {
    const CodeBlockSpec s = spec(index);
    if (!s.path.isEmpty()) {
        return QFileInfo(s.path).completeBaseName();
    }
    if (const code::CodeTemplate* t = code::CodeLibrary::instance().findTemplate(s.templateId)) {
        return t->name;
    }
    return s.templateId;
}

QString LabControllerDevice::source(int index) const {
    if (index != kController && (index < 0 || index >= m_filterBlocks.size())) {
        return QString();
    }
    return block(index).source;
}

code::BlockKind LabControllerDevice::blockKind(int block) const {
    return block == kController ? code::BlockKind::Controller : code::BlockKind::Filter;
}

QString LabControllerDevice::blockRole(int block) const {
    if (block != kController && (block < 0 || block >= filterCount())) {
        return QString();  // no such block (any more)
    }
    return block == kController
               ? QCoreApplication::translate("LabControllerDevice", "Controller")
               : QCoreApplication::translate("LabControllerDevice", "Filter %1 of %2")
                     .arg(block + 1)
                     .arg(filterCount());
}

CodeBlockOwner::Status LabControllerDevice::status(int block) const {
    Q_UNUSED(block);
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("tracelab::LabControllerDevice", text);
    };
    if (!m_plant) {
        return {2, tr("No plant linked.")};
    }
    if (!m_host->plantReady()) {
        return {2, tr("Waiting for the plant...")};
    }
    if (!m_host->configureError().isEmpty()) {
        return {3, m_host->configureError()};
    }
    if (!m_host->configured()) {
        return {2, tr("No controller.")};
    }
    return {1, tr("Loop closed on %1.").arg(m_plant->typeName())};
}

QString LabControllerDevice::note(int block) const {
    CodeControllerAdapter* adapter = block == kController ? controller() : nullptr;
    if (adapter && adapter->usesPreview()) {
        return QCoreApplication::translate("LabControllerDevice",
                                           "Uses the plant's preview of the track (TL_PREVIEW): "
                                           "information a real robot may not have.");
    }
    return QString();
}

QVector<SignalRow> LabControllerDevice::signalRows(int block) const {
    Q_UNUSED(block);
    const auto tr = [](const char* text) {
        return QCoreApplication::translate("tracelab::LabControllerDevice", text);
    };
    QVector<SignalRow> rows;
    appendSignalRows(&rows, tr("From the plant (loop.state)"), m_host->measurementNames(),
                     m_host->lastMeasurements(), QStringLiteral("TL_IN(%1);"),
                     QStringLiteral("TL_IN_ARRAY(%1, %2);"));
    if (filterCount() > 0) {
        appendSignalRows(&rows, tr("After the filters (what the controller reads)"),
                         m_host->signalNames(), m_host->lastSignals(), QStringLiteral("TL_IN(%1);"),
                         QStringLiteral("TL_IN_ARRAY(%1, %2);"));
    }
    appendSignalRows(&rows, tr("Plant inputs (loop.u: the controller writes)"),
                     m_host->inputNames(), {}, QStringLiteral("TL_OUT(%1);"),
                     QStringLiteral("TL_OUT_PLAN(%1, %2);"), false);
    if (rows.isEmpty()) {
        rows.append({tr("No plant linked yet."), QString(), false});
    }
    return rows;
}

QStringList LabControllerDevice::takeLog(int index) {
    if (index == kController) {
        CodeControllerAdapter* adapter = controller();
        return adapter ? adapter->module().takeLog() : QStringList();
    }
    CodeSignalFilter* f = filter(index);
    return f ? f->module().takeLog() : QStringList();
}

void LabControllerDevice::setParam(int index, const QString& name, double value) {
    if (index != kController && (index < 0 || index >= m_filterBlocks.size())) {
        return;
    }
    if (index == kController) {
        if (CodeControllerAdapter* adapter = controller()) {
            adapter->module().setParamValue(name, value);
            value = adapter->module().paramValue(name);
        }
    } else if (CodeSignalFilter* f = filter(index)) {
        f->module().setParamValue(name, value);
        value = f->module().paramValue(name);
    }
    Block& entry = block(index);
    entry.spec.params.insert(name, value);
    if (index != kController) {
        m_filterSpecs[index] = entry.spec;
    }
    emit configChanged();
}

void LabControllerDevice::attachViewer(LoopbackTransport* peer) {
    if (m_viewerLink && m_viewerLink != peer) {
        m_viewerLink->close();
    }
    m_viewerLink = peer;
    if (peer) {
        peer->setParent(this);
    }
    m_host->setViewerTransport(peer);
}

void LabControllerDevice::followPlantClock() {
    const double ts = m_plant ? m_plant->sampleTime() : m_host->sampleTime();
    if (ts <= 0.0) {
        return;
    }
    m_host->setSampleTime(ts);
    if (CodeControllerAdapter* adapter = controller()) {
        adapter->setSampleTime(ts);
    }
}

void LabControllerDevice::linkPlant() {
    LabDeviceRegistry& registry = LabDeviceRegistry::instance();
    LabPlantDevice* plant = registry.plant(m_plantDeviceId);
    if (plant == nullptr && m_plantDeviceId.isEmpty() && !registry.plantIds().isEmpty()) {
        // No plant id configured: the first live plant there is.
        plant = registry.plant(registry.plantIds().first());
    }
    if (plant == m_plant && m_ownEnd && m_ownEnd->isConnected()) {
        return;
    }
    if (m_plant) {
        disconnect(m_plant, nullptr, this, nullptr);
    }
    if (m_ownEnd) {
        m_host->setPlantTransport(nullptr);
        m_ownEnd->close();
        m_ownEnd->deleteLater();
    }
    m_plant = plant;
    emit statusChanged();
    if (!plant) {
        return;
    }
    connect(plant, &LabPlantDevice::settingsChanged, this, &LabControllerDevice::followPlantClock);
    followPlantClock();
    // A link of its own to the plant's controller port: BTP all the way.
    m_ownEnd = new LoopbackTransport(this);
    auto* plantEnd = new LoopbackTransport(plant);
    m_ownEnd->connectTo(plantEnd);
    plant->attachController(plantEnd);
    m_host->setPlantTransport(m_ownEnd);
}

// ---------------------------------------------------------------------------

LabDeviceRegistry& LabDeviceRegistry::instance() {
    static LabDeviceRegistry registry;
    return registry;
}

void LabDeviceRegistry::registerPlantType(const PlantType& type) {
    for (const PlantType& known : std::as_const(m_plantTypes)) {
        if (known.typeId == type.typeId && known.name == type.name) {
            return;
        }
    }
    m_plantTypes.append(type);
}

void LabDeviceRegistry::addOtherController(const QString& deviceId, QObject* device,
                                           const QString& name, const QString& plantDeviceId) {
    m_others.insert(deviceId, {device, name, plantDeviceId});
    connect(device, &QObject::destroyed, this, [this, deviceId, device] {
        if (m_others.value(deviceId).device == device || !m_others.value(deviceId).device) {
            m_others.remove(deviceId);
        }
        emit devicesChanged();
    });
    emit devicesChanged();
}

QStringList LabDeviceRegistry::otherControllerIds() const {
    QStringList ids;
    for (auto it = m_others.cbegin(); it != m_others.cend(); ++it) {
        if (it.value().device) {
            ids.append(it.key());
        }
    }
    ids.sort();
    return ids;
}

LabDeviceRegistry::OtherController LabDeviceRegistry::otherController(
    const QString& deviceId) const {
    return m_others.value(deviceId);
}

LabPlantDevice* LabDeviceRegistry::plant(const QString& deviceId) const {
    return m_plants.value(deviceId);
}

LabControllerDevice* LabDeviceRegistry::controller(const QString& deviceId) const {
    return m_controllers.value(deviceId);
}

QStringList LabDeviceRegistry::plantIds() const {
    QStringList ids;
    for (auto it = m_plants.cbegin(); it != m_plants.cend(); ++it) {
        if (it.value()) {
            ids.append(it.key());
        }
    }
    ids.sort();
    return ids;
}

QStringList LabDeviceRegistry::controllerIds() const {
    QStringList ids;
    for (auto it = m_controllers.cbegin(); it != m_controllers.cend(); ++it) {
        if (it.value()) {
            ids.append(it.key());
        }
    }
    ids.sort();
    return ids;
}

QString LabDeviceRegistry::idOf(const QObject* device) const {
    for (auto it = m_plants.cbegin(); it != m_plants.cend(); ++it) {
        if (it.value() == device) {
            return it.key();
        }
    }
    for (auto it = m_controllers.cbegin(); it != m_controllers.cend(); ++it) {
        if (it.value() == device) {
            return it.key();
        }
    }
    return QString();
}

QStringList LabDeviceRegistry::controllersOf(const QString& plantDeviceId) const {
    QStringList ids;
    for (const QString& id : controllerIds()) {
        LabControllerDevice* c = controller(id);
        if (c && c->plant() == plant(plantDeviceId)) {
            ids.append(id);
        }
    }
    return ids;
}

void LabDeviceRegistry::addPlant(const QString& deviceId, LabPlantDevice* plant) {
    m_plants.insert(deviceId, plant);
    connect(plant, &QObject::destroyed, this, [this, deviceId, plant] {
        if (m_plants.value(deviceId) == plant || !m_plants.value(deviceId)) {
            m_plants.remove(deviceId);
        }
        emit devicesChanged();
    });
    emit devicesChanged();
}

void LabDeviceRegistry::addController(const QString& deviceId, LabControllerDevice* controller) {
    m_controllers.insert(deviceId, controller);
    connect(controller, &QObject::destroyed, this, [this, deviceId, controller] {
        if (m_controllers.value(deviceId) == controller || !m_controllers.value(deviceId)) {
            m_controllers.remove(deviceId);
        }
        emit devicesChanged();
    });
    emit devicesChanged();
}

// ---------------------------------------------------------------------------

traceview::ModuleDeviceType labPlantDeviceType() {
    traceview::ModuleDeviceType type;
    type.id = QString::fromLatin1(kCodePlantDeviceType);
    type.displayName = QCoreApplication::translate("LabDevices", "TraceLab: Plant");
    type.createTransport = [](const QString& deviceId, const QJsonObject& config,
                              QObject* parent) -> traceview::ModuleTransport* {
        auto* device = new CodePlantDevice(
            deviceId, labDeviceSourceId(deviceId, kCodePlantDeviceType), config);
        const bool autoRun = config.value(QStringLiteral("autoRun")).toBool(true);
        auto* transport = new EndpointTransport(
            [device, autoRun](LoopbackTransport* peer, QString*) {
                device->attachViewer(peer);
                if (autoRun && !device->running() && device->tick() == 0) {
                    device->run();
                }
                return true;
            },
            parent);
        device->setParent(transport);
        LabDeviceRegistry::instance().addPlant(deviceId, device);
        return transport;
    };
    return type;
}

traceview::ModuleDeviceType labControllerDeviceType() {
    traceview::ModuleDeviceType type;
    type.id = QString::fromLatin1(kLabControllerDeviceType);
    type.displayName = QCoreApplication::translate("LabDevices", "TraceLab: Controller");
    type.createTransport = [](const QString& deviceId, const QJsonObject& config,
                              QObject* parent) -> traceview::ModuleTransport* {
        auto* device = new LabControllerDevice(deviceId, config);
        auto* transport = new EndpointTransport(
            [device](LoopbackTransport* peer, QString*) {
                device->attachViewer(peer);
                return true;
            },
            parent);
        device->setParent(transport);
        LabDeviceRegistry::instance().addController(deviceId, device);
        return transport;
    };
    return type;
}

}  // namespace tracelab
