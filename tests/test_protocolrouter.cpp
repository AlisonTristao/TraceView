#include <QtTest>

#include "protocol/protocolrouter.h"

using traceview::BtpFrame;
using traceview::ProtocolRouter;
using traceview::TelemetrySample;

namespace {

BtpFrame makeFrame(btp::MessageType type, const QByteArray& payload) {
    BtpFrame frame;
    frame.type = type;
    frame.sourceId = 0x11223344;
    frame.bootId = 0xA1B2C3D4;
    frame.sequence = 7;
    frame.timestampUs = 123456789;
    frame.objectId = 0x0101;
    frame.fragmentIndex = 0;
    frame.fragmentCount = 1;
    frame.payload = payload;
    return frame;
}

class TestProtocolRouter : public QObject {
    Q_OBJECT

private slots:
    void telemetrySplitsSchemaVersionFromPayload();
    void telemetryPreservesEmbeddedZeroBytesUntruncated();
    void telemetryTooShortForSchemaVersionIsDropped();
    void logCommandTerminalControlForwardUnchanged();
    void invalidTypeIsCountedAndNotForwarded();
};

void TestProtocolRouter::telemetrySplitsSchemaVersionFromPayload() {
    ProtocolRouter router;
    TelemetrySample received;
    bool got = false;
    connect(&router, &ProtocolRouter::telemetrySampleReceived, &router, [&](const TelemetrySample& sample) {
        received = sample;
        got = true;
    });

    // schema_version = 1 (little-endian 01 00), body = "AB" (0x41 0x42).
    const BtpFrame frame = makeFrame(btp::MessageType::Telemetry, QByteArray::fromHex("0100") + "AB");
    router.onFrameReceived(frame);

    QVERIFY(got);
    QCOMPARE(received.sourceId, quint32(0x11223344));
    QCOMPARE(received.bootId, quint32(0xA1B2C3D4));
    QCOMPARE(received.sequence, quint32(7));
    QCOMPARE(received.timestampUs, quint64(123456789));
    QCOMPARE(received.topicId, quint16(0x0101));  // envelope object_id
    QCOMPARE(received.schemaVersion, quint16(1));
    QCOMPARE(received.payload, QByteArrayLiteral("AB"));
    QCOMPARE(router.diagnostics().telemetryRouted, quint64(1));
}

void TestProtocolRouter::telemetryPreservesEmbeddedZeroBytesUntruncated() {
    ProtocolRouter router;
    TelemetrySample received;
    connect(&router, &ProtocolRouter::telemetrySampleReceived, &router,
            [&](const TelemetrySample& sample) { received = sample; });

    // CRITERIO DE ACEITE: "payload binario com zero nao e truncado". The
    // body deliberately contains 0x00, 0x0A (LF) and 0x0D (CR), matching the
    // Marco 2 requirement in PLANO_GERAL.txt.
    QByteArray payload = QByteArray::fromHex("0100");  // schema_version = 1
    payload += QByteArray("\x00\x0A\x0D\xFF\x00\x01", 6);
    const BtpFrame frame = makeFrame(btp::MessageType::Telemetry, payload);
    router.onFrameReceived(frame);

    QCOMPARE(received.payload.size(), 6);
    QCOMPARE(received.payload, QByteArray("\x00\x0A\x0D\xFF\x00\x01", 6));
}

void TestProtocolRouter::telemetryTooShortForSchemaVersionIsDropped() {
    ProtocolRouter router;
    bool got = false;
    connect(&router, &ProtocolRouter::telemetrySampleReceived, &router,
            [&](const TelemetrySample&) { got = true; });

    router.onFrameReceived(makeFrame(btp::MessageType::Telemetry, QByteArray::fromHex("01")));  // 1 byte only

    QVERIFY(!got);
    QCOMPARE(router.diagnostics().telemetryDropped, quint64(1));
}

void TestProtocolRouter::logCommandTerminalControlForwardUnchanged() {
    ProtocolRouter router;
    int logCount = 0, commandCount = 0, terminalCount = 0, controlCount = 0;
    connect(&router, &ProtocolRouter::logFrameReceived, &router, [&](const BtpFrame&) { ++logCount; });
    connect(&router, &ProtocolRouter::commandFrameReceived, &router, [&](const BtpFrame&) { ++commandCount; });
    connect(&router, &ProtocolRouter::terminalFrameReceived, &router, [&](const BtpFrame&) { ++terminalCount; });
    connect(&router, &ProtocolRouter::controlFrameReceived, &router, [&](const BtpFrame&) { ++controlCount; });

    router.onFrameReceived(makeFrame(btp::MessageType::Log, "hello"));
    router.onFrameReceived(makeFrame(btp::MessageType::Command, "cmd"));
    router.onFrameReceived(makeFrame(btp::MessageType::Terminal, "term"));
    router.onFrameReceived(makeFrame(btp::MessageType::Control, "ctl"));

    QCOMPARE(logCount, 1);
    QCOMPARE(commandCount, 1);
    QCOMPARE(terminalCount, 1);
    QCOMPARE(controlCount, 1);
}

void TestProtocolRouter::invalidTypeIsCountedAndNotForwarded() {
    ProtocolRouter router;
    router.onFrameReceived(makeFrame(btp::MessageType::Invalid, QByteArray()));
    QCOMPARE(router.diagnostics().unknownTypeDropped, quint64(1));
}

} // namespace

QTEST_MAIN(TestProtocolRouter)
#include "test_protocolrouter.moc"
