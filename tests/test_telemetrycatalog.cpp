#include <QtTest>

#include "protocol/telemetrycatalog.h"

using traceview::registerBallySoftwareCatalog;
using traceview::TelemetryCatalog;
using traceview::TelemetryEncoding;
using traceview::TelemetryFieldType;
using traceview::TelemetryTopicSchema;

namespace {

class TestTelemetryCatalog : public QObject {
    Q_OBJECT

private slots:
    void lookupMissesWhenNothingRegistered();
    void registerThenLookupRoundTrips();
    void differentSchemaVersionsAreDistinctEntries();
    void reRegisteringSameKeyReplaces();
    void ballySoftwareCatalogHasBothDocumentedTopics();
    void fieldByIdFindsAndMisses();
};

void TestTelemetryCatalog::lookupMissesWhenNothingRegistered() {
    TelemetryCatalog catalog;
    QVERIFY(catalog.lookup(1, 1, 1) == nullptr);
}

void TestTelemetryCatalog::registerThenLookupRoundTrips() {
    TelemetryCatalog catalog;
    TelemetryTopicSchema schema;
    schema.sourceId = 0x11223344;
    schema.topicId = 0x0101;
    schema.schemaVersion = 1;
    schema.name = "motor_state";
    schema.encoding = TelemetryEncoding::PackedLe;
    catalog.registerSchema(schema);

    const TelemetryTopicSchema* found = catalog.lookup(0x11223344, 0x0101, 1);
    QVERIFY(found != nullptr);
    QCOMPARE(found->name, QStringLiteral("motor_state"));

    // Wrong source_id, topic_id, or schema_version must all miss -- the
    // catalog key is the full triple (telemetry.md section 1).
    QVERIFY(catalog.lookup(0x99999999, 0x0101, 1) == nullptr);
    QVERIFY(catalog.lookup(0x11223344, 0x9999, 1) == nullptr);
    QVERIFY(catalog.lookup(0x11223344, 0x0101, 2) == nullptr);
}

void TestTelemetryCatalog::differentSchemaVersionsAreDistinctEntries() {
    TelemetryCatalog catalog;
    TelemetryTopicSchema v1;
    v1.sourceId = 1;
    v1.topicId = 1;
    v1.schemaVersion = 1;
    v1.name = "v1";
    TelemetryTopicSchema v2 = v1;
    v2.schemaVersion = 2;
    v2.name = "v2";

    catalog.registerSchema(v1);
    catalog.registerSchema(v2);

    QCOMPARE(catalog.lookup(1, 1, 1)->name, QStringLiteral("v1"));
    QCOMPARE(catalog.lookup(1, 1, 2)->name, QStringLiteral("v2"));
}

void TestTelemetryCatalog::reRegisteringSameKeyReplaces() {
    TelemetryCatalog catalog;
    TelemetryTopicSchema first;
    first.sourceId = 1;
    first.topicId = 1;
    first.schemaVersion = 1;
    first.name = "first";
    catalog.registerSchema(first);

    TelemetryTopicSchema second = first;
    second.name = "second";
    catalog.registerSchema(second);

    QCOMPARE(catalog.lookup(1, 1, 1)->name, QStringLiteral("second"));
}

void TestTelemetryCatalog::ballySoftwareCatalogHasBothDocumentedTopics() {
    TelemetryCatalog catalog;
    registerBallySoftwareCatalog(catalog, 0x11223344);

    const TelemetryTopicSchema* protocolTest = catalog.lookup(0x11223344, 0x0001, 1);
    QVERIFY(protocolTest != nullptr);
    QCOMPARE(protocolTest->name, QStringLiteral("protocol.test"));
    QCOMPARE(protocolTest->fields.size(), 2);
    QCOMPARE(protocolTest->fieldById(1)->type, TelemetryFieldType::UInt32);
    QCOMPARE(protocolTest->fieldById(2)->type, TelemetryFieldType::Float32);

    const TelemetryTopicSchema* robotState = catalog.lookup(0x11223344, 0x0002, 1);
    QVERIFY(robotState != nullptr);
    QCOMPARE(robotState->name, QStringLiteral("robot.state"));
    QCOMPARE(robotState->fields.size(), 1);
    QCOMPARE(robotState->fieldById(1)->type, TelemetryFieldType::UInt8);
}

void TestTelemetryCatalog::fieldByIdFindsAndMisses() {
    TelemetryCatalog catalog;
    registerBallySoftwareCatalog(catalog, 1);
    const TelemetryTopicSchema* schema = catalog.lookup(1, 0x0001, 1);
    QVERIFY(schema->fieldById(1) != nullptr);
    QVERIFY(schema->fieldById(999) == nullptr);
}

}  // namespace

QTEST_MAIN(TestTelemetryCatalog)
#include "test_telemetrycatalog.moc"
