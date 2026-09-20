#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "core/tcptransport.h"

using traceview::TcpTransport;

namespace {

// RFC 5737 TEST-NET-1: reserved for documentation, guaranteed not to be
// routed anywhere -- same address test_otaclient.cpp uses for the same
// reason (a connect attempt that reliably never gets a SYN-ACK or an RST).
const QString kUnroutableHost = QStringLiteral("192.0.2.1");

class TestTcpTransport : public QObject {
    Q_OBJECT

private slots:
    void startsDisconnectedAndRejectsInvalidOpen();
    void connectsAndExchangesBytesAsynchronously();
    void queuesFramesWithoutMergingOrDroppingThem();
    void reconnectsAfterUnexpectedPeerDisconnect();
    void closeEmitsDisconnected();
    void receivesFragmentedWritesAsSeparateEmissionsPreservingOrder();
    void receivesMultipleFramesWrittenTogetherWithoutLosingBytes();
    void connectTimesOutAgainstAnUnreachableAddress();
    void manyQueuedWritesToASlowPeerArriveIntactOnceDrained();
};

void TestTcpTransport::startsDisconnectedAndRejectsInvalidOpen() {
    TcpTransport transport;
    QVERIFY(!transport.isConnected());
    QVERIFY(!transport.open(QString(), 44300));
    QVERIFY(!transport.open(QStringLiteral("127.0.0.1"), 0));
    QVERIFY(!transport.write("not connected"));
}

void TestTcpTransport::connectsAndExchangesBytesAsynchronously() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    QSignalSpy connectedSpy(&transport, &TcpTransport::connectionStateChanged);
    QSignalSpy receivedSpy(&transport, &TcpTransport::dataReceived);
    QSignalSpy errorsSpy(&transport, &TcpTransport::errorOccurred);

    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QVERIFY(!transport.isConnected());
    QTRY_COMPARE_WITH_TIMEOUT(connectedSpy.count(), 1, 2000);
    QVERIFY(transport.isConnected());
    QCOMPARE(errorsSpy.count(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);

    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    QVERIFY(transport.write(QByteArrayLiteral("from-client")));
    QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= 11, 1000);
    QCOMPARE(peer->readAll(), QByteArrayLiteral("from-client"));

    peer->write(QByteArrayLiteral("from-server"));
    QVERIFY(peer->waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(receivedSpy.count(), 1, 1000);
    QCOMPARE(receivedSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("from-server"));

    peer->deleteLater();
}

void TestTcpTransport::closeEmitsDisconnected() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    QSignalSpy stateSpy(&transport, &TcpTransport::connectionStateChanged);
    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 2000);
    QVERIFY(transport.isConnected());

    transport.close();
    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 2, 1000);
    QVERIFY(!transport.isConnected());
}

void TestTcpTransport::reconnectsAfterUnexpectedPeerDisconnect() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    transport.setReconnectDelayForTesting(20);
    QSignalSpy stateSpy(&transport, &TcpTransport::connectionStateChanged);
    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 1, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);

    QTcpSocket* firstPeer = server.nextPendingConnection();
    QVERIFY(firstPeer != nullptr);
    firstPeer->abort();
    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() >= 2, 1000);

    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() >= 3, 2000);
    QVERIFY(transport.isConnected());
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    QTcpSocket* secondPeer = server.nextPendingConnection();
    QVERIFY(secondPeer != nullptr);

    transport.close();
    QTRY_COMPARE_WITH_TIMEOUT(stateSpy.count(), 4, 1000);
    QVERIFY(!transport.isConnected());
    secondPeer->deleteLater();
}

void TestTcpTransport::queuesFramesWithoutMergingOrDroppingThem() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(transport.isConnected(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const QByteArray first = QByteArrayLiteral("frame-one");
    const QByteArray second = QByteArrayLiteral("frame-two");
    QVERIFY(transport.write(first));
    QVERIFY(transport.write(second));
    QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= first.size() + second.size(), 1000);
    QCOMPARE(peer->readAll(), first + second);
    QCOMPARE(transport.pendingFrameCount(), 0);
    peer->deleteLater();
}

// T24: raw bytes arriving across several small readyRead() emissions must
// come out in the same order with nothing merged or dropped -- who
// reassembles them into a BTP frame (BtpSession) is out of scope here; this
// is purely about TcpTransport::onReadyRead() forwarding exactly what
// QTcpSocket handed it, once per emission.
void TestTcpTransport::receivesFragmentedWritesAsSeparateEmissionsPreservingOrder() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    QSignalSpy receivedSpy(&transport, &TcpTransport::dataReceived);
    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(transport.isConnected(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const QByteArray chunk1 = QByteArrayLiteral("BTP-");
    const QByteArray chunk2 = QByteArrayLiteral("frame-");
    const QByteArray chunk3 = QByteArrayLiteral("payload");

    // Forcing one readyRead() per chunk (rather than hoping the OS segments
    // them apart) is what makes this deterministic: wait for each chunk to
    // actually arrive before writing the next one.
    peer->write(chunk1);
    QVERIFY(peer->waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(receivedSpy.count(), 1, 1000);

    peer->write(chunk2);
    QVERIFY(peer->waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(receivedSpy.count(), 2, 1000);

    peer->write(chunk3);
    QVERIFY(peer->waitForBytesWritten(1000));
    QTRY_COMPARE_WITH_TIMEOUT(receivedSpy.count(), 3, 1000);

    QCOMPARE(receivedSpy.at(0).at(0).toByteArray(), chunk1);
    QCOMPARE(receivedSpy.at(1).at(0).toByteArray(), chunk2);
    QCOMPARE(receivedSpy.at(2).at(0).toByteArray(), chunk3);

    peer->deleteLater();
}

// T24: the mirror case -- several logical frames the server sends back to
// back, in one go, must not merge into a garbled emission or lose bytes.
// Whether the OS/Qt happens to coalesce them into one readyRead() or not is
// not this test's business (that's an OS scheduling detail); what matters is
// that concatenating whatever TcpTransport actually emitted reproduces the
// exact bytes the server sent, in order.
void TestTcpTransport::receivesMultipleFramesWrittenTogetherWithoutLosingBytes() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    QSignalSpy receivedSpy(&transport, &TcpTransport::dataReceived);
    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(transport.isConnected(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    const QByteArray frameOne = QByteArrayLiteral("frame-one-payload");
    const QByteArray frameTwo = QByteArrayLiteral("frame-two-payload");
    const QByteArray expected = frameOne + frameTwo;

    // Both frames queued before a single flush -- the likely case is the OS
    // hands them to us as one segment/one readyRead(), but the assertion
    // below holds either way.
    peer->write(frameOne);
    peer->write(frameTwo);
    QVERIFY(peer->waitForBytesWritten(1000));

    QByteArray accumulated;
    QTRY_VERIFY_WITH_TIMEOUT((
        [&]() {
            accumulated.clear();
            for (const QList<QVariant>& call : receivedSpy) {
                accumulated += call.at(0).toByteArray();
            }
            return accumulated.size() >= expected.size();
        }()),
        1000);
    QCOMPARE(accumulated, expected);

    peer->deleteLater();
}

// T24: TcpTransport::onConnectTimeout() -- a peer that never completes the
// TCP handshake (here, an address RFC 5737 guarantees is never routed, so
// there is neither a SYN-ACK nor an RST to short-circuit the wait) must not
// hang forever; the configurable timeout lets this fire well under a second
// instead of the real 5s default.
void TestTcpTransport::connectTimesOutAgainstAnUnreachableAddress() {
    TcpTransport transport;
    // Isolates the timeout behavior itself: a reconnect attempt afterward
    // would just repeat the same unroutable-host wait, which is exactly what
    // reconnectsAfterUnexpectedPeerDisconnect() already covers against a real
    // peer.
    transport.setReconnectEnabled(false);
    transport.setConnectTimeoutForTesting(200);
    QSignalSpy errorsSpy(&transport, &TcpTransport::errorOccurred);
    QSignalSpy stateSpy(&transport, &TcpTransport::connectionStateChanged);

    QVERIFY(transport.open(kUnroutableHost, 44300));
    QVERIFY(!transport.isConnected());

    QTRY_VERIFY_WITH_TIMEOUT(errorsSpy.count() >= 1, 2000);
    QVERIFY(errorsSpy.at(0).at(0).toString().contains(QStringLiteral("timed out")));
    QVERIFY(!transport.isConnected());
    // Never actually connected, so the connected-state signal never fired.
    QCOMPARE(stateSpy.count(), 0);
}

// T24 "saturação": found, while writing this test, that QTcpSocket::write()
// always reports success and buffers internally without limit -- it never
// returns a short count for TcpTransport::drainPendingFrames() to react to,
// even against a peer that has not been accept()-ed at all (confirmed
// empirically: 20 writes of 2MB each all report accepted, with
// pendingFrameCount() reading back 0 after every one of them). That makes
// the kMaxPendingFrames=16 rejection in TcpTransport::write() unreachable
// through genuine backpressure in a test built on real sockets -- it would
// need a fake/mocked socket to exercise the >=16 branch at all. What IS
// realistically testable, and is the actual risk T18 cared about, is
// byte-level integrity under a burst that has not drained yet: this writes a
// batch of frames before the peer has even been accepted, then confirms
// every byte arrives, in order, once it finally is.
void TestTcpTransport::manyQueuedWritesToASlowPeerArriveIntactOnceDrained() {
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    TcpTransport transport;
    QVERIFY(transport.open(QStringLiteral("127.0.0.1"), server.serverPort()));
    QTRY_VERIFY_WITH_TIMEOUT(transport.isConnected(), 2000);

    QByteArray expected;
    for (int i = 0; i < 20; ++i) {
        QByteArray frame = QByteArrayLiteral("frame-") + QByteArray::number(i) +
                          QByteArray(500, char('a' + (i % 26)));
        expected += frame;
        QVERIFY(transport.write(frame));
    }
    QCOMPARE(transport.pendingFrameCount(), 0);

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1000);
    QTcpSocket* peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() >= expected.size(), 2000);
    QCOMPARE(peer->readAll(), expected);

    peer->deleteLater();
}

}  // namespace

QTEST_MAIN(TestTcpTransport)
#include "test_tcptransport.moc"
