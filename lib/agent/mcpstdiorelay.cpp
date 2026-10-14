#include "agent/mcpstdiorelay.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QThread>
#include <cstdio>

#include "agent/agentbridgeserver.h"
#include "agent/agenttool.h"
#include "agent/mcpsession.h"
#include "traceview/version.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace traceview {

namespace {

constexpr int kRetryIntervalMs = 1000;
constexpr int kMethodNotFound = -32601;
constexpr int kInternalError = -32603;
// Our own id for the initialize replayed to the app; its answer is dropped.
constexpr char kRelayInitializeId[] = "traceview-relay-initialize";

QString idKey(const QJsonValue& id) {
    return id.isString() ? QStringLiteral("s:") + id.toString()
                         : QStringLiteral("n:") + QString::number(id.toDouble(), 'g', 17);
}

QString offlineMessage() {
    return QStringLiteral(
        "TraceView is not reachable: it is closed, or its AI assistant access is off. Call "
        "start_traceview to open it, or ask the user to turn on Settings > Connections > AI "
        "assistants in the TraceView window that is already open.");
}

AgentTool startTool() {
    AgentTool tool;
    tool.name = McpStdioRelay::startToolName();
    tool.title = QStringLiteral("Start TraceView");
    tool.description = QStringLiteral(
        "TraceView is not reachable yet, so this is the only tool. It opens a new TraceView "
        "window with AI assistant access on and returns once its tools are available. If the "
        "user already has TraceView open, do not call this: ask them to turn on Settings > "
        "Connections > AI assistants instead, and the tools appear by themselves.");
    tool.handler = [](const QJsonObject&, const AgentToolReply&) {};
    return tool;
}

// Blocking reads of stdin on a thread of their own: a QSocketNotifier cannot
// watch a pipe handle on Windows.
class StdinReader : public QThread {
    Q_OBJECT

public:
    using QThread::QThread;

signals:
    void lineRead(const QByteArray& line);

protected:
    void run() override {
        QByteArray buffer;
        char chunk[4096];
        for (;;) {
#if defined(Q_OS_WIN)
            DWORD count = 0;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), chunk, sizeof(chunk), &count, nullptr) ||
                count == 0) {
                break;
            }
#else
            const ssize_t count = ::read(STDIN_FILENO, chunk, sizeof(chunk));
            if (count <= 0) {
                break;
            }
#endif
            buffer.append(chunk, int(count));
            int newline;
            while ((newline = buffer.indexOf('\n')) >= 0) {
                const QByteArray line = buffer.left(newline).trimmed();
                buffer.remove(0, newline + 1);
                if (!line.isEmpty()) {
                    emit lineRead(line);
                }
            }
        }
    }
};

void writeStdout(const QByteArray& message) {
    const QByteArray line = message + '\n';
#if defined(Q_OS_WIN)
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    for (int offset = 0; offset < line.size(); offset += int(written)) {
        if (!WriteFile(out, line.constData() + offset, DWORD(line.size() - offset), &written,
                       nullptr)) {
            return;
        }
    }
#else
    for (qsizetype offset = 0; offset < line.size();) {
        const ssize_t written =
            ::write(STDOUT_FILENO, line.constData() + offset, size_t(line.size() - offset));
        if (written <= 0) {
            return;
        }
        offset += written;
    }
#endif
}

}  // namespace

McpStdioRelay::McpStdioRelay(const QString& socketName, QObject* parent)
    : QObject(parent), m_socketName(socketName), m_socket(new QLocalSocket(this)) {
    m_launcher = [] { return launchAgentBridgeApp(); };
    m_retryTimer.setInterval(kRetryIntervalMs);
    connect(&m_retryTimer, &QTimer::timeout, this, [this] {
        if (m_socket->state() == QLocalSocket::UnconnectedState) {
            m_socket->connectToServer(m_socketName);
        }
    });
    m_startTimer.setSingleShot(true);
    connect(&m_startTimer, &QTimer::timeout, this, [this] { finishStartCalls(false); });

    connect(m_socket, &QLocalSocket::connected, this, &McpStdioRelay::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &McpStdioRelay::onDisconnected);
    connect(m_socket, &QLocalSocket::readyRead, this, [this] {
        m_appBuffer.append(m_socket->readAll());
        int newline;
        while ((newline = m_appBuffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_appBuffer.left(newline).trimmed();
            m_appBuffer.remove(0, newline + 1);
            if (!line.isEmpty()) {
                onAppLine(line);
            }
        }
    });
}

QString McpStdioRelay::startToolName() {
    return QStringLiteral("start_traceview");
}

void McpStdioRelay::start() {
    m_socket->connectToServer(m_socketName);
    m_retryTimer.start();
}

void McpStdioRelay::fromClient(const QByteArray& message) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendToClient(jsonRpcError(QJsonValue::Null, -32700, QStringLiteral("Parse error.")));
        return;
    }
    const QJsonObject object = document.object();
    const QString method = object.value("method").toString();
    const bool isRequest = object.contains("id") && !method.isEmpty();

    if (method == QLatin1String("initialize") && isRequest) {
        m_clientInitialize = object.value("params").toObject();
        m_clientInitialized = true;
        sendToClient(jsonRpcResult(
            object.value("id"),
            mcpInitializeResult(m_clientInitialize.value("protocolVersion").toString())));
        if (m_connected) {
            sendToApp(QJsonObject{{"jsonrpc", "2.0"},
                                  {"id", kRelayInitializeId},
                                  {"method", "initialize"},
                                  {"params", m_clientInitialize}});
        }
        return;
    }
    if (method == QLatin1String("ping") && isRequest) {
        sendToClient(jsonRpcResult(object.value("id"), QJsonObject()));
        return;
    }
    if (!isRequest) {
        // Notifications and stray responses only matter to a live app.
        if (m_connected) {
            sendToApp(object);
        }
        return;
    }
    if (!m_connected) {
        handleOffline(object.value("id"), method, object.value("params").toObject());
        return;
    }
    m_pending.insert(idKey(object.value("id")), {object.value("id"), method});
    sendToApp(object);
}

void McpStdioRelay::handleOffline(const QJsonValue& id, const QString& method,
                                  const QJsonObject& params) {
    if (method == QLatin1String("tools/list")) {
        sendToClient(jsonRpcResult(id, QJsonObject{{"tools", QJsonArray{startTool().toJson()}}}));
        return;
    }
    if (method != QLatin1String("tools/call")) {
        sendToClient(jsonRpcError(id, kMethodNotFound,
                                  QStringLiteral("Method \"%1\" is not supported.").arg(method)));
        return;
    }
    if (params.value("name").toString() != startToolName()) {
        sendToClient(jsonRpcResult(id, AgentToolResult::error(offlineMessage()).toJson()));
        return;
    }
    if (m_startCalls.isEmpty()) {
        const QString problem = m_launcher ? m_launcher() : QStringLiteral("No launcher.");
        if (!problem.isEmpty()) {
            sendToClient(jsonRpcResult(id, AgentToolResult::error(problem).toJson()));
            return;
        }
    }
    m_startCalls.append(id);
    m_startTimer.start(m_startTimeoutMs);
}

void McpStdioRelay::onConnected() {
    m_connected = true;
    m_appBuffer.clear();
    if (m_clientInitialized) {
        sendToApp(QJsonObject{{"jsonrpc", "2.0"},
                              {"id", kRelayInitializeId},
                              {"method", "initialize"},
                              {"params", m_clientInitialize}});
        sendToApp(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}});
        notifyToolsChanged();
    }
    finishStartCalls(true);
}

void McpStdioRelay::onDisconnected() {
    m_connected = false;
    m_appBuffer.clear();
    for (const Pending& pending : std::as_const(m_pending)) {
        if (pending.method == QLatin1String("tools/call")) {
            sendToClient(jsonRpcResult(
                pending.id,
                AgentToolResult::error(QStringLiteral("TraceView closed before answering."))
                    .toJson()));
        } else {
            sendToClient(
                jsonRpcError(pending.id, kInternalError, QStringLiteral("TraceView closed.")));
        }
    }
    m_pending.clear();
    if (m_clientInitialized) {
        notifyToolsChanged();
    }
}

void McpStdioRelay::onAppLine(const QByteArray& line) {
    const QJsonObject object = QJsonDocument::fromJson(line).object();
    const QJsonValue id = object.value("id");
    if (id.isString() && id.toString() == QLatin1String(kRelayInitializeId)) {
        return;
    }
    if (object.contains("id") && !object.contains("method")) {
        m_pending.remove(idKey(id));
    }
    emit toClient(line);
}

void McpStdioRelay::sendToApp(const QJsonObject& object) {
    m_socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    m_socket->write("\n");
}

void McpStdioRelay::sendToClient(const QJsonObject& object) {
    emit toClient(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void McpStdioRelay::notifyToolsChanged() {
    sendToClient(QJsonObject{{"jsonrpc", "2.0"}, {"method", "notifications/tools/list_changed"}});
}

void McpStdioRelay::finishStartCalls(bool started) {
    m_startTimer.stop();
    const AgentToolResult result =
        started ? AgentToolResult::text(
                      QStringLiteral("TraceView is running and its tools are now available."))
                : AgentToolResult::error(QStringLiteral(
                      "TraceView did not come up in time. Ask the user to check its window."));
    for (const QJsonValue& id : std::as_const(m_startCalls)) {
        sendToClient(jsonRpcResult(id, result.toJson()));
    }
    m_startCalls.clear();
}

int runMcpStdioRelay(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("TraceView");
    QCoreApplication::setOrganizationName("AlisonTristao");
    QCoreApplication::setApplicationVersion(traceview::kVersion);

    McpStdioRelay relay(AgentBridgeServer::socketName());
    QObject::connect(&relay, &McpStdioRelay::toClient, &writeStdout);

    StdinReader reader;
    QObject::connect(&reader, &StdinReader::lineRead, &relay, &McpStdioRelay::fromClient);
    // stdin closing is how an MCP client ends a stdio server.
    QObject::connect(&reader, &QThread::finished, &app, &QCoreApplication::quit);
    reader.start();
    relay.start();

    // Only the reader finishing quits the loop, so it is done by now.
    const int code = QCoreApplication::exec();
    reader.wait();
    return code;
}

}  // namespace traceview

#include "mcpstdiorelay.moc"
