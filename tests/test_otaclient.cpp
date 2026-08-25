#include <QSignalSpy>
#include <QString>
#include <QtTest>

#include "ota/otaclient.h"

using traceview::OtaClient;

namespace {

// TEST-NET-1 (RFC 5737). Reserved for documentation and guaranteed not to
// be routed, so a request to it fails on its own without ever reaching a
// real host -- which is what lets "a request is in flight" be observed here
// without a server to talk to.
const QString kUnroutableHost = QStringLiteral("192.0.2.1");

// Comfortably past OtaClient's own 4s status timeout, so a test that waits
// for the single emission never races it.
constexpr int kStatusSettleMs = 12000;

}  // namespace

class TestOtaClient : public QObject {
    Q_OBJECT

private slots:
    void emptyAddressReportsUnreachableWithAReason();
    void requestIsInFlightUntilItAnswers();
    void repeatedPollsCoalesceIntoTheRequestAlreadyRunning();
    void differentDevicesDoNotCoalesceIntoEachOther();
};

void TestOtaClient::emptyAddressReportsUnreachableWithAReason() {
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::statusChecked);

    client.checkStatus(QStringLiteral("dev-1"), QString());

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("dev-1"));
    QCOMPARE(spy.at(0).at(1).toBool(), false);  // reachable
    // Not just "false": OtaTab shows this as the status cell's tooltip, and
    // "Offline" with an empty tooltip is exactly the case that used to make
    // an unconfigured row indistinguishable from an unreachable one.
    QVERIFY(!spy.at(0).at(4).toString().isEmpty());  // errorMessage

    // Nothing was started, so nothing is outstanding.
    QVERIFY(!client.statusRequestInFlight(QStringLiteral("dev-1")));
}

void TestOtaClient::requestIsInFlightUntilItAnswers() {
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::statusChecked);

    client.checkStatus(QStringLiteral("dev-1"), kUnroutableHost);
    QVERIFY(client.statusRequestInFlight(QStringLiteral("dev-1")));
    QCOMPARE(spy.count(), 0);  // nothing reported yet

    QVERIFY(spy.wait(kStatusSettleMs));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QVERIFY(!client.statusRequestInFlight(QStringLiteral("dev-1")));
}

void TestOtaClient::repeatedPollsCoalesceIntoTheRequestAlreadyRunning() {
    // The regression this guards: OtaTab polls every second, a status
    // request is allowed four. When a repeat poll aborted the running
    // request instead of coalescing, a device slower than the poll interval
    // had every attempt cancelled by the next tick and never resolved once
    // -- its row sat on "Checking..." forever, which is precisely the
    // "host is up but slow" case worth reporting.
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::statusChecked);

    client.checkStatus(QStringLiteral("dev-1"), kUnroutableHost);
    for (int poll = 0; poll < 5; ++poll) {
        client.checkStatus(QStringLiteral("dev-1"), kUnroutableHost);
        QVERIFY(client.statusRequestInFlight(QStringLiteral("dev-1")));
    }
    QCOMPARE(spy.count(), 0);

    QVERIFY(spy.wait(kStatusSettleMs));
    // Exactly one -- six calls, one answer. The five repeats added no
    // second request and no second emission racing back to the same row.
    QCOMPARE(spy.count(), 1);
    QVERIFY(!client.statusRequestInFlight(QStringLiteral("dev-1")));
}

void TestOtaClient::differentDevicesDoNotCoalesceIntoEachOther() {
    // Coalescing is keyed by Device::id, so one slow row must not suppress
    // any other row's poll.
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::statusChecked);

    client.checkStatus(QStringLiteral("dev-1"), kUnroutableHost);
    client.checkStatus(QStringLiteral("dev-2"), kUnroutableHost);
    QVERIFY(client.statusRequestInFlight(QStringLiteral("dev-1")));
    QVERIFY(client.statusRequestInFlight(QStringLiteral("dev-2")));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, kStatusSettleMs);

    QSet<QString> reported;
    for (const QList<QVariant>& emission : spy) {
        reported.insert(emission.at(0).toString());
    }
    QCOMPARE(reported, QSet<QString>({QStringLiteral("dev-1"), QStringLiteral("dev-2")}));
}

QTEST_MAIN(TestOtaClient)
#include "test_otaclient.moc"
