#include "agent/agentbridgeserver.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>

#include "agent/mcpsession.h"
#include "agent/toolregistry.h"

namespace traceview {

namespace {

// A peer that never sends a newline cannot grow our buffer without bound.
constexpr int kMaxMessageBytes = 16 * 1024 * 1024;

QString userTag() {
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) {
        user = qEnvironmentVariable("USER");
    }
    user.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return user.isEmpty() ? QStringLiteral("default") : user;
}

}  // namespace

QStringList mcpServerCommand() {
    const QString relay = QString::fromLatin1(kMcpRelayArgument);
    const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
    if (!flatpakId.isEmpty()) {
        return {QStringLiteral("flatpak"), QStringLiteral("run"), flatpakId, relay};
    }
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty()) {
        return {appImage, relay};
    }
    return {QDir::toNativeSeparators(QCoreApplication::applicationFilePath()), relay};
}

QString launchAgentBridgeApp() {
    if (!qEnvironmentVariableIsEmpty("FLATPAK_ID")) {
        return QStringLiteral(
            "TraceView runs as a Flatpak, which can't be started from here. Ask the user to "
            "open TraceView and turn on Settings > Connections > AI assistants.");
    }
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString program = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    if (!QProcess::startDetached(program, {QString::fromLatin1(kAgentBridgeArgument)})) {
        return QStringLiteral("Could not start TraceView (%1).").arg(program);
    }
    return QString();
}

AgentBridgeServer::AgentBridgeServer(ToolRegistry* registry, QObject* parent)
    : QObject(parent), m_registry(registry), m_socketName(socketName()) {}

AgentBridgeServer::~AgentBridgeServer() {
    stop();
}

QString AgentBridgeServer::socketName() {
    const QString name = QStringLiteral("traceview-mcp-") + userTag();
#if defined(Q_OS_UNIX)
    // Inside a Flatpak sandbox every instance gets its own /tmp; only the
    // per-app runtime directory is shared between them (and the relay is
    // another instance).
    const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!flatpakId.isEmpty() && !runtimeDir.isEmpty()) {
        return QDir(runtimeDir).filePath(QStringLiteral("app/%1/%2").arg(flatpakId, name));
    }
    if (!runtimeDir.isEmpty() && QDir(runtimeDir).exists()) {
        return QDir(runtimeDir).filePath(name);
    }
#endif
    return name;
}

bool AgentBridgeServer::start(QString* error) {
    if (isListening()) {
        return true;
    }
    const QString& name = m_socketName;
    {
        // Someone answering means another instance owns it; removeServer()
        // below would otherwise unlink that instance's live Unix socket.
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(200)) {
            if (error) {
                *error = tr("Another TraceView window is already serving AI assistants.");
            }
            return false;
        }
    }
    // A socket file left behind by a crashed instance blocks listen() on Unix.
    QLocalServer::removeServer(name);

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        if (error) {
            *error = m_server->errorString();
        }
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &AgentBridgeServer::onNewConnection);
    return true;
}

void AgentBridgeServer::stop() {
    const QList<QLocalSocket*> sockets = m_sessions.keys();
    for (QLocalSocket* socket : sockets) {
        socket->disconnectFromServer();
    }
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
}

bool AgentBridgeServer::isListening() const {
    return m_server && m_server->isListening();
}

void AgentBridgeServer::onNewConnection() {
    while (QLocalSocket* socket = m_server->nextPendingConnection()) {
        auto* session = new McpSession(m_registry, socket);
        m_sessions.insert(socket, session);
        connect(session, &McpSession::send, socket, [socket](const QByteArray& message) {
            socket->write(message);
            socket->write("\n");
        });
        connect(session, &McpSession::clientIdentified, this,
                [this](const QString& name) { emit clientConnected(name); });
        connect(session, &McpSession::toolCalled, this, [this, session](const QString& tool) {
            emit toolCalled(session->clientName(), tool);
        });
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            McpSession* gone = m_sessions.take(socket);
            m_buffers.remove(socket);
            const QString name = gone ? gone->clientName() : QString();
            socket->deleteLater();
            emit clientDisconnected(name);
        });
    }
}

void AgentBridgeServer::onReadyRead(QLocalSocket* socket) {
    // Taken out of the hash while dispatching: a tool may stop() the server
    // (and so drop this socket's entry) from inside receive().
    QByteArray buffer = m_buffers.take(socket) + socket->readAll();
    int newline;
    while ((newline = buffer.indexOf('\n')) >= 0) {
        const QByteArray line = buffer.left(newline).trimmed();
        buffer.remove(0, newline + 1);
        McpSession* session = m_sessions.value(socket);
        if (!session) {
            return;
        }
        if (!line.isEmpty()) {
            session->receive(line);
        }
    }
    if (!m_sessions.contains(socket)) {
        return;
    }
    if (buffer.size() > kMaxMessageBytes) {
        socket->disconnectFromServer();
        return;
    }
    m_buffers.insert(socket, buffer);
}

}  // namespace traceview
