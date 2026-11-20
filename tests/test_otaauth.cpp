#include <QCryptographicHash>
#include <QtTest>

#include "ota/otaauth.h"

using namespace traceview;

// The golden vectors of TraceKit's test/host/test_ota_auth.cpp and
// tools/test_btp_ota.py: the recovery, its Python tool, its web page and
// TraceView must all sign the same way, or an upload is refused.
class TestOtaAuth : public QObject {
    Q_OBJECT

private:
    static QByteArray key() {
        QByteArray k;
        for (int i = 0; i < 16; ++i) k.append(static_cast<char>(i));
        return k;
    }
    static QByteArray nonce() {
        QByteArray n;
        for (int i = 0xA0; i < 0xB0; ++i) n.append(static_cast<char>(i));
        return n;
    }

private slots:
    void updateMatchesTheFirmwareVector() {
        const QByteArray image = QByteArray("firmware image bytes").repeated(100);
        const QByteArray digest = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
        QCOMPARE(digest.toHex(),
                 QByteArray("3f5a0b3dfe39f2714a5b2c004faa882dd9a768296bb1bca2d47435894341d11f"));
        QCOMPARE(ota::authMac(key(), nonce(), ota::kActionUpdate, digest),
                 QByteArray("6ef143d06ec211459855a0c6588c7ef795799e78d6d002265e82dbf38c89fc8e"));
    }

    void rebootMatchesTheFirmwareVector() {
        QCOMPARE(ota::authMac(key(), nonce(), ota::kActionReboot, QByteArray(32, '\0')),
                 QByteArray("800ea9f1afc17066dbd1d5d53cafe0353f0221b71887ed2968575926adeedf21"));
    }

    void badInputsGiveNoSignature() {
        const QByteArray digest(32, '\0');
        QVERIFY(ota::authMac(key(), nonce().left(8), ota::kActionUpdate, digest).isEmpty());
        QVERIFY(ota::authMac(key(), nonce(), 'X', digest).isEmpty());
        QVERIFY(ota::authMac(QByteArray(), nonce(), ota::kActionUpdate, digest).isEmpty());
        QVERIFY(ota::authMac(key(), nonce(), ota::kActionUpdate, digest.left(16)).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestOtaAuth)
#include "test_otaauth.moc"
