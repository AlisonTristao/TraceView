#include <QtTest/QtTest>

#include "diagnostics/notificationlog.h"

using namespace traceview;

namespace {

NotificationEntry entry(const QString& text,
                        StatusSeverity severity = StatusSeverity::Info) {
    return {QDateTime::currentDateTime(), text, severity, QString()};
}

class TestNotificationLog : public QObject {
    Q_OBJECT

private slots:
    void appendKeepsOrderAndEmits();
    void ringDropsOldestPastCapacity();
    void clearEmitsOnlyWhenNonEmpty();
};

void TestNotificationLog::appendKeepsOrderAndEmits() {
    NotificationLog log;
    QSignalSpy spy(&log, &NotificationLog::entryAdded);

    log.append(entry(QStringLiteral("first")));
    log.append(entry(QStringLiteral("second"), StatusSeverity::Error));

    QCOMPARE(log.entries().size(), 2);
    QCOMPARE(log.entries().first().text, QStringLiteral("first"));
    QCOMPARE(log.entries().last().severity, StatusSeverity::Error);
    QCOMPARE(spy.count(), 2);
    // droppedOldest is the second argument, false while under capacity.
    QCOMPARE(spy.at(0).at(1).toBool(), false);
}

void TestNotificationLog::ringDropsOldestPastCapacity() {
    NotificationLog log;
    for (int i = 0; i < NotificationLog::kCapacity + 25; ++i) {
        log.append(entry(QStringLiteral("msg %1").arg(i)));
    }

    QCOMPARE(log.entries().size(), NotificationLog::kCapacity);
    // The first 25 were evicted; the oldest survivor is #25.
    QCOMPARE(log.entries().first().text, QStringLiteral("msg 25"));
    QCOMPARE(log.entries().last().text,
             QStringLiteral("msg %1").arg(NotificationLog::kCapacity + 24));
}

void TestNotificationLog::clearEmitsOnlyWhenNonEmpty() {
    NotificationLog log;
    QSignalSpy spy(&log, &NotificationLog::cleared);

    log.clear();
    QCOMPARE(spy.count(), 0);  // nothing to clear

    log.append(entry(QStringLiteral("x")));
    log.clear();
    QCOMPARE(spy.count(), 1);
    QVERIFY(log.entries().isEmpty());
}

}  // namespace

QTEST_MAIN(TestNotificationLog)
#include "test_notificationlog.moc"
