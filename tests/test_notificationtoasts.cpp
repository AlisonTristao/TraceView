#include <QtTest>
#include <QWidget>

#include "core/notificationtoasts.h"

using namespace traceview;

// Status messages as floating cards (notificationtoasts.h): they stack up to a
// limit, a repeat refreshes the newest card instead of stacking, the overlay
// never takes the mouse, sits in the area's bottom-right corner, and goes away
// once every card has expired.
class TestNotificationToasts : public QObject {
    Q_OBJECT

private slots:
    void cardsStackUpToTheLimit();
    void aRepeatRefreshesTheNewestCard();
    void staysOutOfTheMouseAndInTheCorner();
    void hidesOnceEveryCardExpires();
};

void TestNotificationToasts::cardsStackUpToTheLimit() {
    QWidget host;
    host.resize(800, 600);
    NotificationToasts toasts(&host);
    toasts.setArea(QRect(0, 0, 800, 600));
    for (int i = 0; i < 6; ++i) {
        toasts.post(QStringLiteral("message %1").arg(i), 5000, StatusSeverity::Info);
    }
    QCOMPARE(toasts.cardCount(), 4);
}

void TestNotificationToasts::aRepeatRefreshesTheNewestCard() {
    QWidget host;
    host.resize(800, 600);
    NotificationToasts toasts(&host);
    toasts.setArea(QRect(0, 0, 800, 600));
    toasts.post(QStringLiteral("command result: ok"), 5000, StatusSeverity::Success, "furuta");
    toasts.post(QStringLiteral("command result: ok"), 5000, StatusSeverity::Success, "furuta");
    QCOMPARE(toasts.cardCount(), 1);
    // Different source, severity or text: a card of its own.
    toasts.post(QStringLiteral("command result: ok"), 5000, StatusSeverity::Success, "other");
    toasts.post(QStringLiteral("command result: ok"), 5000, StatusSeverity::Error, "other");
    QCOMPARE(toasts.cardCount(), 3);
    // Blank text is ignored.
    toasts.post(QStringLiteral("  "), 5000, StatusSeverity::Info);
    QCOMPARE(toasts.cardCount(), 3);
}

void TestNotificationToasts::staysOutOfTheMouseAndInTheCorner() {
    QWidget host;
    host.resize(800, 600);
    host.show();
    NotificationToasts toasts(&host);
    const QRect area(10, 50, 700, 500);
    toasts.setArea(area);
    toasts.post(QStringLiteral("A long message that has to wrap onto more than one line in the "
                               "card, since it is longer than the card is wide."),
                5000, StatusSeverity::Warning, QStringLiteral("furuta"));
    // Only the cards take the mouse: the widget is masked to them.
    QVERIFY(!toasts.mask().isEmpty());
    QVERIFY(toasts.mask().boundingRect().size() == toasts.size());
    QVERIFY(toasts.isVisible());
    QVERIFY(area.contains(toasts.geometry()));
    QVERIFY(toasts.width() <= 380);
    // Flush with the area's bottom-right corner, minus the margin.
    QCOMPARE(area.right() - toasts.geometry().right(), 16);
    QCOMPARE(area.bottom() - toasts.geometry().bottom(), 16);
    // Wrapped: taller than a single line card.
    QVERIFY(toasts.height() > 2 * QFontMetrics(toasts.font()).lineSpacing());
}

void TestNotificationToasts::hidesOnceEveryCardExpires() {
    QWidget host;
    host.resize(800, 600);
    host.show();
    NotificationToasts toasts(&host);
    toasts.setArea(QRect(0, 0, 800, 600));
    toasts.post(QStringLiteral("short"), 1, StatusSeverity::Info);  // clamped to the minimum
    QVERIFY(toasts.isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!toasts.isVisible(), 5000);
    QCOMPARE(toasts.cardCount(), 0);
}

QTEST_MAIN(TestNotificationToasts)
#include "test_notificationtoasts.moc"
