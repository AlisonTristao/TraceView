#include "ota/mdnsresolver.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace traceview {

namespace {
constexpr quint16 kMdnsPort = 5353;
constexpr int kTimeoutMs = 3000;
constexpr int kTimeoutTickMs = 250;

QHostAddress mdnsGroup() { return QHostAddress(QStringLiteral("224.0.0.251")); }
}  // namespace

namespace mdns_detail {

namespace {

// Reads a DNS name starting at `offset` (RFC 1035 4.1.4: a sequence of
// length-prefixed labels, terminated either by a zero-length label or by a
// 2-byte compression pointer into an earlier part of the message).
// `offset` is advanced past whichever of those two endings appears FIRST in
// `message` at the name's own position -- i.e. exactly as far as the caller
// needs to skip to reach the next field, regardless of how many pointers
// were then followed to actually decode the name. `guard` bounds pointer
// chains against a malformed/malicious message looping forever.
QString readName(const QByteArray& message, int& offset) {
    QStringList labels;
    int cursor = offset;
    int endOfNameInPlace = -1;
    int guard = 0;

    while (cursor >= 0 && cursor < message.size() && guard++ < 128) {
        const quint8 length = quint8(message.at(cursor));
        if (length == 0) {
            if (endOfNameInPlace < 0) endOfNameInPlace = cursor + 1;
            break;
        }
        if ((length & 0xC0) == 0xC0) {
            if (cursor + 1 >= message.size()) break;
            const int pointer = ((length & 0x3F) << 8) | quint8(message.at(cursor + 1));
            if (endOfNameInPlace < 0) endOfNameInPlace = cursor + 2;
            cursor = pointer;
            continue;
        }
        if (cursor + 1 + length > message.size()) break;
        labels << QString::fromLatin1(message.constData() + cursor + 1, length);
        cursor += 1 + length;
    }

    offset = endOfNameInPlace >= 0 ? endOfNameInPlace : cursor;
    return labels.join(QLatin1Char('.')).toLower();
}

}  // namespace

QByteArray buildQuery(const QString& hostname) {
    QByteArray packet(12, char(0));
    // Header: ID=0 (mDNS doesn't use it to correlate -- RFC 6762 18.1),
    // flags=0 (standard query), QDCOUNT=1, ANCOUNT/NSCOUNT/ARCOUNT=0.
    packet[5] = char(1);  // QDCOUNT high/low byte -- byte[4] stays 0

    const QStringList labels = hostname.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (const QString& label : labels) {
        const QByteArray bytes = label.toLatin1().left(63);  // DNS label length limit
        packet.append(char(bytes.size()));
        packet.append(bytes);
    }
    packet.append(char(0));  // root label terminating QNAME

    packet.append(char(0));
    packet.append(char(1));  // QTYPE = A
    packet.append(char(0));
    packet.append(char(1));  // QCLASS = IN
    return packet;
}

QHash<QString, QHostAddress> parseAAnswers(const QByteArray& message) {
    QHash<QString, QHostAddress> answers;
    if (message.size() < 12) return answers;

    const int qdcount = (quint8(message.at(4)) << 8) | quint8(message.at(5));
    const int ancount = (quint8(message.at(6)) << 8) | quint8(message.at(7));
    if (ancount <= 0) return answers;

    int offset = 12;
    // Skip the question section -- present when this is a direct reply to
    // our own query, usually absent from the unsolicited announcements
    // mDNS responders also broadcast periodically on their own.
    for (int i = 0; i < qdcount && offset < message.size(); ++i) {
        readName(message, offset);
        offset += 4;  // QTYPE(2) + QCLASS(2)
    }

    for (int i = 0; i < ancount && offset < message.size(); ++i) {
        const QString name = readName(message, offset);
        if (offset + 10 > message.size()) break;  // TYPE(2)+CLASS(2)+TTL(4)+RDLENGTH(2)
        const quint16 type = (quint8(message.at(offset)) << 8) | quint8(message.at(offset + 1));
        offset += 8;  // TYPE(2) + CLASS(2) + TTL(4)
        const quint16 rdlength = (quint8(message.at(offset)) << 8) | quint8(message.at(offset + 1));
        offset += 2;
        if (offset + rdlength > message.size()) break;

        if (type == 1 && rdlength == 4) {  // A record
            // quint32(quint8(...)), not a bare quint8: an octet promotes to
            // int, and shifting a value >= 128 left by 24 overflows a signed
            // int -- which is every address in 128.0.0.0/1, so every ordinary
            // 192.168.x.x lease. Same idiom the rest of this codebase already
            // uses to widen before shifting (btphandshake.cpp, clocksync.cpp).
            const quint32 ip = (quint32(quint8(message.at(offset))) << 24) |
                                (quint32(quint8(message.at(offset + 1))) << 16) |
                                (quint32(quint8(message.at(offset + 2))) << 8) |
                                quint32(quint8(message.at(offset + 3)));
            answers.insert(name, QHostAddress(ip));
        }
        offset += rdlength;
    }

    return answers;
}

}  // namespace mdns_detail

MdnsResolver::MdnsResolver(QObject* parent) : QObject(parent) {
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setInterval(kTimeoutTickMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, &MdnsResolver::onTimeoutTick);
}

bool MdnsResolver::isMdnsHostname(const QString& hostname) {
    return hostname.endsWith(QStringLiteral(".local"), Qt::CaseInsensitive);
}

void MdnsResolver::ensureSocket() {
    if (m_socket != nullptr) return;

    m_socket = new QUdpSocket(this);
    // ShareAddress+ReuseAddressHint: a real OS mDNS responder (Bonjour, or
    // another instance of this app) may already be bound to 5353 -- this is
    // meant to coexist with one, not fight it for the port, same as every
    // other mDNS query tool.
    m_socket->bind(QHostAddress::AnyIPv4, kMdnsPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    m_socket->joinMulticastGroup(mdnsGroup());
    connect(m_socket, &QUdpSocket::readyRead, this, &MdnsResolver::onReadyRead);
}

void MdnsResolver::resolve(const QString& requestId, const QString& hostname) {
    ensureSocket();

    PendingQuery query;
    query.hostnameLower = hostname.toLower();
    query.deadlineMs = QDateTime::currentMSecsSinceEpoch() + kTimeoutMs;
    m_pending.insert(requestId, query);

    m_socket->writeDatagram(mdns_detail::buildQuery(hostname), mdnsGroup(), kMdnsPort);
    if (!m_timeoutTimer->isActive()) {
        m_timeoutTimer->start();
    }
}

void MdnsResolver::onReadyRead() {
    while (m_socket->hasPendingDatagrams()) {
        const QHash<QString, QHostAddress> answers = mdns_detail::parseAAnswers(m_socket->receiveDatagram().data());
        if (answers.isEmpty()) continue;

        for (auto it = m_pending.begin(); it != m_pending.end();) {
            const auto answer = answers.constFind(it->hostnameLower);
            if (answer != answers.constEnd()) {
                emit resolved(it.key(), answer.value());
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (m_pending.isEmpty()) {
        m_timeoutTimer->stop();
    }
}

void MdnsResolver::onTimeoutTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (now >= it->deadlineMs) {
            emit resolveFailed(it.key());
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    if (m_pending.isEmpty()) {
        m_timeoutTimer->stop();
    }
}

}  // namespace traceview
