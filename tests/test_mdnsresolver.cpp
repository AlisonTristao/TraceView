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
    message.append(char(1));                 // CLASS = IN
    message.append(QByteArray(4, char(0)));  // TTL, value irrelevant to parseAAnswers
    message.append(char(0));
    message.append(char(4));  // RDLENGTH = 4
    message.append(char((address >> 24) & 0xFF));
    message.append(char((address >> 16) & 0xFF));
    message.append(char((address >> 8) & 0xFF));
    message.append(char(address & 0xFF));
    return message;
}

// One resource record: NAME (plain labels, or a 2-byte pointer when
// `pointer` >= 0), TYPE, CLASS IN, TTL, RDLENGTH, RDATA.
void appendRecord(QByteArray& out, const QByteArray& name, quint16 type, const QByteArray& rdata,
                  int pointer = -1) {
    if (pointer >= 0) {
        out.append(char(0xC0 | (pointer >> 8)));
        out.append(char(pointer & 0xFF));
    } else {
        for (const QByteArray& label : name.split('.')) {
            appendLabel(out, label);
        }
        out.append(char(0));
    }
    out.append(char(type >> 8));
    out.append(char(type & 0xFF));
    out.append(char(0));
    out.append(char(1));
    out.append(QByteArray(4, char(0)));
    out.append(char(rdata.size() >> 8));
    out.append(char(rdata.size() & 0xFF));
    out.append(rdata);
}

QByteArray plainName(const QByteArray& name) {
    QByteArray out;
    for (const QByteArray& label : name.split('.')) {
        appendLabel(out, label);
    }
    out.append(char(0));
    return out;
}

QByteArray srvData(quint16 port, const QByteArray& target) {
    QByteArray out(4, char(0));  // priority, weight
    out.append(char(port >> 8));
    out.append(char(port & 0xFF));
    out.append(plainName(target));
    return out;
}

QByteArray txtData(const QList<QByteArray>& entries) {
    QByteArray out;
    for (const QByteArray& entry : entries) {
        out.append(char(entry.size()));
        out.append(entry);
    }
    return out;
}

// A header whose ANCOUNT/ARCOUNT are filled in by the caller.
QByteArray responseHeader(int answers, int additional) {
    QByteArray message(12, char(0));
    message[2] = char(0x84);  // QR + AA, as a responder sets them
    message[7] = char(answers);
    message[11] = char(additional);
    return message;
}

class TestMdnsResolver : public QObject {
    Q_OBJECT

private slots:
    void buildQueryEncodesHeaderAndQName();
    void isMdnsHostnameOnlyMatchesDotLocal();
    void parseAAnswersFindsPlainEncodedRecord();
    void parseAAnswersWidensEveryOctetBeforeShifting();
    void parseAAnswersFollowsCompressionPointer();
    void parseAAnswersIgnoresNonARecords();
    void parseAAnswersReturnsEmptyForTruncatedMessage();
    void buildPtrQueryAsksForPtrRecords();
    void parseServiceRecordsJoinsPtrSrvTxtAndA();
    void parseServiceRecordsAcceptsSrvWithoutPtr();
    void parseServiceRecordsSkipsInstanceWithoutSrv();
    void parseServiceRecordsIgnoresOtherServiceTypes();
    void parseServiceRecordsSurvivesTruncatedTxt();
};

void TestMdnsResolver::buildQueryEncodesHeaderAndQName() {
    const QByteArray packet = buildQuery(QStringLiteral("robot1.local"));

    // Header: ID=0, flags=0, QDCOUNT=1, ANCOUNT=NSCOUNT=ARCOUNT=0.
    QCOMPARE(packet.size(),
             12 + (1 + 6) + (1 + 5) + 1 + 4);  // "robot1" + "local" + root + QTYPE/QCLASS
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
    QCOMPARE(answers.value(QStringLiteral("robot1.local")),
             QHostAddress(QStringLiteral("192.168.1.5")));
}

void TestMdnsResolver::parseAAnswersWidensEveryOctetBeforeShifting() {
    // A DNS octet promotes to int, so shifting one >= 128 left by 24
    // overflows a signed int unless it is widened to quint32 first. That is
    // every address in 128.0.0.0/1 -- including the 192.168.x.x range a
    // robot on a home network actually gets. Pinned at the extreme
    // (255.255.255.255, every octet at its maximum) so the widening cannot
    // silently regress into whatever the current compiler happens to do
    // with the overflow.
    const QHash<QString, QHostAddress> answers =
        parseAAnswers(buildAnswerMessage(QStringLiteral("robot1.local"), 0xFFFFFFFFu));

    QCOMPARE(answers.value(QStringLiteral("robot1.local")),
             QHostAddress(QStringLiteral("255.255.255.255")));

    // The high-bit-set boundary itself, one past the last address that fits
    // without overflowing.
    const QHash<QString, QHostAddress> boundary =
        parseAAnswers(buildAnswerMessage(QStringLiteral("robot1.local"), 0x80000001u));
    QCOMPARE(boundary.value(QStringLiteral("robot1.local")),
             QHostAddress(QStringLiteral("128.0.0.1")));
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
    QCOMPARE(answers.value(QStringLiteral("robot1.local")),
             QHostAddress(QStringLiteral("10.0.0.42")));
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

void TestMdnsResolver::buildPtrQueryAsksForPtrRecords() {
    using traceview::mdns_detail::buildPtrQuery;
    const QByteArray packet = buildPtrQuery(QStringLiteral("_btp._tcp.local"));
    QCOMPARE(quint8(packet.at(5)), quint8(1));  // QDCOUNT = 1
    QCOMPARE(packet.mid(12, packet.size() - 12 - 4), plainName("_btp._tcp.local"));
    QCOMPARE(quint8(packet.at(packet.size() - 4)), quint8(0));
    QCOMPARE(quint8(packet.at(packet.size() - 3)), quint8(12));  // QTYPE = PTR
    QCOMPARE(quint8(packet.at(packet.size() - 1)), quint8(1));   // QCLASS = IN
}

void TestMdnsResolver::parseServiceRecordsJoinsPtrSrvTxtAndA() {
    using traceview::mdns_detail::parseServiceRecords;
    // The reply ESP-IDF's mdns component gives a PTR query: the PTR as the
    // answer, SRV/TXT/A as additional records, names compressed.
    QByteArray message = responseHeader(1, 3);
    const int typeOffset = message.size();
    appendRecord(message, "_btp._tcp.local", 12, plainName("Robo 2._btp._tcp.local"));
    // The PTR's RDATA (the instance name) starts after NAME(17)+10 octets.
    const int instanceOffset = typeOffset + 17 + 10;
    appendRecord(message, QByteArray(), 33, srvData(44300, "robo-2.local"), instanceOffset);
    appendRecord(message, QByteArray(), 16, txtData({"name=Robo 2"}), instanceOffset);
    appendRecord(message, "robo-2.local", 1, QByteArray("\xC0\xA8\x00\x2A", 4));

    const auto records = parseServiceRecords(message, QStringLiteral("_btp._tcp.local"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].instance, QStringLiteral("robo 2._btp._tcp.local"));
    QCOMPARE(records[0].host, QStringLiteral("robo-2.local"));
    QCOMPARE(records[0].port, quint16(44300));
    QCOMPARE(records[0].txtName, QStringLiteral("Robo 2"));
    QCOMPARE(records[0].address, QHostAddress(QStringLiteral("192.168.0.42")));
}

void TestMdnsResolver::parseServiceRecordsAcceptsSrvWithoutPtr() {
    using traceview::mdns_detail::parseServiceRecords;
    QByteArray message = responseHeader(1, 0);
    appendRecord(message, "furuta._btp._tcp.local", 33, srvData(44301, "furuta.local"));
    const auto records = parseServiceRecords(message, QStringLiteral("_BTP._tcp.local"));
    QCOMPARE(records.size(), 1);
    QCOMPARE(records[0].port, quint16(44301));
    QVERIFY(records[0].txtName.isEmpty());
    QVERIFY(records[0].address.isNull());
}

void TestMdnsResolver::parseServiceRecordsSkipsInstanceWithoutSrv() {
    using traceview::mdns_detail::parseServiceRecords;
    QByteArray message = responseHeader(1, 0);
    appendRecord(message, "_btp._tcp.local", 12, plainName("lonely._btp._tcp.local"));
    QVERIFY(parseServiceRecords(message, QStringLiteral("_btp._tcp.local")).isEmpty());
}

void TestMdnsResolver::parseServiceRecordsIgnoresOtherServiceTypes() {
    using traceview::mdns_detail::parseServiceRecords;
    QByteArray message = responseHeader(2, 0);
    appendRecord(message, "_http._tcp.local", 12, plainName("printer._http._tcp.local"));
    appendRecord(message, "printer._http._tcp.local", 33, srvData(80, "printer.local"));
    QVERIFY(parseServiceRecords(message, QStringLiteral("_btp._tcp.local")).isEmpty());
}

void TestMdnsResolver::parseServiceRecordsSurvivesTruncatedTxt() {
    using traceview::mdns_detail::parseServiceRecords;
    QByteArray message = responseHeader(2, 0);
    appendRecord(message, "r._btp._tcp.local", 33, srvData(44300, "r.local"));
    // The TXT entry claims 40 octets but the record holds 5.
    appendRecord(message, "r._btp._tcp.local", 16, QByteArray("\x28name", 5));
    const auto records = parseServiceRecords(message, QStringLiteral("_btp._tcp.local"));
    QCOMPARE(records.size(), 1);
    QVERIFY(records[0].txtName.isEmpty());
    QVERIFY(parseServiceRecords(message.left(20), QStringLiteral("_btp._tcp.local")).isEmpty());
}

}  // namespace

QTEST_MAIN(TestMdnsResolver)
#include "test_mdnsresolver.moc"
