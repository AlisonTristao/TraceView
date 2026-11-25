#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "transport.h"

class QHostAddress;
class QNetworkAccessManager;
class QTcpSocket;

namespace traceview {

class MdnsResolver;

// Asynchronous TCP byte transport. It owns no protocol state: callers receive
// exactly the bytes emitted by QTcpSocket and decide how to frame them.
class TcpTransport : public Transport {
    Q_OBJECT

public:
    explicit TcpTransport(QObject* parent = nullptr);

    // Starts a non-blocking connection. Returns false for invalid arguments or
    // while another socket is active; connectionStateChanged(true) is emitted
    // later from the socket's connected signal.
    bool open(const QString& host, quint16 port);
    void close() override;

    bool isConnected() const override;
    bool write(const QByteArray& data) override;

    void setReconnectEnabled(bool enabled) {
        m_reconnectEnabled = enabled;
        if (!enabled) {
            m_retryTimer.stop();
        }
    }
    void setReconnectDelayForTesting(int delayMs) {
        m_initialRetryDelayMs = qMax(1, delayMs);
    }
    // Same rationale as setReconnectDelayForTesting(): lets a test drive the
    // 5s connect-timeout (onConnectTimeout) without actually waiting 5s.
    void setConnectTimeoutForTesting(int timeoutMs) {
        m_connectTimeoutMs = qMax(1, timeoutMs);
    }

    QString host() const {
        return m_host;
    }
    quint16 port() const {
        return m_port;
    }
    int pendingFrameCount() const {
        return m_pendingFrames.size();
    }
    // Address where TraceKit's recovery firmware answered (empty while not
    // seen). See recoveryChanged().
    QString recoveryAddress() const {
        return m_recoveryAddress;
    }
    // Numeric address dialed when the *.local host is not answered by mDNS
    // (Device::tcpFallbackHost). Empty = none.
    void setFallbackHost(const QString& host) {
        m_fallbackHost = host.trimmed();
    }
    // What a fallback dial would use: the configured one, else the address
    // mDNS last answered with in this session.
    QString effectiveFallbackHost() const {
        return m_fallbackHost.isEmpty() ? m_learnedHost : m_fallbackHost;
    }

signals:
    // A refused or timed-out connection is probed with GET http://<address>/status: when
    // the answer is TraceKit's recovery (mode "recovery"), the device is up
    // but out of the app, and the BTP port will never open. Emitted with
    // that address, and with an empty one once a connection succeeds or a
    // probe gets any other answer. Kept across retries, which close and
    // reopen the transport.
    void recoveryChanged(const QString& address);
    // A *.local host was resolved by mDNS to `address` (worth remembering as
    // the fallback for when mDNS goes quiet).
    void hostResolved(const QString& address);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();
    void onConnectTimeout();
    void retryConnection();
    void onMdnsResolved(const QString& requestId, const QHostAddress& address);
    void onMdnsResolveFailed(const QString& requestId);

private:
    // One connection attempt: connectToHost() directly, or -- for a *.local
    // host -- an mDNS lookup first (see the .cpp).
    void dial();
    void drainPendingFrames();
    void scheduleReconnect();
    void probeRecovery();
    void setRecoveryAddress(const QString& address);

    static constexpr int kMaxPendingFrames = 16;
    QTcpSocket* m_socket = nullptr;
    MdnsResolver* m_resolver = nullptr;
    // Bumped by every dial() and close(); an mDNS answer for an older
    // attempt no longer matches and is dropped.
    quint64 m_dialSequence = 0;
    QString m_host;
    quint16 m_port = 0;
    // Last address dialed (the mDNS answer, or m_host itself): what the
    // recovery probe asks.
    QString m_lastAddress;
    QNetworkAccessManager* m_probe = nullptr;
    bool m_probeInFlight = false;
    QString m_recoveryAddress;
    QString m_fallbackHost;
    QString m_learnedHost;
    bool m_closing = false;
    bool m_reconnectEnabled = true;
    int m_initialRetryDelayMs = 250;
    int m_retryDelayMs = 250;
    int m_connectTimeoutMs = 5000;
    QTimer m_connectTimer;
    QTimer m_retryTimer;
    QQueue<QByteArray> m_pendingFrames;
    qsizetype m_pendingOffset = 0;
};

}  // namespace traceview
