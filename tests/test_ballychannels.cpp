#include <bally_channels.h>

#include <QCryptographicHash>
#include <QFile>
#include <QtTest/QtTest>

// bally_channels.h is the single table that answers "whose message is this,
// and which key opens it". It exists in THREE byte-identical copies -- one in
// bally_OS, one in bally_dongle, one here -- because it is product convention
// rather than protocol: BTP has no key-id field on the wire at all, so "this
// source_id uses the link key" is true only because the three ends agree on
// it.
//
// Three copies with no enforcement would drift, and drifting is silent: the
// first device added to the fleet after a divergence simply stops working,
// with nothing to point at the cause. So each repository hashes its own copy
// against a constant committed alongside it. Edit the file in one repository
// and the other two fail to build until someone copies it across and updates
// the hash -- which is the whole mechanism.
//
// The rest of the file is contract assertions, and every one of them is
// constexpr on purpose: a wrong table does not fail at run time here, it fails
// to compile.

namespace {

// SHA-256 of bally_channels.h with line endings normalized to LF.
//
// Normalized because git on Windows checks the file out with CRLF while the
// repositories store LF, so hashing the bytes as they sit on disk would make
// this pass or fail depending on which machine ran it -- a false alarm that
// would teach people to ignore the check, which is worse than not having it.
const char kExpectedSha256[] = "c141fbb0e14f1f0c6573d0406b736d4419d3c1879a17044f86ce83fc6a804560";

}  // namespace

using namespace bally;

// ---------------------------------------------------------------------------
// The channel table, checked at compile time
// ---------------------------------------------------------------------------

static_assert(key_of_channel(Channel::A_Console) == KeyKind::None,
              "channel A is the console link and is deliberately in the clear");
static_assert(key_of_channel(Channel::B_Endpoint) == KeyKind::Endpoint,
              "channel B is protected by the per-robot endpoint key");
static_assert(key_of_channel(Channel::C_Link) == KeyKind::Link,
              "channel C is protected by the per-fleet link key");

// The console sees the hub across A and a robot across B.
static_assert(channel_of_peer(Vantage::Console, 0xD0u, 0xD0u) == Channel::A_Console, "");
static_assert(channel_of_peer(Vantage::Console, 0xA1u, 0xD0u) == Channel::B_Endpoint, "");
// The robot sees the hub across C and the console across B. Same predicate,
// different answer -- which is exactly why the vantage is a parameter.
static_assert(channel_of_peer(Vantage::Robot, 0xD0u, 0xD0u) == Channel::C_Link, "");
static_assert(channel_of_peer(Vantage::Robot, 0xC5u, 0xD0u) == Channel::B_Endpoint, "");

// The hub cannot answer from source_id alone: a frame off the radio is C if it
// consumes it and B merely passing through if it does not, and both carry the
// same robot as source.
static_assert(dongle_channel_of(Side::Cable, false) == Channel::A_Console, "");
static_assert(dongle_channel_of(Side::Radio, true) == Channel::C_Link, "");
static_assert(dongle_channel_of(Side::Radio, false) == Channel::B_Endpoint, "");

// The header only chooses which frames need reassembly plus an L-key open.
// The final ownership checks below are valid only on authenticated plaintext.
static_assert(dongle_may_consume(btp::MessageType::Control, 0x0009u), "");
static_assert(dongle_may_consume(btp::MessageType::Command, 0x0001u), "");
static_assert(dongle_may_consume(btp::MessageType::Command, 0x0002u), "");
static_assert(dongle_may_consume(btp::MessageType::Control, 0x0004u), "");
static_assert(!dongle_may_consume(btp::MessageType::Telemetry, 0x0001u), "");
static_assert(!dongle_may_consume(btp::MessageType::Control, 0x0006u), "");

// The ingress ownership rule after authentication.
static_assert(dongle_consumes(btp::MessageType::Control, 0x0009u, 0u, 0xD0u),
              "the heartbeat is link administration and stops at the hub");
static_assert(dongle_consumes(btp::MessageType::Command, 0x0002u, 0xD0u, 0xD0u),
              "the answer to a command the hub itself issued stops at the hub");
static_assert(dongle_consumes(btp::MessageType::Command, 0x0001u, 0xD0u, 0xD0u),
              "a command addressed to the hub is the hub's to run, not to relay");
static_assert(!dongle_consumes(btp::MessageType::Command, 0x0001u, 0xA1u, 0xD0u),
              "a command addressed to a robot is relayed down, not executed here");
static_assert(!dongle_consumes(btp::MessageType::Command, 0x0002u, 0xA1u, 0xD0u),
              "a command result for somebody else is relayed, not swallowed");
static_assert(!dongle_consumes(btp::MessageType::Telemetry, 0x0001u, 0u, 0xD0u), "");
static_assert(!dongle_consumes(btp::MessageType::Log, 0x0001u, 0u, 0xD0u), "");
static_assert(!dongle_consumes(btp::MessageType::Terminal, 0x0002u, 0u, 0xD0u),
              "terminal output must reach the console -- it used to be dropped in silence");
static_assert(!dongle_consumes(btp::MessageType::Control, 0x0004u, 0u, 0xD0u),
              "a manifest pushed with no correlation to a hub request is relayed");
static_assert(dongle_consumes(btp::MessageType::Control, 0x0004u, 0xD0u, 0xD0u),
              "a manifest answering a request the hub itself issued is the hub's business, "
              "same idea as COMMAND_RESULT");
static_assert(!dongle_consumes(btp::MessageType::Control, 0x0004u, 0xA1u, 0xD0u),
              "a robot's manifest correlated to somebody else's request is still relayed");

class TestBallyChannels : public QObject {
    Q_OBJECT

private slots:
    void thisCopyMatchesTheCommittedHash();
    void relayingIsTheDefaultForAnythingUnrecognized();
};

void TestBallyChannels::thisCopyMatchesTheCommittedHash() {
    QFile file(QStringLiteral(BALLY_CHANNELS_HEADER_PATH));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    QByteArray contents = file.readAll();
    contents.replace("\r\n", "\n");

    const QByteArray actual =
        QCryptographicHash::hash(contents, QCryptographicHash::Sha256).toHex();
    QVERIFY2(actual == QByteArray(kExpectedSha256),
             qPrintable(
                 QStringLiteral("bally_channels.h does not match the committed hash.\n"
                                "  expected %1\n  actual   %2\n"
                                "If you changed it on purpose, copy it verbatim into bally_OS and "
                                "bally_dongle and update the expected hash in all three tests. "
                                "There is no correct partial edit of this file.")
                     .arg(QString::fromLatin1(kExpectedSha256), QString::fromLatin1(actual))));
}

// The property that makes the ingress rule safe to extend: a message type or
// object_id nobody thought about is RELAYED, so it shows up at the console --
// visible and harmless -- rather than being swallowed by the hub. Forgetting
// to add something to the consume list is a nuisance; forgetting to add it to
// a relay list would be a message that vanishes.
void TestBallyChannels::relayingIsTheDefaultForAnythingUnrecognized() {
    const quint32 self = 0xD0u;
    for (quint16 objectId = 1; objectId < 0x40; ++objectId) {
        const bool consumed = dongle_consumes(btp::MessageType::Control, objectId, 0, self);
        if (objectId == 0x0009u) {
            QVERIFY2(consumed, "the heartbeat is the one CONTROL the hub keeps");
        } else {
            QVERIFY2(!consumed, qPrintable(QStringLiteral("CONTROL 0x%1 should be relayed")
                                               .arg(objectId, 4, 16, QLatin1Char('0'))));
        }
    }
    // No TELEMETRY, LOG or TERMINAL object_id is ever consumed: those channels
    // belong end to end to the console and the robot, and the hub holds no key
    // for them.
    for (quint16 objectId = 1; objectId < 0x40; ++objectId) {
        QVERIFY(!dongle_consumes(btp::MessageType::Telemetry, objectId, 0, self));
        QVERIFY(!dongle_consumes(btp::MessageType::Log, objectId, 0, self));
        QVERIFY(!dongle_consumes(btp::MessageType::Terminal, objectId, 0, self));
    }
}

QTEST_MAIN(TestBallyChannels)
#include "test_ballychannels.moc"
