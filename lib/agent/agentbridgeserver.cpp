#include "agent/agentbridgeserver.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QtGlobal>
#if !defined(Q_OS_IOS)
#include <QProcess>  // none on iOS, see launchAgentBridgeApp()
#endif

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
#if defined(Q_OS_IOS)
    // No QProcess, and an app cannot start another copy of itself.
    return QStringLiteral(
        "TraceView can't be started from here on this platform. Ask the user to open TraceView "
        "and turn on Settings > Connections > AI assistants.");
#else
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString program = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    if (!QProcess::startDetached(program, {QString::fromLatin1(kAgentBridgeArgument)})) {
        return QStringLiteral("Could not start TraceView (%1).").arg(program);
    }
    return QString();
#endif
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
        m_buffers.insert(socket, QByteArray());
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
            m_dispatching.remove(socket);
            const QString name = gone ? gone->clientName() : QString();
            socket->deleteLater();
            emit clientDisconnected(name);
        });
    }
}

void AgentBridgeServer::onReadyRead(QLocalSocket* socket) {
    if (!m_sessions.contains(socket)) {
        return;
    }
    m_buffers[socket] += socket->readAll();
    if (m_dispatching.contains(socket)) {
        return;  // nested inside a tool's modal dialog; the outer loop drains it
    }
    m_dispatching.insert(socket);
    for (;;) {
        // Looked up again every pass: a tool may stop() the server or the
        // client may disconnect from inside receive(), dropping the entry.
        auto it = m_buffers.find(socket);
        McpSession* session = m_sessions.value(socket);
        if (it == m_buffers.end() || !session) {
            break;
        }
        const int newline = it->indexOf('\n');
        if (newline < 0) {
            if (it->size() > kMaxMessageBytes) {
                socket->disconnectFromServer();
            }
            break;
        }
        const QByteArray line = it->left(newline).trimmed();
        it->remove(0, newline + 1);
        if (!line.isEmpty()) {
            session->receive(line);
        }
    }
    m_dispatching.remove(socket);
}

}  // namespace traceview
