#include "ota/mdnsbrowser.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace traceview {

namespace {
constexpr quint16 kMdnsPort = 5353;
constexpr int kTickMs = 500;
// The first query, then these repeats (RFC 6762 5.2: a querier repeats,
// doubling the gap), then one every kRequeryIntervalMs.
constexpr int kQuickRepeats = 2;
constexpr int kFirstRepeatMs = 1000;
constexpr int kRequeryIntervalMs = 30000;
constexpr qint64 kInterfaceRefreshMs = 30000;

QHostAddress mdnsGroup() {
    return QHostAddress(QStringLiteral("224.0.0.251"));
}
}  // namespace

MdnsBrowser::MdnsBrowser(const QString& serviceType, QObject* parent)
    : QObject(parent), m_serviceType(serviceType.toLower() + QStringLiteral(".local")) {
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &MdnsBrowser::onTick);
}

void MdnsBrowser::start() {
    if (m_running) {
        return;
    }
    m_running = true;
    ensureSocket();
    queryNow();
    m_timer->start();
}

void MdnsBrowser::stop() {
    if (!m_running) {
        return;
    }
    m_running = false;
    m_timer->stop();
    // The socket goes too: a stopped browser holds no port and leaves the
    // multicast group, so an idle Home page costs nothing on the network.
    if (m_socket != nullptr) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_joined.clear();
    m_interfacesFetchedMs = -1;
}

void MdnsBrowser::queryNow() {
    if (!m_running) {
        return;
    }
    sendQuery();
    m_quickRepeatsLeft = kQuickRepeats;
    m_nextQueryMs = QDateTime::currentMSecsSinceEpoch() + kFirstRepeatMs;
}

void MdnsBrowser::sendQuery() {
    const QByteArray datagram = mdns_detail::buildPtrQuery(m_serviceType);
    const QList<QNetworkInterface> interfaces = refreshInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        m_socket->setMulticastInterface(iface);
        m_socket->writeDatagram(datagram, mdnsGroup(), kMdnsPort);
    }
    if (interfaces.isEmpty()) {
        // No interface qualified (or none could be listed): let the OS pick.
        if (m_joined.isEmpty()) {
            m_socket->joinMulticastGroup(mdnsGroup());
        }
        m_socket->writeDatagram(datagram, mdnsGroup(), kMdnsPort);
    }
}

void MdnsBrowser::ensureSocket() {
    if (m_socket != nullptr) {
        return;
    }
    m_socket = new QUdpSocket(this);
    // Shared with the OS responder and MdnsResolver -- see MdnsResolver.
    m_socket->bind(QHostAddress::AnyIPv4, kMdnsPort,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_socket, &QUdpSocket::readyRead, this, &MdnsBrowser::onReadyRead);
}

QList<QNetworkInterface> MdnsBrowser::refreshInterfaces() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_interfacesFetchedMs >= 0 && now - m_interfacesFetchedMs < kInterfaceRefreshMs) {
        return m_interfaces;
    }
    m_interfacesFetchedMs = now;
    QList<QNetworkInterface> usable;
    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        if (!mdns_detail::usableInterface(iface)) {
            continue;
        }
        usable.append(iface);
        if (!m_joined.contains(iface.name()) && m_socket->joinMulticastGroup(mdnsGroup(), iface)) {
            m_joined.insert(iface.name());
        }
    }
    m_interfaces = usable;
    return usable;
}

void MdnsBrowser::onReadyRead() {
    while (m_socket != nullptr && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket->receiveDatagram();
        const QVector<mdns_detail::ServiceRecord> records =
            mdns_detail::parseServiceRecords(datagram.data(), m_serviceType);
        for (mdns_detail::ServiceRecord record : records) {
            // No A record in the same message: the sender is the robot itself
            // (a responder answers for its own name), so its address will do.
            if (record.address.isNull() && datagram.senderAddress().protocol() ==
                                               QAbstractSocket::IPv4Protocol) {
                record.address = datagram.senderAddress();
            }
            emit serviceSeen(record);
        }
    }
}

void MdnsBrowser::onTick() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now < m_nextQueryMs) {
        return;
    }
    sendQuery();
    if (m_quickRepeatsLeft > 0) {
        --m_quickRepeatsLeft;
        m_nextQueryMs = now + kFirstRepeatMs * (kQuickRepeats - m_quickRepeatsLeft + 1);
    } else {
        m_nextQueryMs = now + kRequeryIntervalMs;
    }
}

}  // namespace traceview
