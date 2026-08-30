#include <QtTest/QtTest>
#include <btp/codec.hpp>

#include "diagnostics/framelog.h"
#include "protocol/btpframe.h"

using namespace traceview;

namespace {

BtpFrame makeFrame(btp::MessageType type, quint32 sequence = 1) {
    BtpFrame frame;
    frame.type = type;
    frame.sourceId = 0x11223344;
    frame.sequence = sequence;
    frame.payload = QByteArrayLiteral("payload");
    return frame;
}

class TestFrameLog : public QObject {
    Q_OBJECT

private slots:
    void recordFrameAssignsMonotonicSeqAndStamps();
    void recordDecodeErrorIsFlaggedAndInbound();
    void ringDropsOldestPastCapacityAndSeqKeepsClimbing();
    void clearEmptiesAndEmits();
};

void TestFrameLog::recordFrameAssignsMonotonicSeqAndStamps() {
    FrameLog log;
    QSignalSpy spy(&log, &FrameLog::entryAdded);

    log.recordFrame(FrameDirection::Outbound, QStringLiteral("dev-1"), QStringLiteral("Robot 1"),
                    makeFrame(btp::MessageType::Control));
    log.recordFrame(FrameDirection::Inbound, QStringLiteral("dev-1"), QStringLiteral("Robot 1"),
                    makeFrame(btp::MessageType::Telemetry));

    QCOMPARE(log.entries().size(), 2);
    QCOMPARE(log.entries().at(0).seq, quint64(1));
    QCOMPARE(log.entries().at(1).seq, quint64(2));
    QCOMPARE(log.entries().at(0).direction, FrameDirection::Outbound);
    QCOMPARE(log.entries().at(1).frame.type, btp::MessageType::Telemetry);
    QVERIFY(log.entries().at(0).wallClock.isValid());
    QVERIFY(!log.entries().at(0).decodeError);
    QCOMPARE(spy.count(), 2);
}

void TestFrameLog::recordDecodeErrorIsFlaggedAndInbound() {
    FrameLog log;
    log.recordDecodeError(QStringLiteral("dev-1"), QStringLiteral("Robot 1"),
                          QStringLiteral("CRC mismatch"));

    QCOMPARE(log.entries().size(), 1);
    const FrameLogEntry& e = log.entries().first();
    QVERIFY(e.decodeError);
    QCOMPARE(e.errorText, QStringLiteral("CRC mismatch"));
    QCOMPARE(e.direction, FrameDirection::Inbound);
}

void TestFrameLog::ringDropsOldestPastCapacityAndSeqKeepsClimbing() {
    FrameLog log;
    const int extra = 10;
    for (int i = 0; i < FrameLog::kCapacity + extra; ++i) {
        log.recordFrame(FrameDirection::Inbound, QStringLiteral("dev"), QStringLiteral("Dev"),
                        makeFrame(btp::MessageType::Log, quint32(i)));
    }

    QCOMPARE(log.entries().size(), FrameLog::kCapacity);
    // seq is never reused: the oldest survivor is the (extra+1)-th frame ever
    // recorded, seq == extra + 1.
    QCOMPARE(log.entries().first().seq, quint64(extra + 1));
    QCOMPARE(log.entries().last().seq, quint64(FrameLog::kCapacity + extra));
}

void TestFrameLog::clearEmptiesAndEmits() {
    FrameLog log;
    QSignalSpy spy(&log, &FrameLog::cleared);

    log.clear();
    QCOMPARE(spy.count(), 0);

    log.recordFrame(FrameDirection::Inbound, QStringLiteral("d"), QStringLiteral("D"),
                    makeFrame(btp::MessageType::Control));
    log.clear();
    QCOMPARE(spy.count(), 1);
    QVERIFY(log.entries().isEmpty());
}

}  // namespace

QTEST_MAIN(TestFrameLog)
#include "test_framelog.moc"
