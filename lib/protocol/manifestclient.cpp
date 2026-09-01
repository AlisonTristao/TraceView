#include "protocol/manifestclient.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <btp/codec.hpp>
#include <btp/messages.hpp>

#include <cstdint>

#include "protocol/btpframe.h"
#include "protocol/btpsession.h"
#include "protocol/protocolrouter.h"
#include "protocol/telemetrycatalog.h"

namespace traceview {

namespace {

constexpr quint16 kControlManifestRequest = 0x0003;
constexpr quint16 kControlManifestData = 0x0004;
constexpr quint8 kStatusSuccess = 0x00;
constexpr quint8 kFlagNotModified = 0x01;
constexpr qint64 kUnknownSchemaRequestCooldownMs = 3000;

// TELEMETRY.md section 5 field type codes. TelemetryFieldType's own values
// are defined identically (see telemetrycatalog.h).
bool valid_field_type(quint8 type) {
    return type >= 0x01 && type <= 0x0D;
}

struct ParsedManifestData {
    quint8 status = 0;
    quint8 flags = 0;
    quint32 configRevision = 0;
    quint32 describedSourceId = 0;
    // topico 17: SUBSCRIBE's target_boot_id MUST be non-zero
    // (commands.md section 4), and this is the only place the
    // client ever learns which boot a source is currently on.
    quint32 describedBootId = 0;
    QVector<TelemetryTopicSchema> topics;
    // source_info block (commands.md 3.12), format 2 only. Present on a full
    // response and on NOT_MODIFIED alike.
    QVector<DeviceInfoRecord> sourceInfo;
};

QString toQString(const btp::ByteView& view) {
    return QString::fromUtf8(reinterpret_cast<const char*>(view.data), static_cast<int>(view.size));
}

bool parseManifestData(const QByteArray& payload, ParsedManifestData* out) {
    // The MANIFEST_DATA layout (commands.md section 3) is btp::ManifestReader:
    // the fixed head + source_name, the status/role/format validation and the
    // section-6 count limits in header(); the source_info block, the topic
    // records and their field records walked record by record; finish()
    // requires the payload to be consumed exactly (action records, which this
    // client does not model yet, are skipped by it).
    btp::ManifestReader reader(reinterpret_cast<const std::uint8_t*>(payload.constData()),
                               static_cast<std::size_t>(payload.size()));
    btp::ManifestHeader header{};
    if (reader.header(&header) != btp::MessageError::Ok)
        return false;

    ParsedManifestData result;
    result.status = header.status;
    result.flags = header.flags;
    result.configRevision = header.config_revision;
    result.describedSourceId = header.described_source_id;
    result.describedBootId = header.described_boot_id;

    // source_info block (commands.md 3.12): format 2 only, present on a full
    // response and a NOT_MODIFIED one alike -- not gated by NOT_MODIFIED the
    // way the topic records below are. next_source_info() returns End straight
    // away for a format-1 payload.
    btp::SourceInfoEntry info{};
    for (auto step = reader.next_source_info(&info); step == btp::ManifestStep::Item;
         step = reader.next_source_info(&info)) {
        DeviceInfoRecord record;
        record.key = toQString(info.key);
        record.label = toQString(info.label);
        record.value = toQString(info.value);
        result.sourceInfo.append(record);
    }
    if (reader.error() != btp::MessageError::Ok)
        return false;

    if (header.status == kStatusSuccess && (header.flags & kFlagNotModified) == 0) {
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

            btp::FieldRecordReader fields(fieldBytes, topicRec.field_count);
            btp::FieldRecord fieldRec{};
            btp::ByteView enumBytes{};
            for (auto fstep = fields.next(&fieldRec, &enumBytes); fstep == btp::ManifestStep::Item;
                 fstep = fields.next(&fieldRec, &enumBytes)) {
                if (!valid_field_type(fieldRec.type))
                    return false;
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
                topic.fields.append(field);
            }
            if (fields.error() != btp::MessageError::Ok)
                return false;

            result.topics.append(topic);
        }
        if (reader.error() != btp::MessageError::Ok)
            return false;
    }

    if (reader.finish() != btp::MessageError::Ok)
        return false;

    *out = result;
    return true;
}

}  // namespace

ManifestClient::ManifestClient(BtpSession* session, ProtocolRouter* router,
                               TelemetryCatalog* catalog, QObject* parent)
    : QObject(parent), m_session(session), m_catalog(catalog) {
    connect(router, &ProtocolRouter::controlFrameReceived, this,
            &ManifestClient::onControlFrameReceived);
    // Private per-process identity, same construction SerialWidgetBridge
    // uses for TERMINAL_IN (topico 19) -- see class comment for why this
    // does not need to match BtpHandshake's own HELLO identity.
    m_clientSourceId = QRandomGenerator::global()->generate() | 1u;
    m_clientBootId = QRandomGenerator::global()->generate() | 1u;
}

void ManifestClient::onSessionEstablished(quint32 peerConfigRevision) {
    if (m_haveDongleConfigRevision && m_lastDongleConfigRevision == peerConfigRevision) {
        // Same dongle catalog as last session in this process -- the
        // existing TelemetryCatalog contents are still valid (MainWindow
        // never clears m_telemetryCatalog on reconnect), so skip a redundant
        // full re-enumeration.
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

    // Carry whatever revision we already have cached for this source (0 if
    // none): the dongle either confirms NOT_MODIFIED (meaning the sample
    // really does use a schema_version this catalog has never seen -- not
    // just a stale cache) or sends the real update.
    const quint32 knownRevision = m_sourceRevisions.value(sourceId, 0);
    sendRequest(sourceId, /*targetBootId=*/0, knownRevision);
}

void ManifestClient::requestFullCatalog() {
    sendRequest(/*targetSourceId=*/0, /*targetBootId=*/0, /*knownRevision=*/0);
}

void ManifestClient::requestCatalogFor(quint32 sourceId) {
    if (sourceId == 0) {
        // Zero is the enumeration wildcard on the wire, so accepting it here
        // would silently turn "ask this robot" into "ask about everything" --
        // which a robot cannot answer, leaving a child with no catalog and no
        // error to explain why.
        return;
    }
    sendRequest(sourceId, /*targetBootId=*/0, /*knownRevision=*/0);
}

void ManifestClient::sendRequest(quint32 targetSourceId, quint32 targetBootId,
                                 quint32 knownRevision) {
    btp::ManifestRequest request{};
    request.target_source_id = targetSourceId;
    request.target_boot_id = targetBootId;
    request.known_config_revision = knownRevision;

    std::uint8_t buffer[12];
    std::size_t written = 0;
    if (btp::encode_manifest_request(request, buffer, sizeof(buffer), &written) !=
        btp::MessageError::Ok) {
        return;
    }
    const QByteArray payload(reinterpret_cast<const char*>(buffer), static_cast<int>(written));

    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = m_clientSourceId;
    header.boot_id = m_clientBootId;
    header.sequence = m_nextSequence++;
    header.timestamp_us = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000ULL;
    header.object_id = kControlManifestRequest;
    header.fragment_index = 0;
    header.fragment_count = 1;

    btp::Frame frame{};
    frame.header = header;
    frame.payload.data = reinterpret_cast<const std::uint8_t*>(payload.constData());
    frame.payload.size = static_cast<std::size_t>(payload.size());
    m_session->sendFrame(frame);
}

void ManifestClient::onControlFrameReceived(const BtpFrame& frame) {
    if (frame.objectId != kControlManifestData) {
        return;
    }

    ParsedManifestData parsed;
    if (!parseManifestData(frame.payload, &parsed)) {
        return;  // malformed/unsupported MANIFEST_DATA; nothing to apply
    }

    if (parsed.status != kStatusSuccess) {
        // NOT_FOUND/STALE_TARGET_BOOT/UNSUPPORTED: nothing to cache. Clear
        // the cooldown so a legitimate later retry (e.g. once that source
        // actually shows up) is not blocked by this failed attempt.
        m_lastRequestMsBySource.remove(parsed.describedSourceId);
        return;
    }

    // The boot this source is currently on, whether or not its catalog
    // changed -- topico 17's SUBSCRIBE addresses a (source, boot) pair, and
    // this response is the only carrier of that fact.
    m_catalog->registerSourceBootId(parsed.describedSourceId, parsed.describedBootId);

    // source_info rides both a full response and a NOT_MODIFIED one; only
    // surface it when the device actually published something.
    if (!parsed.sourceInfo.isEmpty()) {
        emit sourceInfoReported(parsed.describedSourceId, parsed.sourceInfo);
    }

    if ((parsed.flags & kFlagNotModified) != 0) {
        m_sourceRevisions.insert(parsed.describedSourceId, parsed.configRevision);
        emit sourceDescribed(parsed.describedSourceId, parsed.describedBootId, parsed.configRevision);
        emit catalogUpdated();  // the boot_id above may be new even when the schemas are not
        return;
    }

    for (const TelemetryTopicSchema& topic : parsed.topics) {
        m_catalog->registerSchema(topic);
    }
    m_sourceRevisions.insert(parsed.describedSourceId, parsed.configRevision);
    emit sourceDescribed(parsed.describedSourceId, parsed.describedBootId, parsed.configRevision);
    emit catalogUpdated();
}

}  // namespace traceview
