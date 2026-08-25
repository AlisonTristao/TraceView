#include <QTemporaryFile>
#include <QtTest>
#include <btp/codec.hpp>
#include <btp/fragmentation.hpp>

#include "protocol/logfilereader.h"

using traceview::LogEntry;
using traceview::LogFileReader;
using traceview::LogSeverity;

namespace {

// Encodes `payload` as one or more BTP v1 Log frames -- fragmenting against
// the EspNow profile exactly like Logger::flush_logs_to does in bally_OS --
// and appends the raw bytes to `out`. Mirrors what ends up on a real .blog
// file for one logical log record.
void appendLogMessage(QByteArray& out, quint32 sourceId, quint32 bootId, quint32 sequence,
                      quint64 timestampUs, quint8 severity, const QByteArray& payload) {
    btp::Header logicalHeader{};
    logicalHeader.type = btp::MessageType::Log;
    logicalHeader.flags = 0;
    logicalHeader.source_id = sourceId;
    logicalHeader.boot_id = bootId;
    logicalHeader.sequence = sequence;
    logicalHeader.timestamp_us = timestampUs;
    logicalHeader.object_id = severity;

    std::uint8_t count = 0;
    QCOMPARE(
        btp::fragment_count(std::size_t(payload.size()), btp::TransportProfile::EspNow, &count),
        btp::Error::Ok);

    const btp::ByteView view{reinterpret_cast<const std::uint8_t*>(payload.constData()),
                             std::size_t(payload.size())};
    for (std::uint8_t index = 0; index < count; ++index) {
        btp::Frame fragment;
        QCOMPARE(btp::make_fragment(logicalHeader, view, btp::TransportProfile::EspNow, index,
                                    &fragment),
                 btp::Error::Ok);

        std::uint8_t buffer[btp::kEspNowMaxFrameSize];
        std::size_t bytesWritten = 0;
        QCOMPARE(btp::encode(fragment, btp::TransportProfile::EspNow, buffer, sizeof(buffer),
                             &bytesWritten),
                 btp::Error::Ok);
        out.append(reinterpret_cast<const char*>(buffer), int(bytesWritten));
    }
}

class TestLogFileReader : public QObject {
    Q_OBJECT

private slots:
    void singleFrameMessageParsesOneEntry();
    void fragmentedMessageReassemblesInOrder();
    void multipleRecordsParseInFileOrder();
    void corruptedCrcFrameIsSkippedButOthersStillParse();
    void missingFileFailsToLoad();
};

void TestLogFileReader::singleFrameMessageParsesOneEntry() {
    QByteArray fileBytes;
    appendLogMessage(fileBytes, 0x11223344, 0xA1B2C3D4, 7, 123456789, quint8(LogSeverity::Info),
                     "hello world");

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(fileBytes);
    file.close();

    LogFileReader reader;
    QVERIFY(reader.load(file.fileName()));
    const QVector<LogEntry> entries = reader.entries();
    QCOMPARE(entries.size(), 1);
    const LogEntry& entry = entries.first();
    QCOMPARE(entry.timestampUs, quint64(123456789));
    QCOMPARE(entry.sourceId, quint32(0x11223344));
    QCOMPARE(entry.bootId, quint32(0xA1B2C3D4));
    QCOMPARE(entry.sequence, quint32(7));
    QCOMPARE(entry.severity, LogSeverity::Info);
    QCOMPARE(entry.message, QStringLiteral("hello world"));
    QCOMPARE(reader.skippedFrameCount(), 0);
}

void TestLogFileReader::fragmentedMessageReassemblesInOrder() {
    QByteArray longPayload;
    for (int i = 0; i < 30; ++i) {
        longPayload += "0123456789";
    }
    QCOMPARE(longPayload.size(), 300);  // > kEspNowMaxPayloadSize (210) -- forces 2 fragments

    QByteArray fileBytes;
    appendLogMessage(fileBytes, 0x11223344, 0xA1B2C3D4, 42, 999, quint8(LogSeverity::Warn),
                     longPayload);

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(fileBytes);
    file.close();

    LogFileReader reader;
    QVERIFY(reader.load(file.fileName()));
    const QVector<LogEntry> entries = reader.entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().message, QString::fromUtf8(longPayload));
    QCOMPARE(entries.first().severity, LogSeverity::Warn);
    QCOMPARE(reader.skippedFrameCount(), 0);
}

void TestLogFileReader::multipleRecordsParseInFileOrder() {
    QByteArray fileBytes;
    appendLogMessage(fileBytes, 1, 2, 1, 100, quint8(LogSeverity::Info), "first");
    appendLogMessage(fileBytes, 1, 2, 2, 200, quint8(LogSeverity::Error), "second");

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(fileBytes);
    file.close();

    LogFileReader reader;
    QVERIFY(reader.load(file.fileName()));
    const QVector<LogEntry> entries = reader.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).message, QStringLiteral("first"));
    QCOMPARE(entries.at(1).message, QStringLiteral("second"));
    QCOMPARE(entries.at(1).severity, LogSeverity::Error);
}

void TestLogFileReader::corruptedCrcFrameIsSkippedButOthersStillParse() {
    QByteArray fileBytes;
    appendLogMessage(fileBytes, 1, 2, 1, 100, quint8(LogSeverity::Info), "good-before");

    const int corruptFrameStart = fileBytes.size();
    appendLogMessage(fileBytes, 1, 2, 2, 200, quint8(LogSeverity::Error), "corrupted");
    // Flip the corrupted record's first payload byte without recomputing its
    // CRC, so btp::decode reports CrcMismatch for this frame only -- framing
    // (the payload_size field just before it) is untouched, so the reader
    // still finds the next frame's correct offset.
    fileBytes[corruptFrameStart + int(btp::kV1HeaderSize)] ^= 0xFF;

    appendLogMessage(fileBytes, 1, 2, 3, 300, quint8(LogSeverity::Info), "good-after");

    QTemporaryFile file;
    QVERIFY(file.open());
    file.write(fileBytes);
    file.close();

    LogFileReader reader;
    QVERIFY(reader.load(file.fileName()));
    const QVector<LogEntry> entries = reader.entries();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).message, QStringLiteral("good-before"));
    QCOMPARE(entries.at(1).message, QStringLiteral("good-after"));
    QCOMPARE(reader.skippedFrameCount(), 1);
}

void TestLogFileReader::missingFileFailsToLoad() {
    LogFileReader reader;
    QVERIFY(!reader.load(QStringLiteral("this/path/does/not/exist.blog")));
    QVERIFY(!reader.lastError().isEmpty());
}

}  // namespace

QTEST_MAIN(TestLogFileReader)
#include "test_logfilereader.moc"
