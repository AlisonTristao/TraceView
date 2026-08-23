#include <QtTest>

#include "ota/mdnsresolver.h"

using traceview::MdnsResolver;
using traceview::mdns_detail::buildQuery;
using traceview::mdns_detail::parseAAnswers;

namespace {

// Appends one length-prefixed DNS label, e.g. appendLabel(out, "local") ->
// [5]"local". Used to hand-build response messages the same way a real
// mDNS responder would lay them out on the wire.
void appendLabel(QByteArray& out, const QByteArray& label) {
    out.append(char(label.size()));
    out.append(label);
}

// Builds one mDNS response message with a single, plainly-encoded A-record
// answer for `name` ("robot1.local") -> `address` (a big-endian IPv4, e.g.
// 0xC0A80105 for 192.168.1.5) and no question section -- the shape an
// unsolicited periodic mDNS announcement takes. The name-compression case
// (a real reply to our own query, where the answer's NAME is a pointer back
// into the question section) is built by hand in
// parseAAnswersFollowsCompressionPointer() below instead, since it needs a
// real question section for the pointer to point at.
QByteArray buildAnswerMessage(const QString& name, quint32 address) {
    QByteArray message(12, char(0));
    message[7] = char(1);  // ANCOUNT = 1 -- header's QDCOUNT(4-5) stays 0, no question section

    for (const QString& label : name.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
        appendLabel(message, label.toLatin1());
    }
    message.append(char(0));

    message.append(char(0));
    message.append(char(1));  // TYPE = A
    message.append(char(0));
    message.append(char(1));  // CLASS = IN
    message.append(QByteArray(4, char(0)));  // TTL, value irrelevant to parseAAnswers
    message.append(char(0));
    message.append(char(4));  // RDLENGTH = 4
    message.append(char((address >> 24) & 0xFF));
    message.append(char((address >> 16) & 0xFF));
    message.append(char((address >> 8) & 0xFF));
    message.append(char(address & 0xFF));
    return message;
}

class TestMdnsResolver : public QObject {
    Q_OBJECT

private slots:
    void buildQueryEncodesHeaderAndQName();
    void isMdnsHostnameOnlyMatchesDotLocal();
    void parseAAnswersFindsPlainEncodedRecord();
    void parseAAnswersFollowsCompressionPointer();
    void parseAAnswersIgnoresNonARecords();
    void parseAAnswersReturnsEmptyForTruncatedMessage();
};

void TestMdnsResolver::buildQueryEncodesHeaderAndQName() {
    const QByteArray packet = buildQuery(QStringLiteral("robot1.local"));

    // Header: ID=0, flags=0, QDCOUNT=1, ANCOUNT=NSCOUNT=ARCOUNT=0.
    QCOMPARE(packet.size(), 12 + (1 + 6) + (1 + 5) + 1 + 4);  // "robot1" + "local" + root + QTYPE/QCLASS
    QCOMPARE(quint8(packet.at(4)), quint8(0));
    QCOMPARE(quint8(packet.at(5)), quint8(1));
    for (int i : {0, 1, 2, 3, 6, 7, 8, 9, 10, 11}) {
        QCOMPARE(quint8(packet.at(i)), quint8(0));
    }

    int offset = 12;
    QCOMPARE(quint8(packet.at(offset)), quint8(6));
    QCOMPARE(packet.mid(offset + 1, 6), QByteArray("robot1"));
    offset += 7;
    QCOMPARE(quint8(packet.at(offset)), quint8(5));
    QCOMPARE(packet.mid(offset + 1, 5), QByteArray("local"));
    offset += 6;
    QCOMPARE(quint8(packet.at(offset)), quint8(0));  // root label
    offset += 1;

    // QTYPE = A (1), QCLASS = IN (1).
    QCOMPARE(quint8(packet.at(offset + 1)), quint8(1));
    QCOMPARE(quint8(packet.at(offset + 3)), quint8(1));
}

void TestMdnsResolver::isMdnsHostnameOnlyMatchesDotLocal() {
    QVERIFY(MdnsResolver::isMdnsHostname(QStringLiteral("robot1.local")));
    QVERIFY(MdnsResolver::isMdnsHostname(QStringLiteral("ROBOT1.LOCAL")));
    QVERIFY(!MdnsResolver::isMdnsHostname(QStringLiteral("192.168.1.5")));
    QVERIFY(!MdnsResolver::isMdnsHostname(QStringLiteral("robot1.example.com")));
    QVERIFY(!MdnsResolver::isMdnsHostname(QStringLiteral("localhost")));
}

void TestMdnsResolver::parseAAnswersFindsPlainEncodedRecord() {
    const QByteArray message = buildAnswerMessage(QStringLiteral("robot1.local"), 0xC0A80105);
    const QHash<QString, QHostAddress> answers = parseAAnswers(message);

    QCOMPARE(answers.size(), 1);
    QVERIFY(answers.contains(QStringLiteral("robot1.local")));
    QCOMPARE(answers.value(QStringLiteral("robot1.local")), QHostAddress(QStringLiteral("192.168.1.5")));
}

void TestMdnsResolver::parseAAnswersFollowsCompressionPointer() {
    // Mirrors a real responder replying to our own query: the answer's NAME
    // is a pointer back to the question section instead of the name spelled
    // out a second time.
    QByteArray message(12, char(0));
    message[5] = char(1);  // QDCOUNT = 1
    message[7] = char(1);  // ANCOUNT = 1

    const int nameOffset = message.size();
    appendLabel(message, "robot1");
    appendLabel(message, "local");
    message.append(char(0));
    message.append(char(0));
    message.append(char(1));  // QTYPE = A
    message.append(char(0));
    message.append(char(1));  // QCLASS = IN

    message.append(char(0xC0));
    message.append(char(nameOffset));
    message.append(char(0));
    message.append(char(1));  // TYPE = A
    message.append(char(0));
    message.append(char(1));  // CLASS = IN
    message.append(QByteArray(4, char(0)));
    message.append(char(0));
    message.append(char(4));  // RDLENGTH = 4
    message.append(char(10));
    message.append(char(0));
    message.append(char(0));
    message.append(char(42));

    const QHash<QString, QHostAddress> answers = parseAAnswers(message);
    QCOMPARE(answers.size(), 1);
    QCOMPARE(answers.value(QStringLiteral("robot1.local")), QHostAddress(QStringLiteral("10.0.0.42")));
}

void TestMdnsResolver::parseAAnswersIgnoresNonARecords() {
    QByteArray message(12, char(0));
    message[7] = char(1);  // ANCOUNT = 1

    appendLabel(message, "robot1");
    appendLabel(message, "local");
    message.append(char(0));
    message.append(char(0));
    message.append(char(28));  // TYPE = AAAA (28), not A -- must be skipped, not mistaken for one
    message.append(char(0));
    message.append(char(1));
    message.append(QByteArray(4, char(0)));
    message.append(char(0));
    message.append(char(16));  // RDLENGTH = 16 (a real IPv6 address's length)
    message.append(QByteArray(16, char(0xAB)));

    QVERIFY(parseAAnswers(message).isEmpty());
}

void TestMdnsResolver::parseAAnswersReturnsEmptyForTruncatedMessage() {
    QVERIFY(parseAAnswers(QByteArray()).isEmpty());
    QVERIFY(parseAAnswers(QByteArray(8, char(0))).isEmpty());  // shorter than a DNS header

    QByteArray truncated(12, char(0));
    truncated[7] = char(1);  // claims ANCOUNT = 1 but has no answer section at all
    QVERIFY(parseAAnswers(truncated).isEmpty());
}

}  // namespace

QTEST_MAIN(TestMdnsResolver)
#include "test_mdnsresolver.moc"
