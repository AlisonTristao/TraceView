#include "tcptransport.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpSocket>
#include <QUrl>

#include "core/applog.h"
#include "ota/mdnsresolver.h"

namespace traceview {

TcpTransport::TcpTransport(QObject* parent) : Transport(parent), m_socket(new QTcpSocket(this)) {
    connect(m_socket, &QTcpSocket::connected, this, &TcpTransport::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &TcpTransport::onReadyRead);
        connect(m_socket, &QTcpSocket::bytesWritten, this,
            [this](qint64) { drainPendingFrames(); });
    connect(m_socket, &QTcpSocket::disconnected, this, &TcpTransport::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &TcpTransport::onSocketError);
    m_connectTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, &TcpTransport::onConnectTimeout);
    connect(&m_retryTimer, &QTimer::timeout, this, &TcpTransport::retryConnection);

    m_resolver = new MdnsResolver(this);
    connect(m_resolver, &MdnsResolver::resolved, this, &TcpTransport::onMdnsResolved);
    connect(m_resolver, &MdnsResolver::resolveFailed, this, &TcpTransport::onMdnsResolveFailed);
}

bool TcpTransport::open(const QString& host, quint16 port) {
    if (host.trimmed().isEmpty() || port == 0 || m_socket->state() != QAbstractSocket::UnconnectedState) {
        return false;
    }

    m_host = host.trimmed();
    m_port = port;
    m_closing = false;
    m_retryTimer.stop();
    m_connectTimer.start(m_connectTimeoutMs);
    m_retryDelayMs = m_initialRetryDelayMs;
    m_pendingFrames.clear();
    m_pendingOffset = 0;
    qCInfo(lcConnection) << "connecting TCP to" << m_host << m_port;
    dial();
    return true;
}

void TcpTransport::close() {
    m_retryTimer.stop();
    m_connectTimer.stop();
    ++m_dialSequence;  // a lookup still in flight must not dial after close()
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        return;
    }
    m_closing = true;
    m_socket->abort();
}

bool TcpTransport::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool TcpTransport::write(const QByteArray& data) {
    if (!isConnected() || data.isEmpty() || m_pendingFrames.size() >= kMaxPendingFrames) {
        return false;
    }
    m_pendingFrames.enqueue(data);
    drainPendingFrames();
    return true;
}

void TcpTransport::onConnected() {
    qCInfo(lcConnection) << "TCP connected to" << m_host << m_port;
    m_closing = false;
    m_connectTimer.stop();
    m_retryTimer.stop();
    m_retryDelayMs = m_initialRetryDelayMs;
    setRecoveryAddress(QString());
    emit connectionStateChanged(true);
}

void TcpTransport::onReadyRead() {
    const QByteArray data = m_socket->readAll();
    if (!data.isEmpty()) {
        emit dataReceived(data);
    }
}

void TcpTransport::onDisconnected() {
    qCInfo(lcConnection) << "TCP disconnected from" << m_host << m_port;
    const bool wasClosing = m_closing;
    m_closing = false;
    m_pendingFrames.clear();
    m_pendingOffset = 0;
    emit connectionStateChanged(false);
    if (!wasClosing) {
        scheduleReconnect();
    }
}

void TcpTransport::onSocketError() {
    const QString message = m_socket->errorString();
    qCWarning(lcConnection) << "TCP error on" << m_host << m_port << ":" << message;
    if (m_socket->error() == QAbstractSocket::ConnectionRefusedError) {
        probeRecovery();
    }
    emit errorOccurred(message);
}

void TcpTransport::probeRecovery() {
    if (m_probeInFlight || m_lastAddress.isEmpty()) {
        return;
    }
    if (m_probe == nullptr) {
        m_probe = new QNetworkAccessManager(this);
    }
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_lastAddress);
    url.setPath(QStringLiteral("/status"));
    QNetworkRequest request(url);
    request.setTransferTimeout(2000);
    m_probeInFlight = true;
    QNetworkReply* reply = m_probe->get(request);
    const QString address = m_lastAddress;
    connect(reply, &QNetworkReply::finished, this, [this, reply, address]() {
        reply->deleteLater();
        m_probeInFlight = false;
        if (m_closing || isConnected()) {
            return;
        }
        const QJsonObject status = QJsonDocument::fromJson(reply->readAll()).object();
        const bool inRecovery = reply->error() == QNetworkReply::NoError &&
                                status.value(QStringLiteral("mode")).toString() ==
                                    QLatin1String("recovery");
        if (inRecovery) {
            qCWarning(lcConnection) << m_host << "is in TraceKit recovery at" << address;
        }
        setRecoveryAddress(inRecovery ? address : QString());
    });
}

void TcpTransport::setRecoveryAddress(const QString& address) {
    if (m_recoveryAddress == address) {
        return;
    }
    m_recoveryAddress = address;
    emit recoveryChanged(address);
}

void TcpTransport::onConnectTimeout() {
    if (m_socket->state() == QAbstractSocket::ConnectingState) {
        qCWarning(lcConnection) << "TCP connection timed out for" << m_host << m_port;
        m_socket->abort();
        // Recovery may drop the SYN instead of refusing it: ask here too.
        probeRecovery();
        emit errorOccurred(tr("TCP connection timed out"));
    }
}

void TcpTransport::scheduleReconnect() {
    if (!m_reconnectEnabled || m_closing || m_host.isEmpty() || m_port == 0 ||
        m_retryTimer.isActive()) {
        return;
    }
    m_retryTimer.start(m_retryDelayMs);
    m_retryDelayMs = qMin(m_retryDelayMs * 2, 4000);
}

void TcpTransport::retryConnection() {
    if (!m_reconnectEnabled || m_closing || m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    qCInfo(lcConnection) << "retrying TCP connection to" << m_host << m_port;
    dial();
    m_connectTimer.start(m_connectTimeoutMs);
}

void TcpTransport::dial() {
    // A new attempt supersedes any lookup still in flight for an older one.
    const QString requestId = QString::number(++m_dialSequence);
    if (!MdnsResolver::isMdnsHostname(m_host)) {
        m_lastAddress = m_host;
        m_socket->connectToHost(m_host, m_port);
        return;
    }
    // The robot's IP changes on every reboot but its mDNS name
    // (OTAUpdater's ota_hostname, "ballyrobot" by default) does not, and the
    // OS resolver cannot look it up on Windows. Resolved again on EVERY
    // attempt, so a retry after a reboot finds the new address.
    qCInfo(lcConnection) << "resolving" << m_host << "via mDNS";
    m_resolver->resolve(requestId, m_host);
}

void TcpTransport::onMdnsResolved(const QString& requestId, const QHostAddress& address) {
    if (requestId != QString::number(m_dialSequence) || m_closing ||
        m_socket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    qCInfo(lcConnection) << m_host << "resolved to" << address.toString();
    m_lastAddress = address.toString();
    m_fallbackHost = m_lastAddress;  // for when mDNS goes quiet later
    emit hostResolved(m_lastAddress);
    m_socket->connectToHost(address, m_port);
}

void TcpTransport::onMdnsResolveFailed(const QString& requestId) {
    if (requestId != QString::number(m_dialSequence) || m_closing) {
        return;
    }
    qCWarning(lcConnection) << "mDNS lookup failed for" << m_host;
    if (!m_fallbackHost.isEmpty() && m_socket->state() == QAbstractSocket::UnconnectedState) {
        // The address it last answered with (or the one set by hand): a
        // refusal or timeout there still gets the recovery probe.
        qCInfo(lcConnection) << "trying the fallback address" << m_fallbackHost;
        m_lastAddress = m_fallbackHost;
        m_socket->connectToHost(m_fallbackHost, m_port);
        return;
    }
    emit errorOccurred(tr("Could not find %1 on the network (mDNS)").arg(m_host));
    scheduleReconnect();
}

void TcpTransport::drainPendingFrames() {
    if (!isConnected()) {
        return;
    }

    while (!m_pendingFrames.isEmpty()) {
        const QByteArray& frame = m_pendingFrames.head();
        const qsizetype remaining = frame.size() - m_pendingOffset;
        const qint64 written = m_socket->write(frame.constData() + m_pendingOffset, remaining);
        if (written <= 0) {
            return;
        }
        m_pendingOffset += written;
        if (m_pendingOffset == frame.size()) {
            m_pendingFrames.dequeue();
            m_pendingOffset = 0;
        }
    }
}

}  // namespace traceview
