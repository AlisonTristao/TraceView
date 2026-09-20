#pragma once

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>

#include "transport.h"

class QTcpSocket;

namespace traceview {

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

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onSocketError();
    void onConnectTimeout();
    void retryConnection();

private:
    void drainPendingFrames();
    void scheduleReconnect();

    static constexpr int kMaxPendingFrames = 16;
    QTcpSocket* m_socket = nullptr;
    QString m_host;
    quint16 m_port = 0;
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
