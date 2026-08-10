#include <QtTest>

#include <cstring>
#include <limits>

#include "protocol/packedledecoder.h"

using traceview::decodePackedLe;
using traceview::TelemetryEncoding;
using traceview::TelemetryFieldSchema;
using traceview::TelemetryFieldType;
using traceview::TelemetryFieldValue;
using traceview::TelemetryTopicSchema;

namespace {

void appendU8(QByteArray& body, quint8 v) { body.append(char(v)); }
void appendU16(QByteArray& body, quint16 v) {
    body.append(char(v & 0xFF));
    body.append(char((v >> 8) & 0xFF));
}
void appendF32(QByteArray& body, float v) {
    quint32 bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 4; ++i) {
        body.append(char((bits >> (8 * i)) & 0xFF));
    }
}

TelemetryFieldSchema field(quint16 id, quint16 order, const QString& name, TelemetryFieldType type,
                            double scale = 1.0, double offset = 0.0, bool nullable = false) {
    TelemetryFieldSchema f;
    f.fieldId = id;
    f.order = order;
    f.name = name;
    f.type = type;
    f.scale = scale;
    f.offset = offset;
    f.nullable = nullable;
    return f;
}

class TestPackedLeDecoder : public QObject {
    Q_OBJECT

private slots:
    // TELEMETRY.md section 9.1
    void decodesMotorExampleFromSpec();
    // TELEMETRY.md section 9.2 -- nullable field, present and absent
    void decodesImuWithNullableTemperaturePresent();
    void decodesImuWithNullableTemperatureAbsent();
    // TELEMETRY.md section 9.3 -- variable-length array + enum8
    void decodesLineSensorVariableArrayAndEnum();

    void rejectsTruncatedBody();
    void rejectsLeftoverBytes();
    void rejectsNonFiniteFloat();
    void rejectsOversizedVariableArray();
    void rejectsNonZeroUnusedBitmapBits();
    void boolMustBeZeroOrOne();
};

void TestPackedLeDecoder::decodesMotorExampleFromSpec() {
    TelemetryTopicSchema schema;
    schema.sourceId = 0x11223344;
    schema.topicId = 0x0101;
    schema.schemaVersion = 1;
    schema.encoding = TelemetryEncoding::PackedLe;
    schema.fields = {
        field(1, 0, "left_speed", TelemetryFieldType::Float32),
        field(2, 1, "right_speed", TelemetryFieldType::Float32),
        field(3, 2, "left_current", TelemetryFieldType::Int16, 0.01),
        field(4, 3, "right_current", TelemetryFieldType::Int16, 0.01),
    };

    // TELEMETRY.md section 9.1: left_speed=1.5f, right_speed=-2.25f,
    // left_current raw 300 (0x012c), right_current raw -40 (0xffd8) -- the
    // body is the payload's 12 octets after its 2-octet schema_version.
    const QByteArray spec = QByteArray::fromHex("0000c03f" "000010c0" "2c01" "d8ff");

    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(decodePackedLe(schema, spec, &values));
    QVERIFY(qFuzzyCompare(values[1].elements[0], 1.5));
    QVERIFY(qFuzzyCompare(values[2].elements[0], -2.25));
    QVERIFY(qFuzzyCompare(values[3].elements[0], 3.00));
    QVERIFY(qFuzzyCompare(values[4].elements[0], -0.40));
}

void TestPackedLeDecoder::decodesImuWithNullableTemperaturePresent() {
    TelemetryTopicSchema schema;
    schema.fields = {
        field(1, 0, "acceleration", TelemetryFieldType::Float32),
        field(2, 1, "angular_velocity", TelemetryFieldType::Float32),
        field(3, 2, "temperature", TelemetryFieldType::Int16, 0.01, 0.0, /*nullable=*/true),
    };
    schema.fields[0].elementCount = 3;
    schema.fields[1].elementCount = 3;

    QByteArray body;
    appendU8(body, 0x01);  // bitmap: bit0 set -> temperature present
    for (float v : {1.0f, 2.0f, 3.0f}) appendF32(body, v);
    for (float v : {4.0f, 5.0f, 6.0f}) appendF32(body, v);
    appendU16(body, quint16(qint16(1234)));  // 12.34 C raw

    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(decodePackedLe(schema, body, &values));
    QVERIFY(!values[3].isNull);
    QVERIFY(qFuzzyCompare(values[3].elements[0], 12.34));
    QCOMPARE(values[1].elements.size(), 3);
    QCOMPARE(values[2].elements.size(), 3);
}

void TestPackedLeDecoder::decodesImuWithNullableTemperatureAbsent() {
    TelemetryTopicSchema schema;
    schema.fields = {
        field(1, 0, "acceleration", TelemetryFieldType::Float32),
        field(2, 1, "angular_velocity", TelemetryFieldType::Float32),
        field(3, 2, "temperature", TelemetryFieldType::Int16, 0.01, 0.0, /*nullable=*/true),
    };
    schema.fields[0].elementCount = 3;
    schema.fields[1].elementCount = 3;

    QByteArray body;
    appendU8(body, 0x00);  // bitmap: temperature absent
    for (float v : {1.0f, 2.0f, 3.0f}) appendF32(body, v);
    for (float v : {4.0f, 5.0f, 6.0f}) appendF32(body, v);
    // No bytes for temperature -- absent fields consume no body bytes.

    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(decodePackedLe(schema, body, &values));
    QVERIFY(values[3].isNull);
    QVERIFY(values[3].elements.isEmpty());
}

void TestPackedLeDecoder::decodesLineSensorVariableArrayAndEnum() {
    TelemetryTopicSchema schema;
    TelemetryFieldSchema reflectance = field(1, 0, "reflectance", TelemetryFieldType::UInt16, 0.01);
    reflectance.elementCount = 0;
    reflectance.maxElementCount = 32;
    TelemetryFieldSchema centroid = field(2, 1, "centroid", TelemetryFieldType::Int16, 0.001, 0.0, true);
    TelemetryFieldSchema quality = field(3, 2, "quality", TelemetryFieldType::Enum8);
    schema.fields = {reflectance, centroid, quality};

    QByteArray body;
    appendU8(body, 0x01);   // centroid present
    appendU16(body, 3);     // reflectance element_count = 3
    appendU16(body, 100);   // reflectance[0] raw
    appendU16(body, 200);   // reflectance[1] raw
    appendU16(body, 300);   // reflectance[2] raw
    appendU16(body, quint16(qint16(-500)));  // centroid raw
    appendU8(body, 2);      // quality = good

    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(decodePackedLe(schema, body, &values));
    QCOMPARE(values[1].elements.size(), 3);
    QVERIFY(qFuzzyCompare(values[1].elements[0], 1.0));
    QVERIFY(!values[2].isNull);
    QVERIFY(qFuzzyCompare(values[2].elements[0], -0.5));
    QCOMPARE(values[3].elements[0], 2.0);  // enum: raw integer, no scale applied
}

void TestPackedLeDecoder::rejectsTruncatedBody() {
    TelemetryTopicSchema schema;
    schema.fields = {field(1, 0, "v", TelemetryFieldType::UInt32)};
    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(!decodePackedLe(schema, QByteArray::fromHex("0102"), &values));  // needs 4 bytes, has 2
}

void TestPackedLeDecoder::rejectsLeftoverBytes() {
    TelemetryTopicSchema schema;
    schema.fields = {field(1, 0, "v", TelemetryFieldType::UInt8)};
    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(!decodePackedLe(schema, QByteArray::fromHex("0102"), &values));  // 1 extra byte
}

void TestPackedLeDecoder::rejectsNonFiniteFloat() {
    TelemetryTopicSchema schema;
    schema.fields = {field(1, 0, "v", TelemetryFieldType::Float32)};
    QByteArray body;
    appendF32(body, std::numeric_limits<float>::infinity());
    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(!decodePackedLe(schema, body, &values));
}

void TestPackedLeDecoder::rejectsOversizedVariableArray() {
    TelemetryTopicSchema schema;
    TelemetryFieldSchema f = field(1, 0, "v", TelemetryFieldType::UInt8);
    f.elementCount = 0;
    f.maxElementCount = 2;
    schema.fields = {f};

    QByteArray body;
    appendU16(body, 3);  // exceeds maxElementCount=2
    appendU8(body, 1);
    appendU8(body, 2);
    appendU8(body, 3);

    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(!decodePackedLe(schema, body, &values));
}

void TestPackedLeDecoder::rejectsNonZeroUnusedBitmapBits() {
    TelemetryTopicSchema schema;
    schema.fields = {field(1, 0, "a", TelemetryFieldType::UInt8, 1.0, 0.0, true)};

    QByteArray body;
    appendU8(body, 0xFE);  // bit0 (the only assigned bit) clear -> field null;
                            // bits 1-7 MUST be zero but aren't here.
    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(!decodePackedLe(schema, body, &values));
}

void TestPackedLeDecoder::boolMustBeZeroOrOne() {
    TelemetryTopicSchema schema;
    schema.fields = {field(1, 0, "b", TelemetryFieldType::Bool)};
    QHash<quint16, TelemetryFieldValue> values;
    QVERIFY(!decodePackedLe(schema, QByteArray::fromHex("02"), &values));
    QVERIFY(decodePackedLe(schema, QByteArray::fromHex("01"), &values));
    QCOMPARE(values[1].elements[0], 1.0);
}

} // namespace

QTEST_MAIN(TestPackedLeDecoder)
#include "test_packedledecoder.moc"
