#include <QtTest/QtTest>
#include <btp/codec.hpp>

#include "protocol/channelseal.h"
#include "protocol/keyderivation.h"

using namespace traceview;

// The exact round trip the BTP Traffic Monitor's Decrypt box performs: derive
// a channel-B key from a typed password, rebuild the logical header from a
// captured frame, and open the sealed payload -- succeeding with the right
// password and failing closed with the wrong one. ChannelSeal is also what
// BtpBackend::onSessionFrameReceived() uses in production, so this pins the
// monitor to the same behaviour.

namespace {

// The golden password from test_keyderivation.cpp -- it protects nothing.
const QString kPassword = QStringLiteral("senha-e-de-teste");

btp::Header capturedHeader() {
    btp::Header header{};
    header.type = btp::MessageType::Control;
    header.flags = 0;
    header.source_id = 0x11223344;
    header.boot_id = 0xA1B2C3D4;
    header.sequence = 7;
    header.timestamp_us = 0x0102030405060708ULL;
    header.object_id = 0x0004;
    header.fragment_index = 0;
    header.fragment_count = 1;
    return header;
}

class TestChannelOpen : public QObject {
    Q_OBJECT

private slots:
    void rightPasswordOpensTheFrame();
    void wrongPasswordFailsClosed();
    void unsealedHeaderIsRejected();
};

void TestChannelOpen::rightPasswordOpensTheFrame() {
    const QByteArray key = deriveChannelKey(kPassword);
    QVERIFY(!key.isEmpty());

    btp::Header header = capturedHeader();
    const QByteArray plaintext = QByteArrayLiteral("dongle -status\n");
    const QByteArray sealed = ChannelSeal::seal(key, header, plaintext);
    QVERIFY(!sealed.isEmpty());
    QVERIFY(header.flags & btp::kFlagEncrypted);  // seal() set it on the header

    const std::optional<QByteArray> opened = ChannelSeal::open(key, header, sealed);
    QVERIFY(opened.has_value());
    QCOMPARE(*opened, plaintext);
}

void TestChannelOpen::wrongPasswordFailsClosed() {
    const QByteArray key = deriveChannelKey(kPassword);
    btp::Header header = capturedHeader();
    const QByteArray sealed = ChannelSeal::seal(key, header, QByteArrayLiteral("secret"));

    const QByteArray wrongKey = deriveChannelKey(QStringLiteral("not-the-password"));
    QVERIFY(!ChannelSeal::open(wrongKey, header, sealed).has_value());
}

void TestChannelOpen::unsealedHeaderIsRejected() {
    const QByteArray key = deriveChannelKey(kPassword);
    btp::Header header = capturedHeader();
    const QByteArray sealed = ChannelSeal::seal(key, header, QByteArrayLiteral("x"));

    btp::Header cleartextHeader = header;
    cleartextHeader.flags = 0;  // ENCRYPTED not set -> never read bytes as plaintext
    QVERIFY(!ChannelSeal::open(key, cleartextHeader, sealed).has_value());
}

}  // namespace

QTEST_MAIN(TestChannelOpen)
#include "test_channelopen.moc"
