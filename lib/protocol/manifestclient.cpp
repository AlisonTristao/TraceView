#include "protocol/manifestclient.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <btp/codec.hpp>
#include <cstring>

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

void appendLe32(QByteArray& out, quint32 value) {
    out.append(static_cast<char>(value));
    out.append(static_cast<char>(value >> 8));
    out.append(static_cast<char>(value >> 16));
    out.append(static_cast<char>(value >> 24));
}

// Bounds-checked little-endian cursor over one MANIFEST_DATA payload
// (commands.md section 3.2). Every read advances `pos` only on
// success, so a caller can bail out on the first `false` without unwinding
// anything by hand.
struct Cursor {
    const QByteArray& data;
    int pos = 0;

    bool skip(int n) {
        if (n < 0 || pos + n > data.size())
            return false;
        pos += n;
        return true;
    }
    bool u8(quint8* out) {
        if (pos + 1 > data.size())
            return false;
        *out = quint8(data.at(pos));
        pos += 1;
        return true;
    }
    bool u16(quint16* out) {
        if (pos + 2 > data.size())
            return false;
        *out = quint16(quint8(data.at(pos))) | (quint16(quint8(data.at(pos + 1))) << 8);
        pos += 2;
        return true;
    }
    bool u32(quint32* out) {
        if (pos + 4 > data.size())
            return false;
        quint32 value = 0;
        for (int i = 0; i < 4; ++i) value |= quint32(quint8(data.at(pos + i))) << (8 * i);
        *out = value;
        pos += 4;
        return true;
    }
    bool f64(double* out) {
        if (pos + 8 > data.size())
            return false;
        std::memcpy(out, data.constData() + pos, sizeof(double));  // host is little-endian
        pos += 8;
        return true;
    }
    bool utf8(QString* out) {
        quint16 len = 0;
        if (!u16(&len))
            return false;
        if (pos + len > data.size())
            return false;
        *out = QString::fromUtf8(data.constData() + pos, len);
        pos += len;
        return true;
    }
};

// Parses one field record (commands.md section 3.3's field record)
// starting at `cursor.pos`, which must be the record's own
// `record_size` prefix. Enum entries are not modeled by TelemetryFieldSchema
// yet (topico 16 does not need them to decode PACKED_LE samples), so this
// jumps straight to the record's end afterward rather than parsing them --
// exactly what `record_size` framing is for, and forward compatible with
// any future field appended after `description`.
bool parseFieldRecord(Cursor& cursor, TelemetryFieldSchema* outField) {
    quint32 recordSize = 0;
    if (!cursor.u32(&recordSize))
        return false;
    const int recordEnd = cursor.pos + int(recordSize);
    if (recordSize > 0x7FFFFFFFU || recordEnd > cursor.data.size())
        return false;

    quint16 fieldId = 0, order = 0, elementCount = 0, maxElementCount = 0, enumCount = 0;
    quint8 type = 0, flags = 0;
    double scale = 1.0, offset = 0.0;
    QString name, unit, description;
    if (!cursor.u16(&fieldId) || !cursor.u16(&order) || !cursor.u8(&type) || !cursor.u8(&flags) ||
        !cursor.u16(&elementCount) || !cursor.u16(&maxElementCount) || !cursor.f64(&scale) ||
        !cursor.f64(&offset) || !cursor.u16(&enumCount) || !cursor.utf8(&name) ||
        !cursor.utf8(&unit) || !cursor.utf8(&description)) {
        return false;
    }
    if (cursor.pos > recordEnd)
        return false;
    cursor.pos = recordEnd;  // skip enum entries / any unknown trailing bytes

    // TELEMETRY.md section 5 type codes; TelemetryFieldType's own values are
    // defined identically (see telemetrycatalog.h), so this is a direct,
    // bounds-checked cast rather than a lookup table.
    if (type < 0x01 || type > 0x0D)
        return false;

    outField->fieldId = fieldId;
    outField->order = order;
    outField->type = static_cast<TelemetryFieldType>(type);
    outField->name = name;
    outField->unit = unit;
    outField->scale = scale;
    outField->offset = offset;
    outField->elementCount = elementCount;
    outField->maxElementCount = maxElementCount;
    outField->nullable = (flags & 0x01) != 0;
    return true;
}

bool parseTopicRecord(Cursor& cursor, quint32 sourceId, TelemetryTopicSchema* outTopic) {
    quint32 recordSize = 0;
    if (!cursor.u32(&recordSize))
        return false;
    const int recordEnd = cursor.pos + int(recordSize);
    if (recordSize > 0x7FFFFFFFU || recordEnd > cursor.data.size())
        return false;

    quint16 topicId = 0, schemaVersion = 0, fieldCount = 0;
    quint8 encoding = 0, flags = 0;
    quint32 maxRateMillihz = 0;
    QString name, description;
    if (!cursor.u16(&topicId) || !cursor.u16(&schemaVersion) || !cursor.u8(&encoding) ||
        !cursor.u8(&flags) || !cursor.u16(&fieldCount) || !cursor.u32(&maxRateMillihz) ||
        !cursor.utf8(&name) || !cursor.utf8(&description)) {
        return false;
    }
    if (topicId == 0 || schemaVersion == 0)
        return false;

    TelemetryTopicSchema topic;
    topic.sourceId = sourceId;
    topic.topicId = topicId;
    topic.schemaVersion = schemaVersion;
    topic.name = name;
    topic.encoding = static_cast<TelemetryEncoding>(encoding);
    topic.fields.reserve(fieldCount);
    for (quint16 i = 0; i < fieldCount; ++i) {
        TelemetryFieldSchema field;
        if (!parseFieldRecord(cursor, &field))
            return false;
        topic.fields.append(field);
    }
    if (cursor.pos > recordEnd)
        return false;
    cursor.pos = recordEnd;

    *outTopic = topic;
    return true;
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

bool parseManifestData(const QByteArray& payload, ParsedManifestData* out) {
    if (payload.size() < 60)
        return false;

    Cursor cursor{payload, 0};
    if (!cursor.skip(12))
        return false;  // request-reference: correlation not needed, see class comment

    quint8 status = 0, flags = 0;
    quint16 errorCode = 0, formatVersion = 0, reserved = 0;
    quint32 configRevision = 0;
    if (!cursor.u8(&status) || !cursor.u8(&flags) || !cursor.u16(&errorCode) ||
        !cursor.u16(&formatVersion) || !cursor.u16(&reserved) || !cursor.u32(&configRevision)) {
        return false;
    }
    if (formatVersion != 1 && formatVersion != 2)
        return false;  // format 1, or 2 which adds the source_info block (commands.md 3.12)
    if (!cursor.skip(16))
        return false;  // source_uuid: not modeled by TelemetryCatalog

    quint32 describedSourceId = 0, describedBootId = 0;
    if (!cursor.u32(&describedSourceId) || !cursor.u32(&describedBootId))
        return false;

    quint8 role = 0, sourceFlags = 0;
    quint16 catalogIndex = 0, catalogCount = 0, topicCount = 0, actionCount = 0;
    if (!cursor.u8(&role) || !cursor.u8(&sourceFlags) || !cursor.u16(&catalogIndex) ||
        !cursor.u16(&catalogCount) || !cursor.u16(&topicCount) || !cursor.u16(&actionCount)) {
        return false;
    }

    QString name;
    if (!cursor.utf8(&name))
        return false;

    ParsedManifestData result;
    result.status = status;
    result.flags = flags;
    result.configRevision = configRevision;
    result.describedSourceId = describedSourceId;
    result.describedBootId = describedBootId;

    // source_info block (commands.md 3.12): sits between source_name and the
    // records in format 2, and is present on every SUCCESS response (full or
    // NOT_MODIFIED) plus error ones with a zero count. Parsed unconditionally
    // -- it is not gated by NOT_MODIFIED the way the topic records below are.
    if (formatVersion >= 2) {
        quint16 infoCount = 0;
        if (!cursor.u16(&infoCount))
            return false;
        result.sourceInfo.reserve(infoCount);
        for (quint16 i = 0; i < infoCount; ++i) {
            DeviceInfoRecord record;
            if (!cursor.utf8(&record.key) || !cursor.utf8(&record.label) ||
                !cursor.utf8(&record.value)) {
                return false;
            }
            result.sourceInfo.append(record);
        }
    }

    if (status == kStatusSuccess && (flags & kFlagNotModified) == 0) {
        for (quint16 i = 0; i < topicCount; ++i) {
            TelemetryTopicSchema topic;
            if (!parseTopicRecord(cursor, describedSourceId, &topic))
                return false;
            result.topics.append(topic);
        }
        // Action records: skip via the same record_size framing. Not
        // modeled yet (topico 18's territory) -- CRITERIO DE ACEITE here
        // only concerns telemetry topics/fields.
        for (quint16 i = 0; i < actionCount; ++i) {
            quint32 recordSize = 0;
            if (!cursor.u32(&recordSize) || recordSize > 0x7FFFFFFFU ||
                !cursor.skip(int(recordSize))) {
                return false;
            }
        }
    }

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
    QByteArray payload;
    payload.reserve(12);
    appendLe32(payload, targetSourceId);
    appendLe32(payload, targetBootId);
    appendLe32(payload, knownRevision);

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
