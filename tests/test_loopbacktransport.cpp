#include <QSignalSpy>
#include <QtTest>
#include <memory>

#include "lab/plant/loopbacktransport.h"

using tracelab::LoopbackTransport;

class TestLoopbackTransport : public QObject {
    Q_OBJECT

private slots:
    void unpairedEndWritesNothing();
    void bytesArriveOnThePeerQueuedAndInOrder();
    void closeClosesBothEnds();
    void destroyingOneEndClosesTheOther();
};

void TestLoopbackTransport::unpairedEndWritesNothing() {
    LoopbackTransport end;
    QVERIFY(!end.isConnected());
    QVERIFY(!end.write("x"));
}

void TestLoopbackTransport::bytesArriveOnThePeerQueuedAndInOrder() {
    LoopbackTransport plant;
    LoopbackTransport viewer;
    QSignalSpy plantState(&plant, &LoopbackTransport::connectionStateChanged);
    QSignalSpy viewerState(&viewer, &LoopbackTransport::connectionStateChanged);
    plant.connectTo(&viewer);
    QVERIFY(plant.isConnected());
    QVERIFY(viewer.isConnected());
    QCOMPARE(plantState.count(), 1);
    QCOMPARE(viewerState.count(), 1);
    QVERIFY(plantState.at(0).at(0).toBool());

    QSignalSpy received(&viewer, &LoopbackTransport::dataReceived);
    QSignalSpy echoed(&plant, &LoopbackTransport::dataReceived);
    QVERIFY(plant.write("one"));
    QVERIFY(plant.write("two"));
    QCOMPARE(received.count(), 0);  // never inside write()
    QTRY_COMPARE(received.count(), 2);
    QCOMPARE(received.at(0).at(0).toByteArray(), QByteArray("one"));
    QCOMPARE(received.at(1).at(0).toByteArray(), QByteArray("two"));
    QCOMPARE(echoed.count(), 0);

    QVERIFY(viewer.write("back"));
    QTRY_COMPARE(echoed.count(), 1);
    QCOMPARE(echoed.at(0).at(0).toByteArray(), QByteArray("back"));
}

void TestLoopbackTransport::closeClosesBothEnds() {
    LoopbackTransport a;
    LoopbackTransport b;
    a.connectTo(&b);
    QSignalSpy bState(&b, &LoopbackTransport::connectionStateChanged);
    QSignalSpy bData(&b, &LoopbackTransport::dataReceived);
    QVERIFY(a.write("late"));
    a.close();
    QVERIFY(!a.isConnected());
    QVERIFY(!b.isConnected());
    QCOMPARE(bState.count(), 1);
    QVERIFY(!bState.at(0).at(0).toBool());
    QVERIFY(!a.write("x"));
    QCoreApplication::processEvents();
    QCOMPARE(bData.count(), 0);  // queued before the close, not delivered after it
}

void TestLoopbackTransport::destroyingOneEndClosesTheOther() {
    LoopbackTransport survivor;
    auto other = std::make_unique<LoopbackTransport>();
    survivor.connectTo(other.get());
    other.reset();
    QVERIFY(!survivor.isConnected());
    QVERIFY(!survivor.write("x"));
}

QTEST_GUILESS_MAIN(TestLoopbackTransport)
#include "test_loopbacktransport.moc"
