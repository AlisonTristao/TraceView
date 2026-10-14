#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>
#include <functional>

class QLocalSocket;

namespace traceview {

// `TraceView --mcp`: the process an MCP client (Claude Code, Claude Desktop)
// launches. It speaks MCP over its own stdin/stdout and relays every message
// to the running TraceView's AgentBridgeServer, so the client talks to the
// live app the user is looking at instead of a headless copy.
//
// It answers the lifecycle itself (initialize, ping), which lets the client
// connect before TraceView is up. Until the app is reachable it offers a
// single start_traceview tool; when the app comes or goes it tells the client
// with notifications/tools/list_changed, and requests the app never answered
// get an error instead of hanging.
class McpStdioRelay : public QObject {
    Q_OBJECT

public:
    // Starts TraceView with its bridge on. Returns an empty string on
    // success, else why it couldn't (shown to the model). Replaceable for
    // tests.
    using Launcher = std::function<QString()>;

    explicit McpStdioRelay(const QString& socketName, QObject* parent = nullptr);

    void setLauncher(Launcher launcher) {
        m_launcher = std::move(launcher);
    }
    // How long start_traceview waits for the app before giving up.
    void setStartTimeoutMs(int ms) {
        m_startTimeoutMs = ms;
    }
    // Begins (re)connecting to the app.
    void start();
    bool isConnected() const {
        return m_connected;
    }

    // One message from the MCP client.
    void fromClient(const QByteArray& message);

    static QString startToolName();

signals:
    // One message for the MCP client, without the trailing newline.
    void toClient(const QByteArray& message);

private:
    void handleOffline(const QJsonValue& id, const QString& method, const QJsonObject& params);
    void onConnected();
    void onDisconnected();
    void onAppLine(const QByteArray& line);
    void sendToApp(const QJsonObject& object);
    void sendToClient(const QJsonObject& object);
    void notifyToolsChanged();
    void finishStartCalls(bool started);

    QString m_socketName;
    QLocalSocket* m_socket;
    QTimer m_retryTimer;
    QTimer m_startTimer;
    Launcher m_launcher;
    int m_startTimeoutMs = 30000;
    bool m_connected = false;
    QByteArray m_appBuffer;

    // The client's initialize params, replayed to the app on every connect
    // so it learns who is driving it.
    QJsonObject m_clientInitialize;
    bool m_clientInitialized = false;

    struct Pending {
        QJsonValue id;
        QString method;
    };
    QHash<QString, Pending> m_pending;
    // start_traceview calls waiting for the app to come up.
    QVector<QJsonValue> m_startCalls;
};

// main()'s entry point for `--mcp`: sets up a QCoreApplication, pumps
// stdin/stdout through an McpStdioRelay and returns when stdin closes.
int runMcpStdioRelay(int argc, char* argv[]);

}  // namespace traceview
