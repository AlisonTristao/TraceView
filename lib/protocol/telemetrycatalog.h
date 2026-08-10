#pragma once

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace traceview {

// Wire-identical to TELEMETRY.md section 5's type codes -- kept as its own
// client-side enum (rather than a raw uint8) so schema code reads by name
// instead of by magic number.
enum class TelemetryFieldType : quint8 {
    UInt8 = 0x01,
    UInt16 = 0x02,
    UInt32 = 0x03,
    UInt64 = 0x04,
    Int8 = 0x05,
    Int16 = 0x06,
    Int32 = 0x07,
    Int64 = 0x08,
    Float32 = 0x09,
    Float64 = 0x0A,
    Bool = 0x0B,
    Enum8 = 0x0C,
    Enum16 = 0x0D,
};

// Width in bytes of one element of `type`.
int telemetryFieldTypeWidth(TelemetryFieldType type);

// TELEMETRY.md section 4 -- PACKED_LE is what production schemas use
// (PLANO_GERAL.txt decision 7); the others are declared for completeness of
// the catalog model, but only PACKED_LE has a decoder in this topico (see
// packedledecoder.h). CSV_UTF8/JSON_UTF8/TLV_LE/OPAQUE_BYTES/UTF8 decoders
// are added when a producer actually needs them, rather than speculatively
// now.
enum class TelemetryEncoding : quint8 {
    Invalid = 0x00,
    OpaqueBytes = 0x01,
    Utf8 = 0x02,
    JsonUtf8 = 0x03,
    CsvUtf8 = 0x04,
    PackedLe = 0x05,
    TlvLe = 0x06,
};

// One structured field of a topic schema (TELEMETRY.md section 3). Field
// identity is `fieldId`, never `order` or `name` -- see TELEMETRY.md section
// 8 ("um binding MUST NOT ... migrar por nome ou posicao").
struct TelemetryFieldSchema {
    quint16 fieldId = 0;
    QString name;
    quint16 order = 0;
    TelemetryFieldType type = TelemetryFieldType::Float32;
    QString unit = QStringLiteral("1");
    double scale = 1.0;
    double offset = 0.0;
    quint16 elementCount = 1;     // 1 = scalar; >1 = fixed-size array
    quint16 maxElementCount = 0;  // set (with elementCount == 0) for a
                                   // variable-count array
    bool nullable = false;

    bool isVariableLength() const { return elementCount == 0 && maxElementCount > 0; }
};

// One (source_id, topic_id, schema_version) schema -- the full identity
// TELEMETRY.md section 2 requires before decoding a sample's body.
struct TelemetryTopicSchema {
    quint32 sourceId = 0;
    quint16 topicId = 0;
    quint16 schemaVersion = 0;
    QString name;
    TelemetryEncoding encoding = TelemetryEncoding::PackedLe;
    QVector<TelemetryFieldSchema> fields;  // need not be pre-sorted by
                                            // `order`; decodePackedLe sorts
                                            // its own working copy.

    const TelemetryFieldSchema* fieldById(quint16 fieldId) const;
};

// Source/topic/schema registry, deliberately independent of DashboardWidget
// or any other UI type (topico 14 PASSO 9) -- a chart widget asks this "what
// does field 3 of topic 0x0101 from source 0x11223344 mean," it never
// hardcodes byte offsets or types itself. In the running app, entries are
// populated dynamically from the wire (ManifestClient, topico 16) --
// registerBallySoftwareCatalog() below is kept only as a test/tool fixture
// (see tests/test_telemetrycatalog.cpp, tests/test_telemetryfieldrouter.cpp,
// tools/chart_preview), not called by MainWindow anymore.
class TelemetryCatalog {
public:
    // Replaces any existing schema with the same (sourceId, topicId,
    // schemaVersion) -- schema_version is supposed to be immutable per
    // TELEMETRY.md section 2 once published, but re-registering during
    // development/tests is deliberately last-write-wins rather than
    // asserting.
    void registerSchema(const TelemetryTopicSchema& schema);

    const TelemetryTopicSchema* lookup(quint32 sourceId, quint16 topicId, quint16 schemaVersion) const;

    // Topico 17: SubscriptionManager needs the source's boot_id to address a
    // SUBSCRIBE/UNSUBSCRIBE's target_boot_id (COMMANDS_AND_ACTIONS.md section
    // 7 marks it non-zero, unlike MANIFEST_REQUEST's target_boot_id which
    // accepts zero). Kept as a source-level fact independent of any one
    // schema_version/topic_id (registerSchema()'s key includes
    // schema_version, which a subscribing widget does not know) --
    // ManifestClient populates this from MANIFEST_DATA's described_boot_id
    // every time it applies a response for that source, so this always
    // reflects the boot the catalog's currently-registered schemas actually
    // came from.
    void registerSourceBootId(quint32 sourceId, quint32 bootId);
    // Returns 0 if this source's boot_id is not known yet (no MANIFEST_DATA
    // applied for it in this process).
    quint32 sourceBootId(quint32 sourceId) const;

    void clear();

private:
    static quint64 makeKey(quint32 sourceId, quint16 topicId, quint16 schemaVersion);

    QHash<quint64, TelemetryTopicSchema> m_schemas;
    QHash<quint32, quint32> m_sourceBootIds;
};

// Registers the two schemas bally_software already produces (TELEMETRY.md
// section 9.4: protocol.test and robot.state, both schema_version 1,
// PACKED_LE) under `sourceId` -- a test/tool fixture only (topico 16 built
// the real, dynamic equivalent: bally_software's ManifestResponder announces
// these same two schemas over the wire, and TraceView's ManifestClient
// builds the matching catalog entries from that MANIFEST_DATA response).
void registerBallySoftwareCatalog(TelemetryCatalog& catalog, quint32 sourceId);

}  // namespace traceview
