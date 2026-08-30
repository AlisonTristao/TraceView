#include <QtTest/QtTest>

#include "diagnostics/hexdump.h"

using namespace traceview;

namespace {

class TestHexDump : public QObject {
    Q_OBJECT

private slots:
    void emptyInputIsEmpty();
    void dumpHasOffsetHexAndAscii();
    void dumpWrapsEverySixteenOctets();
    void inlineTruncatesWithRemainderCount();
};

void TestHexDump::emptyInputIsEmpty() {
    QVERIFY(hexDump(QByteArray()).isEmpty());
    QVERIFY(hexInline(QByteArray()).isEmpty());
}

void TestHexDump::dumpHasOffsetHexAndAscii() {
    const QByteArray data = QByteArrayLiteral("AB\x00\xFF");
    const QString dump = hexDump(data);

    QVERIFY(dump.startsWith(QStringLiteral("00000000  ")));
    QVERIFY(dump.contains(QStringLiteral("41 42 00 ff")));
    // Printable octets render as themselves, others as '.'.
    QVERIFY(dump.contains(QStringLiteral("|AB..|")));
}

void TestHexDump::dumpWrapsEverySixteenOctets() {
    const QByteArray data(20, 'x');
    const QStringList lines = hexDump(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 2);
    QVERIFY(lines.at(1).startsWith(QStringLiteral("00000010  ")));  // second line offset = 16
}

void TestHexDump::inlineTruncatesWithRemainderCount() {
    const QByteArray data(40, char(0xAB));
    const QString inl = hexInline(data, 8);
    QVERIFY(inl.startsWith(QStringLiteral("ab ab ab ab ab ab ab ab")));
    QVERIFY(inl.contains(QStringLiteral("(+32)")));

    // Shorter than the cap: no remainder marker.
    QVERIFY(!hexInline(QByteArray(3, 'z'), 8).contains(QLatin1Char('+')));
}

}  // namespace

QTEST_MAIN(TestHexDump)
#include "test_hexdump.moc"
