#include "protocol/manifestclient.h"

#include <QDateTime>
#include <btp/messages.hpp>
#include <btp/node.hpp>

#include <cstdint>

#include "protocol/manifeststore.h"
#include "protocol/telemetrycatalog.h"

namespace traceview {

namespace {

constexpr qint64 kUnknownSchemaRequestCooldownMs = 3000;

// TELEMETRY.md section 5 field type codes. TelemetryFieldType's own values
// are defined identically (see telemetrycatalog.h).
bool valid_field_type(quint8 type) {
    return type >= 0x01 && type <= 0x0D;
}

QString toQString(const btp::ByteView& view) {
    return QString::fromUtf8(reinterpret_cast<const char*>(view.data), static_cast<int>(view.size));
}

// The source_info block (commands.md 3.12) of `payload`; format 2+ only,
// present on a full response and a NOT_MODIFIED one alike. Empty on any
// parse failure -- source_info is informational, never worth failing over.
QVector<DeviceInfoRecord> parseSourceInfo(const btp::ByteView& payload) {
    QVector<DeviceInfoRecord> out;
    btp::ManifestReader reader(payload.data, payload.size);
    btp::ManifestHeader header{};
    if (reader.header(&header) != btp::MessageError::Ok) {
        return out;
    }
    btp::SourceInfoEntry info{};
    for (auto step = reader.next_source_info(&info); step == btp::ManifestStep::Item;
         step = reader.next_source_info(&info)) {
        DeviceInfoRecord record;
        record.key = toQString(info.key);
        record.label = toQString(info.label);
        record.value = toQString(info.value);
        out.append(record);
    }
    if (reader.error() != btp::MessageError::Ok) {
        return {};
    }
    return out;
}

// The topic records of a COMPLETE manifest (a full response, or the cached
// copy of one). The whole payload must parse -- a record running past its
// size, an unknown field type, trailing garbage -- or nothing is returned, so
// a malformed manifest is never half-applied. A NOT_MODIFIED payload (no
// topics to give) sets *complete false and returns nothing.
bool parseTopics(const btp::ByteView& payload, QVector<TelemetryTopicSchema>* out,
                 bool* complete) {
    *complete = false;
    btp::ManifestReader reader(payload.data, payload.size);
    btp::ManifestHeader header{};
    if (reader.header(&header) != btp::MessageError::Ok ||
        header.status != static_cast<std::uint8_t>(btp::ResultStatus::Success)) {
        return false;
    }
    if ((header.flags & btp::kManifestNotModified) != 0U) {
        return true;  // valid, just nothing to register
    }

    QVector<TelemetryTopicSchema> topics;
    btp::TopicRecord topicRec{};
    btp::ByteView fieldBytes{};
    for (auto step = reader.next_topic(&topicRec, &fieldBytes); step == btp::ManifestStep::Item;
         step = reader.next_topic(&topicRec, &fieldBytes)) {
        TelemetryTopicSchema topic;
        topic.sourceId = header.described_source_id;
        topic.topicId = topicRec.topic_id;
        topic.schemaVersion = topicRec.schema_version;
        topic.name = toQString(topicRec.name);
        topic.encoding = static_cast<TelemetryEncoding>(topicRec.encoding);
        topic.fields.reserve(topicRec.field_count);

        btp::FieldRecordReader fields(fieldBytes, topicRec.field_count,
                                     header.manifest_format_version);
        btp::FieldRecord fieldRec{};
        btp::ByteView enumBytes{};
        for (auto fstep = fields.next(&fieldRec, &enumBytes); fstep == btp::ManifestStep::Item;
             fstep = fields.next(&fieldRec, &enumBytes)) {
            if (!valid_field_type(fieldRec.type)) {
                return false;
            }
            TelemetryFieldSchema field;
            field.fieldId = fieldRec.field_id;
            field.order = fieldRec.order;
            field.type = static_cast<TelemetryFieldType>(fieldRec.type);
            field.name = toQString(fieldRec.name);
            field.unit = toQString(fieldRec.unit);
            field.scale = fieldRec.scale;
            field.offset = fieldRec.offset;
            field.elementCount = fieldRec.element_count;
            field.maxElementCount = fieldRec.max_element_count;
            field.nullable = (fieldRec.flags & 0x01) != 0;
            // manifest_format_version < 3 (or a field that declared no
            // range): BTP's reader already leaves these at NaN.
            field.minValue = fieldRec.min_value;
            field.maxValue = fieldRec.max_value;
            topic.fields.append(field);
        }
        if (fields.error() != btp::MessageError::Ok) {
            return false;
        }
        topics.append(topic);
    }
    // finish() also requires the payload consumed exactly (action records,
    // which this client does not model yet, are skipped by it).
    if (reader.error() != btp::MessageError::Ok || reader.finish() != btp::MessageError::Ok) {
        return false;
    }
    *out = topics;
    *complete = true;
    return true;
}

void manifestThunk(void* ctx, btp::Node& /*node*/, const btp::NodeManifest& manifest) {
    static_cast<ManifestClient*>(ctx)->applyManifest(manifest);
}

}  // namespace

ManifestClient::ManifestClient(btp::Node& node, TelemetryCatalog* catalog, QObject* parent)
    : QObject(parent), m_node(node), m_catalog(catalog) {
    m_node.on_manifest(&manifestThunk, this);
}

ManifestClient::~ManifestClient() {
    m_node.on_manifest(nullptr, nullptr);
}

void ManifestClient::onSessionEstablished(quint32 peerConfigRevision) {
    if (m_haveDongleConfigRevision && m_lastDongleConfigRevision == peerConfigRevision) {
        // Same dongle catalog as last session in this process -- the existing
        // TelemetryCatalog contents are still valid, skip the re-enumeration.
        return;
    }
    m_haveDongleConfigRevision = true;
    m_lastDongleConfigRevision = peerConfigRevision;
    requestFullCatalog();
}

void ManifestClient::onUnknownSchema(quint32 sourceId, quint16 /*topicId*/,
                                     quint16 /*schemaVersion*/) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = m_lastRequestMsBySource.value(sourceId, 0);
    if (now - last < kUnknownSchemaRequestCooldownMs) {
        return;  // already asked recently; wait for a reply before asking again
    }
    m_lastRequestMsBySource.insert(sourceId, now);
    // With the cached revision: the far end either confirms NOT_MODIFIED
    // (the sample really does use a schema_version nobody published -- not
    // just a stale cache) or sends the real update.
    requestCatalogFor(sourceId);
}

void ManifestClient::onManifestRejected(const btp::NodeManifestOutcome& outcome) {
    if (outcome.correlated) {
        m_lastRequestMsBySource.remove(outcome.target_source_id);
    }
}

void ManifestClient::requestFullCatalog() {
    m_node.request_manifest(/*target_source_id=*/0, /*target_boot_id=*/0,
                            /*known_config_revision=*/0);
}

void ManifestClient::requestCatalogFor(quint32 sourceId) {
    if (sourceId == 0) {
        // Zero is the enumeration wildcard on the wire, so accepting it here
        // would silently turn "ask this robot" into "ask about everything" --
        // which a robot cannot answer.
        return;
    }
    m_node.set_manifest_skip_on_hello(ManifestStore::instance().skipOnHello());
    m_node.request_manifest_cached(sourceId, /*target_boot_id=*/0);
}

bool ManifestClient::primeFromCache(quint32 sourceId) {
    QVector<TelemetryTopicSchema> topics;
    if (!readCached(sourceId, &topics, nullptr)) {
        return false;
    }
    for (const TelemetryTopicSchema& topic : topics) {
        m_catalog->registerSchema(topic);
    }
    emit catalogUpdated();
    return true;
}

bool ManifestClient::readCached(quint32 sourceId, QVector<TelemetryTopicSchema>* topics,
                                QVector<DeviceInfoRecord>* info) {
    ManifestStore& store = ManifestStore::instance();
    if (sourceId == 0 || !store.enabled()) {
        return false;
    }
    const QByteArray* cached = store.find(sourceId);
    if (cached == nullptr) {
        return false;
    }
    const btp::ByteView view{reinterpret_cast<const std::uint8_t*>(cached->constData()),
                             static_cast<std::size_t>(cached->size())};
    QVector<TelemetryTopicSchema> parsed;
    bool complete = false;
    if (!parseTopics(view, &parsed, &complete) || !complete || parsed.isEmpty() ||
        parsed.first().sourceId != sourceId) {
        return false;
    }
    if (topics != nullptr) {
        *topics = parsed;
    }
    if (info != nullptr) {
        *info = parseSourceInfo(view);
    }
    return true;
}

quint32 ManifestClient::cachedSourceNamed(const QString& name) {
    const QString wanted = name.trimmed();
    ManifestStore& store = ManifestStore::instance();
    if (wanted.isEmpty() || !store.enabled()) {
        return 0;
    }
    quint32 found = 0;
    for (const quint32 sourceId : store.sourceIds()) {
        QVector<DeviceInfoRecord> info;
        if (!readCached(sourceId, nullptr, &info)) {
            continue;
        }
        for (const DeviceInfoRecord& record : info) {
            if (record.key == QLatin1String("name") &&
                record.value.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
                if (found != 0 && found != sourceId) {
                    return 0;  // two robots share the name -- not ours to pick
                }
                found = sourceId;
            }
        }
    }
    return found;
}

void ManifestClient::applyManifest(const btp::NodeManifest& manifest) {
    const btp::ManifestHeader& header = manifest.header;

    QVector<TelemetryTopicSchema> topics;
    bool complete = false;
    if (!parseTopics(manifest.topics_payload, &topics, &complete)) {
        return;  // malformed; nothing applied, not even the boot
    }

    const quint32 source = header.described_source_id;
    // The boot this source is currently on, whether or not its catalog
    // changed -- SUBSCRIBE addresses a (source, boot) pair, and this answer is
    // the only carrier of that fact for a source reached through a hub.
    m_catalog->registerSourceBootId(source, header.described_boot_id);

    const QVector<DeviceInfoRecord> info = parseSourceInfo(manifest.payload);
    if (!info.isEmpty()) {
        emit sourceInfoReported(source, info);
    }

    for (const TelemetryTopicSchema& topic : topics) {
        m_catalog->registerSchema(topic);
    }
    emit sourceDescribed(source, header.described_boot_id, header.config_revision);
    emit catalogUpdated();
}

}  // namespace traceview
