#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QtTest>

#include "ota/otaauth.h"
#include "ota/otaclient.h"

using traceview::OtaClient;
using traceview::RecoveryStatus;

namespace {

// TEST-NET-1 (RFC 5737). Reserved for documentation and guaranteed not to
// be routed, so a request to it fails on its own without ever reaching a
// real host -- which is what lets "a request is in flight" be observed here
// without a server to talk to.
const QString kUnroutableHost = QStringLiteral("192.0.2.1");

// Comfortably past OtaClient's own 4s status timeout, so a test that waits
// for the single emission never races it.
constexpr int kStatusSettleMs = 12000;

// A TraceKit recovery in miniature: GET /status with a fixed nonce, POST
// /update and /reboot recorded as they arrive. Enough HTTP/1.1 for
// QNetworkAccessManager (Content-Length bodies, keep-alive).
class FakeRecovery : public QObject {
public:
    struct Request {
        QByteArray method, path, body;
        QHash<QByteArray, QByteArray> headers;  // lowercase names
    };

    QByteArray nonce = QByteArray::fromHex("a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    QString auth = QStringLiteral("hmac-sha256");
    qint64 maxSize = 1 << 20;
    QVector<Request> requests;

    FakeRecovery() {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { serve(socket); });
            }
        });
        m_server.listen(QHostAddress::LocalHost);
    }
    QString address() const { return QStringLiteral("127.0.0.1:%1").arg(m_server.serverPort()); }

private:
    void serve(QTcpSocket* socket) {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        for (;;) {
            const int end = buffer.indexOf("\r\n\r\n");
            if (end < 0) return;
            Request request;
            const QList<QByteArray> lines = buffer.left(end).split('\n');
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            request.method = first.value(0);
            request.path = first.value(1);
            for (int i = 1; i < lines.size(); ++i) {
                const int colon = lines[i].indexOf(':');
                if (colon > 0) {
                    request.headers.insert(lines[i].left(colon).trimmed().toLower(),
                                           lines[i].mid(colon + 1).trimmed());
                }
            }
            const int length = request.headers.value("content-length", "0").toInt();
            if (buffer.size() < end + 4 + length) return;  // body still arriving
            request.body = buffer.mid(end + 4, length);
            buffer.remove(0, end + 4 + length);
            requests.append(request);
            reply(socket, request);
        }
    }

    void reply(QTcpSocket* socket, const Request& request) {
        QByteArray body;
        QByteArray status = "200 OK";
        if (request.path == "/status") {
            body = QJsonDocument(QJsonObject{
                                     {"mode", "recovery"},
                                     {"name", "bancada"},
                                     {"chip", "esp32s3"},
                                     {"recovery", "0.1.0"},
                                     {"reason", "command"},
                                     {"ota_ready", true},
                                     {"auth", auth},
                                     {"nonce", QString::fromLatin1(nonce.toHex())},
                                     {"max_size", static_cast<double>(maxSize)},
                                     {"app", QJsonObject{{"valid", true},
                                                         {"project", "sensor"},
                                                         {"version", "1.2.0"}}},
                                 })
                       .toJson(QJsonDocument::Compact);
        } else if (request.path == "/update") {
            body = "OK. Rebooting into sensor 1.3.0.";
        } else if (request.path == "/reboot") {
            body = "OK. Rebooting into the application.";
        } else {
            status = "404 Not Found";
        }
        socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                      "\r\nContent-Type: text/plain\r\n\r\n" + body);
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

QByteArray testKey() {
    QByteArray key;
    for (int i = 0; i < 16; ++i) key.append(static_cast<char>(i));
    return key;
}

// Writes `content` to a temporary file and returns its path.
QString writeImage(QTemporaryFile& file, const QByteArray& content) {
    file.open();
    file.write(content);
    file.close();
    return file.fileName();
}

}  // namespace

class TestOtaClient : public QObject {
    Q_OBJECT

private slots:
    void emptyAddressReportsUnreachableWithAReason();
    void requestIsInFlightUntilItAnswers();
    void repeatedPollsCoalesceIntoTheRequestAlreadyRunning();
    void differentDevicesDoNotCoalesceIntoEachOther();
    void statusParsesTheRecovery();
    void uploadIsSignedOverAFreshNonce();
    void anImageWithoutTheMagicIsRefusedBeforeSending();
    void anImageLargerThanThePartitionIsRefused();
    void rebootIsSignedWithZeroDigest();
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
    QVERIFY(!spy.at(0).at(3).toString().isEmpty());  // errorMessage

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

void TestOtaClient::statusParsesTheRecovery() {
    FakeRecovery recovery;
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::statusChecked);
    client.checkStatus(QStringLiteral("dev-1"), recovery.address());
    QVERIFY(spy.wait(kStatusSettleMs));
    QCOMPARE(spy.at(0).at(1).toBool(), true);
    const auto status = spy.at(0).at(2).value<RecoveryStatus>();
    QCOMPARE(status.name, QStringLiteral("bancada"));
    QCOMPARE(status.reason, QStringLiteral("command"));
    QCOMPARE(status.appProject, QStringLiteral("sensor"));
    QCOMPARE(status.appVersion, QStringLiteral("1.2.0"));
    QVERIFY(status.authRequired);
    QCOMPARE(status.nonce, recovery.nonce);
}

void TestOtaClient::uploadIsSignedOverAFreshNonce() {
    FakeRecovery recovery;
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::uploadFinished);
    QTemporaryFile file;
    const QByteArray image = QByteArray(1, char(0xE9)) + QByteArray(4000, 'x');
    client.uploadFirmware(QStringLiteral("dev-1"), recovery.address(), testKey(),
                          writeImage(file, image));
    QVERIFY(spy.wait(kStatusSettleMs));
    QCOMPARE(spy.at(0).at(1).toBool(), true);
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("OK. Rebooting into sensor 1.3.0."));

    // A GET /status for the nonce, then the POST.
    QCOMPARE(recovery.requests.size(), 2);
    QCOMPARE(recovery.requests[0].path, QByteArray("/status"));
    const FakeRecovery::Request& post = recovery.requests[1];
    QCOMPARE(post.method, QByteArray("POST"));
    QCOMPARE(post.path, QByteArray("/update"));
    QCOMPARE(post.body, image);
    const QByteArray digest = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
    QCOMPARE(post.headers.value("x-ota-sha256"), digest.toHex());
    QCOMPARE(post.headers.value("x-ota-auth"),
             traceview::ota::authMac(testKey(), recovery.nonce, 'U', digest));
}

void TestOtaClient::anImageWithoutTheMagicIsRefusedBeforeSending() {
    FakeRecovery recovery;
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::uploadFinished);
    QTemporaryFile file;
    client.uploadFirmware(QStringLiteral("dev-1"), recovery.address(), testKey(),
                          writeImage(file, QByteArray(100, 'x')));
    QCOMPARE(spy.count(), 1);  // synchronous: nothing was sent
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QVERIFY(recovery.requests.isEmpty());
}

void TestOtaClient::anImageLargerThanThePartitionIsRefused() {
    FakeRecovery recovery;
    recovery.maxSize = 1000;
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::uploadFinished);
    QTemporaryFile file;
    client.uploadFirmware(QStringLiteral("dev-1"), recovery.address(), testKey(),
                          writeImage(file, QByteArray(1, char(0xE9)) + QByteArray(2000, 'x')));
    QVERIFY(spy.wait(kStatusSettleMs));
    QCOMPARE(spy.at(0).at(1).toBool(), false);
    QCOMPARE(recovery.requests.size(), 1);  // the status only, no POST
}

void TestOtaClient::rebootIsSignedWithZeroDigest() {
    FakeRecovery recovery;
    OtaClient client;
    QSignalSpy spy(&client, &OtaClient::rebootFinished);
    client.rebootToApp(QStringLiteral("dev-1"), recovery.address(), testKey());
    QVERIFY(spy.wait(kStatusSettleMs));
    QCOMPARE(spy.at(0).at(1).toBool(), true);
    QCOMPARE(recovery.requests.size(), 2);
    QCOMPARE(recovery.requests[1].path, QByteArray("/reboot"));
    QCOMPARE(recovery.requests[1].headers.value("x-ota-auth"),
             traceview::ota::authMac(testKey(), recovery.nonce, 'B', QByteArray(32, '\0')));
}

QTEST_MAIN(TestOtaClient)
#include "test_otaclient.moc"
