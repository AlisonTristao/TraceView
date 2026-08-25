#include <QtTest/QtTest>

#include "protocol/keyderivation.h"

using namespace traceview;

// The acceptance criterion of the key-provisioning topico, and the reason it
// is written as a test rather than checked by hand: three separate
// implementations of one derivation contract have to produce identical
// octets, and the failure mode of a near-miss is invisible from here.
//
// A password that derives ALMOST the right key does not fail here. It fails
// two repositories away, at a robot, as a tag that will not verify -- which
// looks exactly like a wrong password, a wrong robot, or a radio problem. So
// the vectors below are not a restatement of the arithmetic in
// keyderivation.cpp; they are octets that bally_OS/scripts/provision_key.py
// actually wrote, pasted in. If someone changes the salt, the iteration count
// or the KDF on either side, this fails immediately and locally.
//
// Regenerate with, from a bally_OS checkout:
//   python scripts/provision_key.py --force \
//       --password-e senha-e-de-teste --password-l senha-l-de-teste bally.key

namespace {

// The two passwords the golden vector was generated from. Test values, never
// anything real -- and the reason they can be written down here at all is that
// they protect nothing.
const QString kPasswordE = QStringLiteral("senha-e-de-teste");
const QString kPasswordL = QStringLiteral("senha-l-de-teste");

const char kExpectedKeyE[] = "dc13a1798601e424c45f691dd5484eed";
const char kExpectedKeyL[] = "e28b8edfd919d39f5b573c92b77224d7";
const char kExpectedVerifyE[] = "c7a45e15a41b780f";
const char kExpectedVerifyL[] = "2a5e4ddc866f8a08";

QString hex(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toHex());
}

}  // namespace

class TestKeyDerivation : public QObject {
    Q_OBJECT

private slots:
    void matchesTheProvisionerByteForByte();
    void verifyTagsMatchTheProvisionerAndDoNotCrossChannels();
    void theTwoPasswordsProduceUnrelatedKeys();
    void anEmptyPasswordDerivesNothing();
    void derivationIsDeterministic();
};

// The whole point of the file: what this application derives is what the
// provisioner wrote onto the robot's card.
void TestKeyDerivation::matchesTheProvisionerByteForByte() {
    QCOMPARE(hex(deriveChannelKey(kPasswordE)), QString::fromLatin1(kExpectedKeyE));
    QCOMPARE(hex(deriveChannelKey(kPasswordL)), QString::fromLatin1(kExpectedKeyL));
    // 16 octets, because the cipher is AES-128-GCM. A key of the wrong LENGTH
    // would be caught by the cipher; a key of the right length and wrong
    // content would not, which is why the comparison above is the real check
    // and this is only a guard on the obvious.
    QCOMPARE(deriveChannelKey(kPasswordE).size(), 16);
}

// The verify tags are what make a wrong password fail early and by NAME. If
// they were computed differently here than by the provisioner, a robot would
// reject a perfectly good card, or accept a bad one and fail later.
void TestKeyDerivation::verifyTagsMatchTheProvisionerAndDoNotCrossChannels() {
    const QByteArray keyE = deriveChannelKey(kPasswordE);
    const QByteArray keyL = deriveChannelKey(kPasswordL);

    QCOMPARE(hex(endpointKeyVerifyTag(keyE)), QString::fromLatin1(kExpectedVerifyE));
    QCOMPARE(hex(linkKeyVerifyTag(keyL)), QString::fromLatin1(kExpectedVerifyL));

    // The two labels are what keep the tags from being interchangeable. If
    // both channels used one label, a link key would satisfy an endpoint
    // key's check and the early failure would stop being early.
    QVERIFY(endpointKeyVerifyTag(keyE) != linkKeyVerifyTag(keyE));
    QVERIFY(endpointKeyVerifyTag(keyL) != linkKeyVerifyTag(keyL));

    QCOMPARE(endpointKeyVerifyTag(keyE).size(), 8);
}

// Independent passwords must give unrelated keys. This is the property the
// three-channel design rests on: whoever holds the fleet's link key still
// cannot read any robot's telemetry, because that needs a different key that a
// different password produced.
void TestKeyDerivation::theTwoPasswordsProduceUnrelatedKeys() {
    const QByteArray keyE = deriveChannelKey(kPasswordE);
    const QByteArray keyL = deriveChannelKey(kPasswordL);
    QVERIFY(!keyE.isEmpty());
    QVERIFY(keyE != keyL);

    // And a one-character difference is a completely different key, not a
    // nearby one -- the property that makes guessing from a near-miss useless.
    const QByteArray nearMiss = deriveChannelKey(QStringLiteral("senha-e-de-testf"));
    QVERIFY(nearMiss != keyE);
    int sharedLeadingOctets = 0;
    for (int i = 0; i < keyE.size() && i < nearMiss.size() && keyE.at(i) == nearMiss.at(i); ++i) {
        ++sharedLeadingOctets;
    }
    QVERIFY(sharedLeadingOctets < 4);
}

// An empty password is a configuration mistake. Deriving a usable key from it
// would work, which is the wrong kind of forgiving: it would let a device
// connect with no secret at all and look configured.
void TestKeyDerivation::anEmptyPasswordDerivesNothing() {
    QVERIFY(deriveChannelKey(QString()).isEmpty());
    QVERIFY(deriveChannelKey(QStringLiteral("")).isEmpty());
    QVERIFY(endpointKeyVerifyTag(QByteArray()).isEmpty());
}

// Derived once per connection and reused; two calls must not disagree.
void TestKeyDerivation::derivationIsDeterministic() {
    QCOMPARE(deriveChannelKey(kPasswordE), deriveChannelKey(kPasswordE));
}

QTEST_MAIN(TestKeyDerivation)
#include "test_keyderivation.moc"
