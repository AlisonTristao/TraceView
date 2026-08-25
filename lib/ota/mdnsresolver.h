#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>

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
    };

    void ensureSocket();
    void onReadyRead();
    void onTimeoutTick();

    QUdpSocket* m_socket = nullptr;
    QTimer* m_timeoutTimer;
    QHash<QString, PendingQuery> m_pending;  // requestId -> query
};

}  // namespace traceview
