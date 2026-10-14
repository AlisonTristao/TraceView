#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

#include "agent/agentbridgeserver.h"
#include "agent/agenttool.h"
#include "agent/devicefeedbuffer.h"
#include "agent/mcpsession.h"
#include "agent/mcpstdiorelay.h"
#include "agent/toolregistry.h"

using namespace traceview;

namespace {

QByteArray request(int id, const QString& method, const QJsonObject& params = QJsonObject()) {
    return QJsonDocument(
               QJsonObject{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}})
        .toJson(QJsonDocument::Compact);
}

QByteArray toolCall(int id, const QString& name, const QJsonObject& arguments = QJsonObject()) {
    return request(id, "tools/call", QJsonObject{{"name", name}, {"arguments", arguments}});
}

QJsonObject parse(const QByteArray& message) {
    return QJsonDocument::fromJson(message).object();
}

// Every message `spy` (on a send/toClient signal) caught, decoded.
QVector<QJsonObject> messages(const QSignalSpy& spy) {
    QVector<QJsonObject> list;
    for (const QList<QVariant>& args : spy) {
        list.append(parse(args.first().toByteArray()));
    }
    return list;
}

// The response to `id` among `spy`'s messages; empty if none arrived.
QJsonObject responseTo(const QSignalSpy& spy, int id) {
    for (const QJsonObject& message : messages(spy)) {
        if (message.value("id").toInt(-1) == id && !message.contains("method")) {
            return message;
        }
    }
    return QJsonObject();
}

bool hasListChanged(const QSignalSpy& spy) {
    for (const QJsonObject& message : messages(spy)) {
        if (message.value("method").toString() == "notifications/tools/list_changed") {
            return true;
        }
    }
    return false;
}

AgentTool echoTool() {
    AgentTool tool;
    tool.name = "echo";
    tool.description = "Echoes text.";
    tool.inputSchema = AgentSchema().string("text", "What to echo.", true).toJson();
    tool.handler = [](const QJsonObject& args, const AgentToolReply& reply) {
        reply(AgentToolResult::text(args.value("text").toString()));
    };
    return tool;
}

QString firstText(const QJsonObject& result) {
    return result.value("content").toArray().first().toObject().value("text").toString();
}

QString uniqueSocketName() {
    return QStringLiteral("traceview-mcp-test-") +
           QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}  // namespace

class TestAgent : public QObject {
    Q_OBJECT

private slots:
    void validatesArguments();
    void registryRejectsDuplicatesAndUnknownTools();
    void initializeNegotiatesVersion();
    void listsAndCallsTools();
    void reportsProtocolErrors();
    void answersLateRepliesOnce();
    void announcesToolChanges();
    void relayOfflineOffersStartTool();
    void relayStartWaitsForApp();
    void relayForwardsToApp();
    void relayFailsPendingCallsWhenAppCloses();
    void feedBufferKeepsTheTail();
    void serverCommandFollowsPackaging();
};

void TestAgent::validatesArguments() {
    const QJsonObject schema = AgentSchema()
                                   .string("name", "", true)
                                   .integer("count", "")
                                   .string("mode", "", false, {"a", "b"})
                                   .toJson();
    QVERIFY(validateAgentArguments(schema, {{"name", "x"}}).isEmpty());
    QVERIFY(validateAgentArguments(schema, {{"name", "x"}, {"count", 3}, {"mode", "b"}}).isEmpty());
    QVERIFY(validateAgentArguments(schema, {}).contains("name"));
    QVERIFY(validateAgentArguments(schema, {{"name", 1}}).contains("string"));
    QVERIFY(validateAgentArguments(schema, {{"name", "x"}, {"count", 1.5}}).contains("integer"));
    QVERIFY(validateAgentArguments(schema, {{"name", "x"}, {"mode", "c"}}).contains("one of"));
    QVERIFY(validateAgentArguments(schema, {{"name", "x"}, {"typo", 1}}).contains("Unknown"));
}

void TestAgent::registryRejectsDuplicatesAndUnknownTools() {
    ToolRegistry registry;
    QVERIFY(registry.registerTool(echoTool()));
    QVERIFY(!registry.registerTool(echoTool()));
    QCOMPARE(registry.tools().size(), 1);

    AgentToolResult result;
    registry.call("missing", {}, [&](const AgentToolResult& r) { result = r; });
    QVERIFY(result.isError);
    registry.call("echo", {}, [&](const AgentToolResult& r) { result = r; });
    QVERIFY(result.isError);  // missing required "text"
    registry.call("echo", {{"text", "hi"}}, [&](const AgentToolResult& r) { result = r; });
    QVERIFY(!result.isError);

    registry.unregisterTool("echo");
    QVERIFY(registry.tools().isEmpty());
}

void TestAgent::initializeNegotiatesVersion() {
    McpSession session(nullptr);
    QSignalSpy sent(&session, &McpSession::send);
    QSignalSpy identified(&session, &McpSession::clientIdentified);

    session.receive(request(
        1, "initialize",
        {{"protocolVersion", "2025-03-26"}, {"clientInfo", QJsonObject{{"name", "claude-code"}}}}));
    const QJsonObject result = responseTo(sent, 1).value("result").toObject();
    QCOMPARE(result.value("protocolVersion").toString(), QString("2025-03-26"));
    QVERIFY(result.value("capabilities")
                .toObject()
                .value("tools")
                .toObject()
                .value("listChanged")
                .toBool());
    QCOMPARE(result.value("serverInfo").toObject().value("name").toString(), QString("traceview"));
    QCOMPARE(identified.size(), 1);
    QCOMPARE(session.clientName(), QString("claude-code"));

    session.receive(request(2, "initialize", {{"protocolVersion", "1999-01-01"}}));
    QCOMPARE(responseTo(sent, 2).value("result").toObject().value("protocolVersion").toString(),
             mcpSupportedProtocolVersions().first());
}

void TestAgent::listsAndCallsTools() {
    ToolRegistry registry;
    registry.registerTool(echoTool());
    McpSession session(&registry);
    QSignalSpy sent(&session, &McpSession::send);

    session.receive(request(1, "tools/list"));
    const QJsonArray tools =
        responseTo(sent, 1).value("result").toObject().value("tools").toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.first().toObject().value("name").toString(), QString("echo"));
    QVERIFY(tools.first().toObject().contains("inputSchema"));

    session.receive(toolCall(2, "echo", {{"text", "hello"}}));
    const QJsonObject ok = responseTo(sent, 2).value("result").toObject();
    QCOMPARE(ok.value("isError").toBool(), false);
    QCOMPARE(firstText(ok), QString("hello"));

    // A bad call is a result with isError, so the model can read why.
    session.receive(toolCall(3, "echo", {{"text", 5}}));
    const QJsonObject bad = responseTo(sent, 3).value("result").toObject();
    QVERIFY(bad.value("isError").toBool());

    session.receive(request(4, "ping"));
    QVERIFY(responseTo(sent, 4).contains("result"));
}

void TestAgent::reportsProtocolErrors() {
    McpSession session(nullptr);
    QSignalSpy sent(&session, &McpSession::send);

    session.receive("{not json");
    QCOMPARE(parse(sent.takeLast().first().toByteArray())
                 .value("error")
                 .toObject()
                 .value("code")
                 .toInt(),
             -32700);

    session.receive(request(7, "resources/list"));
    QCOMPARE(responseTo(sent, 7).value("error").toObject().value("code").toInt(), -32601);

    // Notifications get no answer at all.
    const int before = sent.size();
    session.receive(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    QCOMPARE(sent.size(), before);
}

void TestAgent::answersLateRepliesOnce() {
    ToolRegistry registry;
    AgentToolReply stored;
    AgentTool slow;
    slow.name = "slow";
    slow.handler = [&stored](const QJsonObject&, const AgentToolReply& reply) { stored = reply; };
    registry.registerTool(slow);
    McpSession session(&registry);
    QSignalSpy sent(&session, &McpSession::send);

    session.receive(toolCall(9, "slow"));
    QVERIFY(responseTo(sent, 9).isEmpty());
    QVERIFY(stored);
    stored(AgentToolResult::text("done"));
    stored(AgentToolResult::text("again"));
    QCOMPARE(sent.size(), 1);
    QCOMPARE(firstText(responseTo(sent, 9).value("result").toObject()), QString("done"));
}

void TestAgent::announcesToolChanges() {
    ToolRegistry registry;
    McpSession session(&registry);
    QSignalSpy sent(&session, &McpSession::send);
    registry.registerTool(echoTool());
    QVERIFY(hasListChanged(sent));
}

void TestAgent::relayOfflineOffersStartTool() {
    McpStdioRelay relay(uniqueSocketName());
    relay.setLauncher([] { return QStringLiteral("cannot spawn"); });
    QSignalSpy toClient(&relay, &McpStdioRelay::toClient);

    // Initialize is answered by the relay itself, app or no app.
    relay.fromClient(request(1, "initialize", {{"protocolVersion", "2025-06-18"}}));
    QCOMPARE(responseTo(toClient, 1).value("result").toObject().value("protocolVersion").toString(),
             QString("2025-06-18"));

    relay.fromClient(request(2, "tools/list"));
    const QJsonArray tools =
        responseTo(toClient, 2).value("result").toObject().value("tools").toArray();
    QCOMPARE(tools.size(), 1);
    QCOMPARE(tools.first().toObject().value("name").toString(), McpStdioRelay::startToolName());

    relay.fromClient(toolCall(3, "add_widget"));
    QVERIFY(responseTo(toClient, 3).value("result").toObject().value("isError").toBool());

    // A launcher that cannot spawn the app fails the call right away.
    relay.fromClient(toolCall(4, McpStdioRelay::startToolName()));
    QVERIFY(responseTo(toClient, 4).value("result").toObject().value("isError").toBool());
}

void TestAgent::relayStartWaitsForApp() {
    const QString name = uniqueSocketName();
    ToolRegistry registry;
    registry.registerTool(echoTool());
    AgentBridgeServer server(&registry);
    server.setSocketName(name);

    McpStdioRelay relay(name);
    // "Launching" the app is bringing its bridge up.
    relay.setLauncher([&server] { return server.start() ? QString() : QStringLiteral("no"); });
    QSignalSpy toClient(&relay, &McpStdioRelay::toClient);
    relay.fromClient(request(1, "initialize", {{"protocolVersion", "2025-06-18"}}));

    relay.fromClient(toolCall(2, McpStdioRelay::startToolName()));
    QVERIFY(responseTo(toClient, 2).isEmpty());
    relay.start();
    QTRY_VERIFY(!responseTo(toClient, 2).isEmpty());
    QVERIFY(!responseTo(toClient, 2).value("result").toObject().value("isError").toBool());
    QVERIFY(hasListChanged(toClient));
}

void TestAgent::relayForwardsToApp() {
    const QString name = uniqueSocketName();
    ToolRegistry registry;
    registry.registerTool(echoTool());
    AgentBridgeServer server(&registry);
    server.setSocketName(name);
    QVERIFY(server.start());
    QSignalSpy connected(&server, &AgentBridgeServer::clientConnected);

    McpStdioRelay relay(name);
    QSignalSpy toClient(&relay, &McpStdioRelay::toClient);
    relay.fromClient(request(
        1, "initialize",
        {{"protocolVersion", "2025-06-18"}, {"clientInfo", QJsonObject{{"name", "test-client"}}}}));
    relay.start();
    QTRY_VERIFY(relay.isConnected());
    // The relay replays the client's initialize so the app knows who it is.
    QTRY_COMPARE(connected.size(), 1);
    QCOMPARE(connected.first().first().toString(), QString("test-client"));

    relay.fromClient(request(2, "tools/list"));
    QTRY_VERIFY(!responseTo(toClient, 2).isEmpty());
    QCOMPARE(responseTo(toClient, 2).value("result").toObject().value("tools").toArray().size(), 1);

    relay.fromClient(toolCall(3, "echo", {{"text", "through the pipe"}}));
    QTRY_VERIFY(!responseTo(toClient, 3).isEmpty());
    QCOMPARE(firstText(responseTo(toClient, 3).value("result").toObject()),
             QString("through the pipe"));

    // A second server on the same name refuses instead of stealing it.
    AgentBridgeServer second(&registry);
    second.setSocketName(name);
    QString error;
    QVERIFY(!second.start(&error));
    QVERIFY(!error.isEmpty());
}

void TestAgent::relayFailsPendingCallsWhenAppCloses() {
    const QString name = uniqueSocketName();
    ToolRegistry registry;
    AgentTool never;
    never.name = "never";
    never.handler = [](const QJsonObject&, const AgentToolReply&) {};
    registry.registerTool(never);
    AgentBridgeServer server(&registry);
    server.setSocketName(name);
    QVERIFY(server.start());

    McpStdioRelay relay(name);
    QSignalSpy toClient(&relay, &McpStdioRelay::toClient);
    relay.fromClient(request(1, "initialize", {{"protocolVersion", "2025-06-18"}}));
    relay.start();
    QTRY_VERIFY(relay.isConnected());

    relay.fromClient(toolCall(2, "never"));
    QTRY_COMPARE(server.clientCount(), 1);
    QTest::qWait(50);
    server.stop();
    QTRY_VERIFY(!responseTo(toClient, 2).isEmpty());
    QVERIFY(responseTo(toClient, 2).value("result").toObject().value("isError").toBool());
    QTRY_VERIFY(!relay.isConnected());
    QVERIFY(hasListChanged(toClient));
}

void TestAgent::feedBufferKeepsTheTail() {
    DeviceFeedBuffer feed;
    feed.appendTerminal("a", "hello ");
    const qint64 mark = feed.terminalReceived("a");
    feed.appendTerminal("a", "world");
    QCOMPARE(feed.terminal("a"), QString("hello world"));
    QCOMPARE(feed.terminal("a", 5), QString("world"));
    QCOMPARE(feed.terminalSince("a", mark), QString("world"));
    QVERIFY(feed.terminal("b").isEmpty());

    // Past capacity the oldest bytes go; the byte count keeps growing.
    feed.appendTerminal("a", QByteArray(DeviceFeedBuffer::kTerminalCapacityBytes, 'x'));
    QCOMPARE(feed.terminal("a").size(), DeviceFeedBuffer::kTerminalCapacityBytes);
    QCOMPARE(feed.terminalReceived("a"), qint64(11 + DeviceFeedBuffer::kTerminalCapacityBytes));

    // A cut UTF-8 sequence at the front is dropped, not shown as garbage.
    // "ete" with both e's accented: C3 A9 74 C3 A9. The last 4 bytes start
    // inside the first e.
    const QByteArray accented = QByteArray::fromHex("c3a974c3a9");
    feed.appendTerminal("u", accented);
    QCOMPARE(feed.terminal("u", 4), QString::fromUtf8(accented.mid(2)));

    for (int i = 0; i < DeviceFeedBuffer::kLogCapacity + 5; ++i) {
        feed.appendLog("a", {QDateTime::currentDateTime(), quint64(i), 1, QString::number(i)});
    }
    QCOMPARE(feed.logs("a").size(), DeviceFeedBuffer::kLogCapacity);
    QCOMPARE(feed.logs("a", 2).last().message, QString::number(DeviceFeedBuffer::kLogCapacity + 4));
    feed.forget("a");
    QVERIFY(feed.logs("a").isEmpty());
}

void TestAgent::serverCommandFollowsPackaging() {
    QCOMPARE(mcpServerCommand().last(), QString(kMcpRelayArgument));

    qputenv("APPIMAGE", "/home/u/TraceView-4.9.0-linux-x64.AppImage");
    QCOMPARE(mcpServerCommand(),
             QStringList({"/home/u/TraceView-4.9.0-linux-x64.AppImage", "--mcp"}));
    qunsetenv("APPIMAGE");

    qputenv("FLATPAK_ID", "io.github.alisontristao.TraceView");
    QCOMPARE(mcpServerCommand(),
             QStringList({"flatpak", "run", "io.github.alisontristao.TraceView", "--mcp"}));
    // A sandbox can't start an instance that outlives it.
    QVERIFY(!launchAgentBridgeApp().isEmpty());
    qunsetenv("FLATPAK_ID");
}

QTEST_MAIN(TestAgent)
#include "test_agent.moc"
