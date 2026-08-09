#include <QtTest>

#include "core/serialprotocol.h"

using traceview::decodeFrame;
using traceview::SerialFrame;
using traceview::SerialLineAssembler;

namespace {

class TestSerialProtocol : public QObject {
    Q_OBJECT

private slots:
    void decodesWellFormedFrame();
    void decodesFrameWithEmptyPayload();
    void rejectsMissingBrackets();
    void rejectsNonNumericOrNegativeTime();
    void rejectsMissingSpaceBeforePayload();
    void rejectsEmptyOrOversizedId();
    void rejectsIdWithInvalidCharacters();

    void assemblerSplitsOnLf();
    void assemblerStripsCrBeforeLf();
    void assemblerKeepsLoneCrAsLiteral();
    void assemblerBuffersFragmentedLine();
    void assemblerHandlesMultipleLinesInOneChunk();
    void assemblerResetDiscardsPartialLine();
};

void TestSerialProtocol::decodesWellFormedFrame() {
    const SerialFrame frame = decodeFrame("[1234][temp1] 23.5");

    QVERIFY(frame.ok);
    QCOMPARE(frame.time, qint64(1234));
    QCOMPARE(frame.id, QStringLiteral("temp1"));
    QCOMPARE(frame.payload, QByteArrayLiteral("23.5"));
}

void TestSerialProtocol::decodesFrameWithEmptyPayload() {
    const SerialFrame frame = decodeFrame("[0][a] ");

    QVERIFY(frame.ok);
    QCOMPARE(frame.time, qint64(0));
    QCOMPARE(frame.id, QStringLiteral("a"));
    QVERIFY(frame.payload.isEmpty());
}

void TestSerialProtocol::rejectsMissingBrackets() {
    QVERIFY(!decodeFrame("1234][temp1] 23.5").ok);
    QVERIFY(!decodeFrame("[1234]temp1] 23.5").ok);
    QVERIFY(!decodeFrame("[1234][temp1 23.5").ok);
    QVERIFY(!decodeFrame("").ok);
}

void TestSerialProtocol::rejectsNonNumericOrNegativeTime() {
    QVERIFY(!decodeFrame("[abc][temp1] 23.5").ok);
    QVERIFY(!decodeFrame("[-5][temp1] 23.5").ok);
    QVERIFY(!decodeFrame("[][temp1] 23.5").ok);
}

void TestSerialProtocol::rejectsMissingSpaceBeforePayload() {
    QVERIFY(!decodeFrame("[1234][temp1]23.5").ok);
    QVERIFY(!decodeFrame("[1234][temp1]").ok);
}

void TestSerialProtocol::rejectsEmptyOrOversizedId() {
    QVERIFY(!decodeFrame("[1234][] 23.5").ok);

    const QByteArray longId(65, 'a');
    QVERIFY(!decodeFrame("[1234][" + longId + "] 23.5").ok);

    const QByteArray maxId(64, 'a');
    QVERIFY(decodeFrame("[1234][" + maxId + "] 23.5").ok);
}

void TestSerialProtocol::rejectsIdWithInvalidCharacters() {
    QVERIFY(!decodeFrame("[1234][temp 1] 23.5").ok);
    QVERIFY(!decodeFrame("[1234][temp.1] 23.5").ok);
    QVERIFY(decodeFrame("[1234][temp_1-A] 23.5").ok);
}

void TestSerialProtocol::assemblerSplitsOnLf() {
    SerialLineAssembler assembler;
    const QList<QByteArray> lines = assembler.feed("[1][a] x\n[2][b] y\n");

    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QByteArrayLiteral("[1][a] x"));
    QCOMPARE(lines.at(1), QByteArrayLiteral("[2][b] y"));
}

void TestSerialProtocol::assemblerStripsCrBeforeLf() {
    SerialLineAssembler assembler;
    const QList<QByteArray> lines = assembler.feed("[1][a] x\r\n");

    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("[1][a] x"));
}

void TestSerialProtocol::assemblerKeepsLoneCrAsLiteral() {
    SerialLineAssembler assembler;
    const QList<QByteArray> lines = assembler.feed("abc\rdef\n");

    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("abc\rdef"));
}

void TestSerialProtocol::assemblerBuffersFragmentedLine() {
    SerialLineAssembler assembler;

    QVERIFY(assembler.feed("[1][a").isEmpty());
    QVERIFY(assembler.feed("bc").isEmpty());
    const QList<QByteArray> lines = assembler.feed("] x\n");

    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("[1][abc] x"));
}

void TestSerialProtocol::assemblerHandlesMultipleLinesInOneChunk() {
    SerialLineAssembler assembler;
    const QList<QByteArray> lines = assembler.feed("one\ntwo\nthree");

    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QByteArrayLiteral("one"));
    QCOMPARE(lines.at(1), QByteArrayLiteral("two"));

    const QList<QByteArray> rest = assembler.feed("\n");
    QCOMPARE(rest.size(), 1);
    QCOMPARE(rest.at(0), QByteArrayLiteral("three"));
}

void TestSerialProtocol::assemblerResetDiscardsPartialLine() {
    SerialLineAssembler assembler;
    QVERIFY(assembler.feed("[1][a").isEmpty());

    assembler.reset();

    const QList<QByteArray> lines = assembler.feed("bc] x\n");
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("bc] x"));
}

} // namespace

QTEST_MAIN(TestSerialProtocol)
#include "test_serialprotocol.moc"
