#pragma once

#include <QList>
#include <QNetworkInterface>
#include <QObject>
#include <QSet>
#include <QString>

#include "ota/mdnsresolver.h"

class QUdpSocket;
class QTimer;

namespace traceview {

// Finds every instance of one DNS-SD service type on the local network
// (RFC 6763 over mDNS): bally_OS robots advertise "_btp._tcp" with their
// name in the TXT record (TraceKit TcpLink::start_mdns), so one PTR query
// lists every robot on the Wi-Fi without dialing any of them.
//
// Cheap on purpose: a query when start() is called, two repeats a second
// or two later (Wi-Fi multicast drops packets), then one every
// kRequeryIntervalMs while running -- and in between it only listens, which
// also catches the announcements a robot multicasts by itself when it joins
// the network. Same socket handling as MdnsResolver (shared port 5353, the
// query sent out of every usable interface).
class MdnsBrowser : public QObject {
    Q_OBJECT

public:
    // `serviceType` without the domain, e.g. "_btp._tcp".
    explicit MdnsBrowser(const QString& serviceType, QObject* parent = nullptr);

    void start();
    void stop();
    bool isRunning() const {
        return m_running;
    }
    // Asks again right now (a "refresh" button), while running.
    void queryNow();

signals:
    // Every time an instance is heard from -- the same robot is reported
    // again on each answer; the caller keys on what it needs.
    void serviceSeen(const traceview::mdns_detail::ServiceRecord& record);

private:
    void ensureSocket();
    void sendQuery();
    QList<QNetworkInterface> refreshInterfaces();
    void onReadyRead();
    void onTick();

    QString m_serviceType;  // with ".local", lower-cased
    QUdpSocket* m_socket = nullptr;
    QTimer* m_timer = nullptr;
    bool m_running = false;
    int m_quickRepeatsLeft = 0;
    qint64 m_nextQueryMs = 0;
    QSet<QString> m_joined;
    QList<QNetworkInterface> m_interfaces;
    qint64 m_interfacesFetchedMs = -1;
};

}  // namespace traceview
