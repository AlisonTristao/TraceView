#include "protocol/telemetrycatalog.h"

namespace traceview {

int telemetryFieldTypeWidth(TelemetryFieldType type) {
    switch (type) {
        case TelemetryFieldType::UInt8:
        case TelemetryFieldType::Int8:
        case TelemetryFieldType::Bool:
        case TelemetryFieldType::Enum8:
            return 1;
        case TelemetryFieldType::UInt16:
        case TelemetryFieldType::Int16:
        case TelemetryFieldType::Enum16:
            return 2;
        case TelemetryFieldType::UInt32:
        case TelemetryFieldType::Int32:
        case TelemetryFieldType::Float32:
            return 4;
        case TelemetryFieldType::UInt64:
        case TelemetryFieldType::Int64:
        case TelemetryFieldType::Float64:
            return 8;
    }
    return 0;
}

const TelemetryFieldSchema* TelemetryTopicSchema::fieldById(quint16 fieldId) const {
    for (const TelemetryFieldSchema& field : fields) {
        if (field.fieldId == fieldId) {
            return &field;
        }
    }
    return nullptr;
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

void TelemetryCatalog::clear() {
    m_schemas.clear();
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
