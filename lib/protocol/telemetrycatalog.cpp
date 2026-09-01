#include "protocol/telemetrycatalog.h"

#include <algorithm>

namespace traceview {

const TelemetryFieldSchema* TelemetryTopicSchema::fieldById(quint16 fieldId) const {
    for (const TelemetryFieldSchema& field : fields) {
        if (field.fieldId == fieldId) {
            return &field;
        }
    }
    return nullptr;
}

const QVector<btp::FieldSpec>& TelemetryTopicSchema::orderedFieldSpecs() const {
    if (m_orderedSpecsBuilt) {
        return m_orderedSpecs;
    }
    m_orderedSpecsBuilt = true;
    m_orderedSpecs.clear();

    QVector<const TelemetryFieldSchema*> ordered;
    ordered.reserve(fields.size());
    for (const TelemetryFieldSchema& field : fields) {
        ordered.append(&field);
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const TelemetryFieldSchema* a, const TelemetryFieldSchema* b) {
                         return a->order < b->order;
                     });

    m_orderedSpecs.reserve(ordered.size());
    for (int i = 0; i < ordered.size(); ++i) {
        if (ordered[i]->order != quint16(i)) {
            m_orderedSpecs.clear();  // not contiguous from zero -- leave empty
            return m_orderedSpecs;
        }
        btp::FieldSpec spec{};
        spec.field_id = ordered[i]->fieldId;
        spec.order = ordered[i]->order;
        spec.type = quint8(ordered[i]->type);
        spec.flags = quint8(ordered[i]->nullable ? btp::kFieldNullable : 0) |
                     quint8(ordered[i]->isVariableLength() ? btp::kFieldVariableCount : 0);
        spec.element_count = ordered[i]->elementCount;
        spec.max_element_count = ordered[i]->maxElementCount;
        spec.scale = ordered[i]->scale;
        spec.offset = ordered[i]->offset;
        m_orderedSpecs.append(spec);
    }
    return m_orderedSpecs;
}

quint64 TelemetryCatalog::makeKey(quint32 sourceId, quint16 topicId, quint16 schemaVersion) {
    return (quint64(sourceId) << 32) | (quint64(topicId) << 16) | quint64(schemaVersion);
}

void TelemetryCatalog::registerSchema(const TelemetryTopicSchema& schema) {
    m_schemas.insert(makeKey(schema.sourceId, schema.topicId, schema.schemaVersion), schema);
}

const TelemetryTopicSchema* TelemetryCatalog::lookup(quint32 sourceId, quint16 topicId,
                                                     quint16 schemaVersion) const {
    const auto it = m_schemas.constFind(makeKey(sourceId, topicId, schemaVersion));
    return it == m_schemas.constEnd() ? nullptr : &it.value();
}

QVector<TelemetryTopicSchema> TelemetryCatalog::allSchemas() const {
    return m_schemas.values();
}

void TelemetryCatalog::registerSourceBootId(quint32 sourceId, quint32 bootId) {
    if (sourceId == 0) {
        return;
    }
    m_sourceBootIds.insert(sourceId, bootId);
}

quint32 TelemetryCatalog::sourceBootId(quint32 sourceId) const {
    return m_sourceBootIds.value(sourceId, 0);
}

void TelemetryCatalog::clear() {
    m_schemas.clear();
    m_sourceBootIds.clear();
}

void registerBallySoftwareCatalog(TelemetryCatalog& catalog, quint32 sourceId) {
    TelemetryTopicSchema protocolTest;
    protocolTest.sourceId = sourceId;
    protocolTest.topicId = 0x0001;
    protocolTest.schemaVersion = 1;
    protocolTest.name = QStringLiteral("protocol.test");
    protocolTest.encoding = TelemetryEncoding::PackedLe;
    {
        TelemetryFieldSchema counter;
        counter.fieldId = 1;
        counter.order = 0;
        counter.name = QStringLiteral("counter");
        counter.type = TelemetryFieldType::UInt32;
        counter.unit = QStringLiteral("1");
        protocolTest.fields.append(counter);

        TelemetryFieldSchema value;
        value.fieldId = 2;
        value.order = 1;
        value.name = QStringLiteral("value");
        value.type = TelemetryFieldType::Float32;
        value.unit = QStringLiteral("1");
        protocolTest.fields.append(value);
    }
    catalog.registerSchema(protocolTest);

    TelemetryTopicSchema robotState;
    robotState.sourceId = sourceId;
    robotState.topicId = 0x0002;
    robotState.schemaVersion = 1;
    robotState.name = QStringLiteral("robot.state");
    robotState.encoding = TelemetryEncoding::PackedLe;
    {
        TelemetryFieldSchema state;
        state.fieldId = 1;
        state.order = 0;
        state.name = QStringLiteral("state");
        state.type = TelemetryFieldType::UInt8;
        state.unit = QStringLiteral("1");
        robotState.fields.append(state);
    }
    catalog.registerSchema(robotState);
}

}  // namespace traceview
