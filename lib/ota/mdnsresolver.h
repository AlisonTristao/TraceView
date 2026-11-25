#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QNetworkInterface>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class QUdpSocket;
class QTimer;

namespace traceview {

// Pure, socket-free packet encode/decode -- split out from MdnsResolver
// itself so tests/test_mdnsresolver.cpp can exercise the wire format
// directly without opening a real multicast socket.
namespace mdns_detail {

// Builds one mDNS (RFC 6762) query message asking for the A record of
// `hostname` (e.g. "robot1.local"). ID=0 and no QU/unicast-response bit --
// MdnsResolver listens on the multicast group itself rather than asking for
// a unicast reply, so it doesn't depend on every responder implementation
// honoring that bit.
QByteArray buildQuery(const QString& hostname);

// Parses one received mDNS message (query OR response -- unsolicited
// multicast traffic includes both) and returns every A-record answer it
// contains, keyed by lower-cased owner name. Empty if the message has no
// answers, is truncated, or isn't a well-formed DNS message at all --
// MdnsResolver treats all of those the same way (nothing usable here),
// there's no need for the caller to distinguish them.
QHash<QString, QHostAddress> parseAAnswers(const QByteArray& message);

// Builds one mDNS query for the PTR records of a DNS-SD service type
// (RFC 6763 section 4, e.g. "_btp._tcp.local") -- "who offers this
// service?". Every responder offering it answers with its instance's PTR,
// and (ESP-IDF's mdns component does) its SRV, TXT and A records alongside.
QByteArray buildPtrQuery(const QString& serviceType);

// One service instance found in a DNS-SD answer: the instance's own name
// (lower-cased, like every name this file reads), its SRV target host and
// port, the "name" key of its TXT record (case kept -- it is what a robot
// calls itself), and the A record of the SRV host when the same message
// carried it (null otherwise).
struct ServiceRecord {
    QString instance;
    QString host;
    quint16 port = 0;
    QString txtName;
    QHostAddress address;
};

// Every instance of `serviceType` that `message` describes, gathered from
// all three record sections (answers, authority, additional): a PTR names
// the instance, its SRV/TXT/A fill in the rest. An SRV for an instance of
// the type counts even without the PTR (an unsolicited announcement may
// carry only that). Instances whose SRV is missing are left out -- without
// a port there is nothing to dial.
QVector<ServiceRecord> parseServiceRecords(const QByteArray& message, const QString& serviceType);

// Up, running, multicast-capable, not loopback, with an IPv4 address: the
// interfaces a query goes out on.
bool usableInterface(const QNetworkInterface& iface);

}  // namespace mdns_detail

// Minimal one-shot Multicast DNS (RFC 6762) A-record resolver, entirely in
// Qt/C++ over a raw UDP multicast socket -- doesn't touch the OS resolver
// (QHostInfo/getaddrinfo) at all. Exists because the *.local names bally_OS
// advertises via mDNS (OTAUpdater::ensure_mdns) routinely fail to resolve on
// Windows: unlike macOS/most Linux distros, Windows has no mDNS responder
// built in, so getaddrinfo("robot1.local") just returns "host not found"
// unless something like Apple's Bonjour happens to be installed. Talking
// the multicast wire protocol ourselves (RFC 6762 section 5: query
// 224.0.0.251:5353, listen on the same group/port for the answer) works
// regardless of what's installed on the machine.
//
// The query goes out of EVERY up, multicast-capable IPv4 interface, not the
// one the OS would pick: that one is the lowest route metric, and on a PC
// with a VPN or overlay (Tailscale, ZeroTier, Hyper-V's switch) it is that
// adapter, where no robot will ever answer -- the robot is on the Wi-Fi.
class MdnsResolver : public QObject {
    Q_OBJECT

public:
    explicit MdnsResolver(QObject* parent = nullptr);

    // True for any hostname ending in ".local" (case-insensitive) -- what a
    // caller should check before routing a lookup through resolve() instead
    // of a normal DNS/IP path.
    static bool isMdnsHostname(const QString& hostname);

    // Sends one mDNS A-record query for `hostname` and waits a few seconds
    // for a matching answer, multicast on the local network. Resolves to
    // exactly one of resolved()/resolveFailed() for this requestId. A
    // second resolve() for a requestId that's still pending replaces it
    // (matches OtaClient's own "abort and replace" convention for its own
    // requests).
    void resolve(const QString& requestId, const QString& hostname);

signals:
    void resolved(const QString& requestId, const QHostAddress& address);
    void resolveFailed(const QString& requestId);

private:
    struct PendingQuery {
        QString hostnameLower;
        qint64 deadlineMs = 0;
        qint64 resendMs = 0;  // next repeat of the query (0: no more)
        int resendsLeft = 0;
    };

    void ensureSocket();
    // Joins the group on interfaces not joined yet (an adapter that came up
    // since the last query) and returns every interface to send on.
    QList<QNetworkInterface> refreshInterfaces();
    void sendQuery(const QString& hostnameLower);
    void onReadyRead();
    void onTimeoutTick();

    QUdpSocket* m_socket = nullptr;
    QTimer* m_timeoutTimer;
    QHash<QString, PendingQuery> m_pending;  // requestId -> query
    QSet<QString> m_joined;                  // QNetworkInterface::name() of the joined ones
    // refreshInterfaces()'s last answer and when it was taken: enumerating
    // the adapters is a slow system call on Windows (GetAdaptersAddresses),
    // and a query is sent up to three times per lookup, every few seconds
    // for a robot that stays offline -- all on the UI thread.
    QList<QNetworkInterface> m_interfaces;
    qint64 m_interfacesFetchedMs = -1;
};

}  // namespace traceview
