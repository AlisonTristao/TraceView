#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "dashboard/widgets/chatprotocol.h"
#include "dashboard/widgets/chatwidget.h"

using traceview::buildChatSendCommand;
using traceview::ChatEvent;
using traceview::ChatEventTracker;
using traceview::ChatWidget;
using traceview::escapeTinyShellArgument;
using traceview::parseChatEventLine;
using traceview::parseChatEventLog;

namespace {

ChatEvent ev(quint32 id) {
    ChatEvent e;
    e.id = id;
    e.kind = ChatEvent::Kind::Incoming;
    return e;
}

QVector<quint32> ids(const QVector<ChatEvent>& events) {
    QVector<quint32> out;
    for (const ChatEvent& e : events) {
        out.append(e.id);
    }
    return out;
}

QJsonObject deviceConfig() {
    return QJsonObject{{"userName", "Alison"},
                       {"deviceId", "dev-1"},
                       {"topicName", "chat"},
                       {"sendCommand", "chat -send"},
                       {"sourceId", "0x11223344"},
                       {"topicId", "0x0005"}};
}

class TestChatProtocol : public QObject {
    Q_OBJECT

private slots:
    void escapesCommasAndBackslashes();
    void buildsTheSendCommand();
    void parsesEventLines();
    void rejectsMalformedLines();
    void parsesAWholeLog();
    void trackerSkipsEventsAlreadySeen();
    void trackerResetsOnReboot();

    void oldConfigStaysVisualOnly();
    void sendEmitsTheEscapedCommand();
    void notConnectedFailsAtOnce();
    void eventsDriveTheFeed();
};

void TestChatProtocol::escapesCommasAndBackslashes() {
    QCOMPARE(escapeTinyShellArgument(QStringLiteral("oi, tudo bem?")),
             QStringLiteral("oi\\, tudo bem?"));
    QCOMPARE(escapeTinyShellArgument(QStringLiteral("a\\b")), QStringLiteral("a\\\\b"));
    QCOMPARE(escapeTinyShellArgument(QStringLiteral("\\,")), QStringLiteral("\\\\\\,"));
    QCOMPARE(escapeTinyShellArgument(QStringLiteral("a\tb\nc")), QStringLiteral("a b c"));
    QCOMPARE(escapeTinyShellArgument(QStringLiteral("plain")), QStringLiteral("plain"));
}

void TestChatProtocol::buildsTheSendCommand() {
    QCOMPARE(buildChatSendCommand(QStringLiteral("chat -send"), 17, QStringLiteral("Alison"),
                                  QStringLiteral("oi, tudo bem?")),
             QStringLiteral("chat -send 17, Alison, oi\\, tudo bem?"));
    QCOMPARE(buildChatSendCommand(QStringLiteral(" chat -send "), 1, QStringLiteral("A,B"),
                                  QStringLiteral("x")),
             QStringLiteral("chat -send 1, A\\,B, x"));
}

void TestChatProtocol::parsesEventLines() {
    ChatEvent e;
    QVERIFY(parseChatEventLine(QStringLiteral("12\tIN\t1a2b\tBob\toi, tudo bem?"), &e));
    QCOMPARE(e.id, 12u);
    QCOMPARE(e.kind, ChatEvent::Kind::Incoming);
    QCOMPARE(e.token, QStringLiteral("1a2b"));
    QCOMPARE(e.author, QStringLiteral("Bob"));
    QCOMPARE(e.text, QStringLiteral("oi, tudo bem?"));

    QVERIFY(parseChatEventLine(QStringLiteral("13\tSENT\t17\tAlison\t\r"), &e));
    QCOMPARE(e.kind, ChatEvent::Kind::Sent);
    QCOMPARE(e.token, QStringLiteral("17"));
    QVERIFY(e.text.isEmpty());

    QVERIFY(parseChatEventLine(QStringLiteral("14\tFAIL\t18\tAlison\tno ack"), &e));
    QCOMPARE(e.kind, ChatEvent::Kind::Failed);
    QCOMPARE(e.text, QStringLiteral("no ack"));

    QVERIFY(parseChatEventLine(QStringLiteral("15\tERR\t\t\tBTP auth failed"), &e));
    QCOMPARE(e.kind, ChatEvent::Kind::Error);
    QCOMPARE(e.text, QStringLiteral("BTP auth failed"));

    QVERIFY(parseChatEventLine(QStringLiteral("16\tPING\t\t\t"), &e));
    QCOMPARE(e.kind, ChatEvent::Kind::Unknown);
    QCOMPARE(e.id, 16u);
}

void TestChatProtocol::rejectsMalformedLines() {
    ChatEvent e;
    QVERIFY(!parseChatEventLine(QString(), &e));
    QVERIFY(!parseChatEventLine(QStringLiteral("12\tIN\t1\tBob"), &e));
    QVERIFY(!parseChatEventLine(QStringLiteral("x\tIN\t1\tBob\thi"), &e));
    QVERIFY(!parseChatEventLine(QStringLiteral("-1\tIN\t1\tBob\thi"), &e));
}

void TestChatProtocol::parsesAWholeLog() {
    const QVector<ChatEvent> events = parseChatEventLog(
        QStringLiteral("1\tIN\tab\tBob\thi\ngarbage\n2\tSENT\t5\tAlison\t\n\n3\tERR\t\t\tbad\n"));
    QCOMPARE(ids(events), (QVector<quint32>{1, 2, 3}));
}

void TestChatProtocol::trackerSkipsEventsAlreadySeen() {
    ChatEventTracker tracker;
    QCOMPARE(ids(tracker.takeNew({ev(1), ev(2), ev(3)})), (QVector<quint32>{1, 2, 3}));
    QCOMPARE(ids(tracker.takeNew({ev(1), ev(2), ev(3)})), QVector<quint32>{});
    QCOMPARE(ids(tracker.takeNew({ev(2), ev(3), ev(4), ev(5)})),
             (QVector<quint32>{4, 5}));
    QCOMPARE(tracker.lastId(), 5u);
    // Within the reboot slack: still the same boot, nothing new.
    QCOMPARE(ids(tracker.takeNew({ev(1)})), QVector<quint32>{});
}

void TestChatProtocol::trackerResetsOnReboot() {
    ChatEventTracker tracker;
    tracker.takeNew({ev(40), ev(41), ev(42)});
    // 42 - 33 = 9 > 8: the first line sits too far below, the device rebooted.
    QCOMPARE(ids(tracker.takeNew({ev(33), ev(34)})), (QVector<quint32>{33, 34}));
    QCOMPARE(tracker.lastId(), 34u);
    // Exactly 8 below is still the same boot (a full 8-line window).
    ChatEventTracker same;
    same.takeNew({ev(20)});
    QCOMPARE(ids(same.takeNew({ev(12), ev(21)})), QVector<quint32>{21});
    // After a reboot to id 1 from far away.
    ChatEventTracker rebooted;
    rebooted.takeNew({ev(100)});
    QCOMPARE(ids(rebooted.takeNew({ev(1), ev(2)})), (QVector<quint32>{1, 2}));
}

void TestChatProtocol::oldConfigStaysVisualOnly() {
    ChatWidget chat;
    chat.setConfig(QJsonObject{{"userName", "Alison"}, {"ownMessagesOnRight", false}});
    QVERIFY(!chat.isDeviceMode());
    QVERIFY(chat.topicRequests().isEmpty());
    QSignalSpy spy(&chat, &ChatWidget::sendRequested);
    chat.sendText(QStringLiteral("hello"));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(chat.messageCount(), 1);
    QCOMPARE(chat.messageStatus(0), ChatWidget::MessageStatus::Sending);
    QTRY_COMPARE(chat.messageStatus(0), ChatWidget::MessageStatus::Sent);
}

void TestChatProtocol::sendEmitsTheEscapedCommand() {
    ChatWidget chat;
    chat.setConfig(deviceConfig());
    QVERIFY(chat.isDeviceMode());
    const QVector<traceview::TelemetryTopicRequest> requests = chat.topicRequests();
    QCOMPARE(requests.size(), 1);
    QCOMPARE(requests[0].deviceId, QStringLiteral("dev-1"));
    QCOMPARE(requests[0].sourceId, 0x11223344u);
    QCOMPARE(requests[0].topicId, quint16(5));
    QCOMPARE(requests[0].rateMillihz, 5000u);  // 200 ms

    QSignalSpy spy(&chat, &ChatWidget::sendRequested);
    chat.sendText(QStringLiteral("oi, tudo bem?"));
    QCOMPARE(spy.count(), 1);
    const quint32 token = spy.at(0).at(1).toUInt();
    QCOMPARE(spy.at(0).at(0).toByteArray(),
             QStringLiteral("chat -send %1, Alison, oi\\, tudo bem?").arg(token).toUtf8());
    QCOMPARE(chat.messageStatus(0), ChatWidget::MessageStatus::Sending);

    chat.sendText(QStringLiteral("again"));
    QCOMPARE(spy.count(), 2);
    QVERIFY(spy.at(1).at(1).toUInt() != token);
}

void TestChatProtocol::notConnectedFailsAtOnce() {
    ChatWidget chat;
    chat.setConfig(deviceConfig());
    connect(&chat, &ChatWidget::sendRequested, &chat,
            [&chat](const QByteArray&, quint32 token) {
                chat.markSendFailed(token, QStringLiteral("Device not connected"));
            });
    chat.sendText(QStringLiteral("hi"));
    QCOMPARE(chat.messageStatus(0), ChatWidget::MessageStatus::Failed);
    QCOMPARE(chat.messageToolTip(0), QStringLiteral("Device not connected"));
}

void TestChatProtocol::eventsDriveTheFeed() {
    ChatWidget chat;
    chat.setConfig(deviceConfig());
    QSignalSpy spy(&chat, &ChatWidget::sendRequested);
    chat.sendText(QStringLiteral("first"));
    chat.sendText(QStringLiteral("second"));
    const quint32 first = spy.at(0).at(1).toUInt();
    const quint32 second = spy.at(1).at(1).toUInt();

    const QString log = QStringLiteral("7\tSENT\t%1\tAlison\t\n"
                                       "8\tIN\tbeef\tBob\toi, Alison\n"
                                       "9\tFAIL\t%2\tAlison\tno ack\n"
                                       "10\tERR\t\t\tframe corrupted\n"
                                       "11\tNOPE\t\t\tignored\n")
                            .arg(first)
                            .arg(second);
    // Another topic: ignored.
    chat.onTextSample(0x11223344, 6, 0, log);
    QCOMPARE(chat.messageCount(), 2);

    chat.onTextSample(0x11223344, 5, 0, log);
    QCOMPARE(chat.messageStatus(0), ChatWidget::MessageStatus::Sent);
    QCOMPARE(chat.messageStatus(1), ChatWidget::MessageStatus::Failed);
    QCOMPARE(chat.messageToolTip(1), QStringLiteral("no ack"));
    QCOMPARE(chat.messageCount(), 4);
    QCOMPARE(chat.messageAuthor(2), QStringLiteral("Bob"));
    QCOMPARE(chat.messageText(2), QStringLiteral("oi, Alison"));
    QCOMPARE(chat.messageStatus(2), ChatWidget::MessageStatus::Received);
    QVERIFY(!chat.isSystemNotice(2));
    QVERIFY(chat.isSystemNotice(3));
    QCOMPARE(chat.messageText(3), QStringLiteral("frame corrupted"));

    // The same log again (the next sample): nothing new.
    chat.onTextSample(0x11223344, 5, 0, log);
    QCOMPARE(chat.messageCount(), 4);
}

}  // namespace

QTEST_MAIN(TestChatProtocol)
#include "test_chatprotocol.moc"
