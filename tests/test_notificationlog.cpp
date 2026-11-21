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
    void unreadCountsAndWorstSeverity();
    void removeDismissesOneById();
    void removeOlderThanKeepsUnreadRight();
    void maxAgeDeletesExpiredEntries();
    void popUpRules();
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

void TestNotificationLog::unreadCountsAndWorstSeverity() {
    NotificationLog log;
    QSignalSpy spy(&log, &NotificationLog::unreadChanged);
    QCOMPARE(log.unreadCount(), 0);

    log.append(entry(QStringLiteral("a"), StatusSeverity::Success));
    log.append(entry(QStringLiteral("b"), StatusSeverity::Warning));
    log.append(entry(QStringLiteral("c"), StatusSeverity::Info));
    QCOMPARE(log.unreadCount(), 3);
    // The worst one wins, whatever came after it.
    QCOMPARE(log.unreadWorstSeverity(), StatusSeverity::Warning);
    QCOMPARE(spy.count(), 3);

    log.markAllRead();
    QCOMPARE(log.unreadCount(), 0);
    QCOMPARE(log.unreadWorstSeverity(), StatusSeverity::Info);
    QCOMPARE(log.entries().size(), 3);  // read, not gone
    log.markAllRead();
    QCOMPARE(spy.count(), 4);  // a second markAllRead() with nothing unread is silent

    log.append(entry(QStringLiteral("d"), StatusSeverity::Error));
    QCOMPARE(log.unreadWorstSeverity(), StatusSeverity::Error);
    log.clear();
    QCOMPARE(log.unreadCount(), 0);
}

void TestNotificationLog::removeDismissesOneById() {
    NotificationLog log;
    log.append(entry(QStringLiteral("first")));
    log.append(entry(QStringLiteral("second")));
    log.append(entry(QStringLiteral("third")));
    const quint64 first = log.entries().at(0).id;
    const quint64 second = log.entries().at(1).id;
    QVERIFY(first != 0);
    QVERIFY(first != second);

    QSignalSpy spy(&log, &NotificationLog::removed);
    QVERIFY(log.remove(second));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), second);
    QCOMPARE(log.entries().size(), 2);
    QCOMPARE(log.entries().at(1).text, QStringLiteral("third"));
    QVERIFY(!log.remove(second));  // already gone
    QCOMPARE(spy.count(), 1);
}

void TestNotificationLog::removeOlderThanKeepsUnreadRight() {
    const QDateTime now = QDateTime::currentDateTime();
    NotificationLog log;
    log.append({now.addSecs(-100), QStringLiteral("old error"), StatusSeverity::Error, QString()});
    log.markAllRead();
    log.append({now.addSecs(-50), QStringLiteral("old warning"), StatusSeverity::Warning, QString()});
    log.append({now, QStringLiteral("new"), StatusSeverity::Info, QString()});
    QCOMPARE(log.unreadCount(), 2);
    QCOMPARE(log.unreadWorstSeverity(), StatusSeverity::Warning);

    QSignalSpy removedSpy(&log, &NotificationLog::removed);
    QSignalSpy unreadSpy(&log, &NotificationLog::unreadChanged);
    QCOMPARE(log.removeOlderThan(now.addSecs(-10)), 2);
    QCOMPARE(removedSpy.count(), 2);
    QCOMPARE(log.entries().size(), 1);
    QCOMPARE(log.entries().first().text, QStringLiteral("new"));
    // The unread warning went with it: the badge no longer counts it.
    QCOMPARE(log.unreadCount(), 1);
    QCOMPARE(log.unreadWorstSeverity(), StatusSeverity::Info);
    QCOMPARE(unreadSpy.count(), 1);  // the read error leaves the badge alone
    QCOMPARE(log.removeOlderThan(now.addSecs(-10)), 0);
}

void TestNotificationLog::maxAgeDeletesExpiredEntries() {
    const QDateTime now = QDateTime::currentDateTime();
    NotificationLog log;
    log.setMaxAgeSeconds(0);
    log.append({now.addSecs(-120), QStringLiteral("stale"), StatusSeverity::Info, QString()});
    log.append({now, QStringLiteral("fresh"), StatusSeverity::Info, QString()});
    QCOMPARE(log.entries().size(), 2);  // off: nothing expires

    log.setMaxAgeSeconds(60);
    QCOMPARE(log.maxAgeSeconds(), 60);
    QCOMPARE(log.entries().size(), 1);  // turning it on sweeps at once
    QCOMPARE(log.entries().first().text, QStringLiteral("fresh"));

    // After that the once-a-second timer sweeps what ages out.
    log.setMaxAgeSeconds(1);
    log.append(entry(QStringLiteral("newest")));
    QTRY_VERIFY_WITH_TIMEOUT(log.entries().isEmpty(), 4000);

    log.setMaxAgeSeconds(0);
    log.append(entry(QStringLiteral("kept")));
    QTest::qWait(1500);
    QCOMPARE(log.entries().size(), 1);  // off again: it stays
}

void TestNotificationLog::popUpRules() {
    const auto pops = [](StatusSeverity severity, bool bellOnly, bool errorPopups, bool panelOpen) {
        return notificationPopsUp(severity, bellOnly, errorPopups, panelOpen);
    };
    // "Every notification": everything pops up...
    QVERIFY(pops(StatusSeverity::Info, false, false, false));
    QVERIFY(pops(StatusSeverity::Error, false, false, false));
    // ...unless the panel is open: it shows up there instead.
    QVERIFY(!pops(StatusSeverity::Error, false, true, true));
    // "Only the bell" (the default) with errors on: only errors pop up.
    QVERIFY(!pops(StatusSeverity::Info, true, true, false));
    QVERIFY(!pops(StatusSeverity::Warning, true, true, false));
    QVERIFY(pops(StatusSeverity::Error, true, true, false));
    // ...and with errors off, nothing does.
    QVERIFY(!pops(StatusSeverity::Error, true, false, false));
}

}  // namespace

QTEST_MAIN(TestNotificationLog)
#include "test_notificationlog.moc"
