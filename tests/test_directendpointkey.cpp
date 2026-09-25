#include <QSignalSpy>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <cstdint>
#include <optional>
#include <vector>

#include "protocol/btpbackend.h"
#include "protocol/channelseal.h"
#include "protocol/keyderivation.h"

using namespace traceview;

// TAREFAS_TCP_BLE_ANDROID.txt T21-T24: bally_OS seals almost everything it
// sends back over a direct TCP session with channel-B key E --
// RobotTcpLink::seal()/reply_seal() (MANIFEST_DATA/SUBSCRIBE_RESULT/
// UNSUBSCRIBE_RESULT), CommandProcessor::send_result(..., protocol_tcp_)
// (COMMAND_RESULT) and TelemetryPublisher::bind_tcp_target()/
// TerminalResponder::bind_tcp_target() (TELEMETRY/TERMINAL_OUT) -- all
// confirmed by reading utils/BallyRobot/BallyRobot.cpp (bally_OS) before
// writing this. The one exception is HELLO/HELLO_RESULT, always cleartext by
// design (BTP/src/node.cpp's route_decoded() sends the session reply with an
// explicit seal=nullptr -- "the handshake bootstraps the session before any
// key").
//
// BtpBackend::setDirectEndpointKey() is what lets a direct session open
// those sealed replies WITHOUT adopting setHubEndpoint()'s hub-child side
// effects (identity rewrite, hub-cache exemptions) -- see its own comment in
// btpbackend.h. Since BTP 2.46.0 a keyed robot also DROPS any unsealed
// message past the handshake, so a keyed direct session (SessionStartMode::
// DirectBtp) seals everything it sends and, symmetrically, drops any unsealed
// frame except HELLO_RESULT / SESSION_CLOSE_RESULT.
//
// This exercises setDirectEndpointKey() directly, the same way
// test_hubendpoint.cpp exercises setHubEndpoint() -- using TERMINAL_OUT as
// the stand-in sealed reply since it is one of the two message types the
// firmware notes explicitly confirm are sealed on TCP.

namespace {

const quint32 kRobot = 0x0A0A0A0Au;
const quint32 kRobotBoot = 0x00C0FFEEu;
constexpr quint16 kTerminalOutObjectId = 0x0002;
// CONTROL/HELLO (commands.md section 1.5) -- spelled as a literal, same
// convention test_hubendpoint.cpp already uses for object ids, to avoid an
// extra include just for the symbol.
constexpr quint16 kControlHello = 0x0001;
constexpr quint16 kControlManifestRequest = 0x0003;

btp::Header terminalOutHeader() {
    btp::Header header{};
    header.type = btp::MessageType::Terminal;
    header.flags = 0;
    header.source_id = kRobot;
    header.boot_id = kRobotBoot;
    header.sequence = 1;
    header.timestamp_us = 0;
    header.object_id = kTerminalOutObjectId;
    header.fragment_index = 0;
    header.fragment_count = 1;
    return header;
}

// Encodes one pre-framed (ESP-NOW profile, no COBS -- matches the
// BtpBackend(PreFramed, kEspNowTransport) constructor every test below
// uses) frame carrying `payload` under `header` exactly as given -- the
// caller decides whether that header already carries kFlagEncrypted. Same
// shape as test_hubendpoint.cpp's controlFrame(), generalized to any
// MessageType/object_id.
QByteArray encodedFrame(const btp::Header& header, const QByteArray& payload) {
    const btp::Frame frame{header,
                           {reinterpret_cast<const std::uint8_t*>(payload.constData()),
                            std::size_t(payload.size())}};
    std::vector<std::uint8_t> out(btp::kEspNowMaxFrameSize);
    std::size_t n = 0;
    if (btp::encode(frame, btp::kEspNowTransport, out.data(), out.size(), &n) != btp::Error::Ok) {
        return QByteArray();
    }
    return QByteArray(reinterpret_cast<const char*>(out.data()), int(n));
}

}  // namespace

class TestDirectEndpointKey : public QObject {
    Q_OBJECT

private slots:
    void sealedTerminalOutIsOpenedOnceTheKeyIsConfigured();
    void sealedTerminalOutIsDroppedWithNoKeyConfigured();
    void wrongPasswordFailsClosed();
    void doesNotAdoptHubChildRoleOrIdentity();
    void unsealedFrameStillReachesAConsoleRoleSessionEvenWithAKeyConfigured();
    void keyedDirectSessionDropsAnUnsealedFrame();
    void keyedDirectSessionSealsTerminalIn();
    void unkeyedDirectSessionSendsTerminalInInTheClear();
};

// The core contract: once setDirectEndpointKey() has the right key, a
// TERMINAL_OUT frame the robot sealed with channel-B key E is opened and
// delivered as plaintext, exactly as a hub child already does for its own
// sealed traffic -- but reached through the new, narrower method.
void TestDirectEndpointKey::sealedTerminalOutIsOpenedOnceTheKeyIsConfigured() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    const QByteArray key = deriveChannelKey(QStringLiteral("senha-do-robo"));
    QVERIFY(!key.isEmpty());
    backend.setDirectEndpointKey(key);
    QCOMPARE(backend.peerSourceId(), 0u);  // still console-role, not a hub child

    QSignalSpy terminalSpy(&backend, &Backend::terminalDataReceived);

    btp::Header header = terminalOutHeader();
    const QByteArray plaintext = QByteArrayLiteral("robot booted\n");
    const QByteArray sealed = ChannelSeal::seal(key, header, plaintext);
    QVERIFY(!sealed.isEmpty());
    QVERIFY(header.flags & btp::kFlagEncrypted);  // seal() set it on the header

    backend.feedBytes(encodedFrame(header, sealed));

    QCOMPARE(terminalSpy.count(), 1);
    QCOMPARE(terminalSpy.at(0).at(0).toByteArray(), plaintext);
}

// Fail closed: no key configured at all (the "not configured yet" state
// connectToTcp()/setDirectEndpointKey() document, same convention as an
// unkeyed hub child) must never surface unauthenticated bytes.
void TestDirectEndpointKey::sealedTerminalOutIsDroppedWithNoKeyConfigured() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    // setDirectEndpointKey() never called.

    QSignalSpy terminalSpy(&backend, &Backend::terminalDataReceived);

    btp::Header header = terminalOutHeader();
    const QByteArray key = deriveChannelKey(QStringLiteral("senha-do-robo"));
    const QByteArray sealed = ChannelSeal::seal(key, header, QByteArrayLiteral("robot booted\n"));
    QVERIFY(!sealed.isEmpty());

    backend.feedBytes(encodedFrame(header, sealed));

    QCOMPARE(terminalSpy.count(), 0);
}

// A key IS configured, but it is the wrong one (typo'd password) -- the AEAD
// tag must never verify, same fail-closed contract ChannelSeal::open()
// itself guarantees (test_channelopen.cpp), exercised here through the
// backend's own receive path instead of calling ChannelSeal directly.
void TestDirectEndpointKey::wrongPasswordFailsClosed() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setDirectEndpointKey(deriveChannelKey(QStringLiteral("chave-errada")));

    QSignalSpy terminalSpy(&backend, &Backend::terminalDataReceived);

    btp::Header header = terminalOutHeader();
    const QByteArray rightKey = deriveChannelKey(QStringLiteral("senha-do-robo"));
    const QByteArray sealed = ChannelSeal::seal(rightKey, header, QByteArrayLiteral("robot booted\n"));
    QVERIFY(!sealed.isEmpty());

    backend.feedBytes(encodedFrame(header, sealed));

    QCOMPARE(terminalSpy.count(), 0);
}

// The regression that matters most, mirroring test_hubendpoint.cpp's own
// "an ordinary serial backend still handshakes": setDirectEndpointKey() must
// not turn this into a hub child. peerSourceId() stays 0, and -- built the
// same way DeviceConnection actually builds a TCP backend
// (SessionStartMode::DirectBtp) -- connecting still sends this backend's own
// HELLO, not a hub-child-style manifest request addressed to some peer.
void TestDirectEndpointKey::doesNotAdoptHubChildRoleOrIdentity() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport,
                       BtpBackend::SessionStartMode::DirectBtp);
    backend.setDirectEndpointKey(deriveChannelKey(QStringLiteral("senha-do-robo")));
    QCOMPARE(backend.peerSourceId(), 0u);

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.onTransportConnectionChanged(true);

    QVERIFY2(written.count() >= 1,
             "a direct session must still send its own HELLO once connected");

    const QByteArray firstFrame = written.at(0).at(0).toByteArray();
    std::vector<std::uint8_t> storage(firstFrame.constBegin(), firstFrame.constEnd());
    btp::DecodedFrame decoded{};
    QVERIFY(btp::decode(storage.data(), storage.size(), btp::kEspNowTransport, &decoded) ==
           btp::Error::Ok);
    QCOMPARE(int(decoded.header.type), int(btp::MessageType::Control));
    QCOMPARE(decoded.header.object_id, kControlHello);
    QVERIFY2(decoded.header.object_id != kControlManifestRequest,
             "a direct session must HELLO, not ask a hub-child-style manifest request");
}

// The unsealed-frame drop is specific to a DIRECT session (DirectBtp) and a
// keyed hub child. A console-role backend (the dongle's own serial session,
// SessionStartMode::Console) keeps delivering the dongle's cleartext traffic
// even with a key configured -- contrast keyedDirectSessionDropsAnUnsealedFrame
// below.
void TestDirectEndpointKey::unsealedFrameStillReachesAConsoleRoleSessionEvenWithAKeyConfigured() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setDirectEndpointKey(deriveChannelKey(QStringLiteral("senha-do-robo")));

    QSignalSpy terminalSpy(&backend, &Backend::terminalDataReceived);

    btp::Header header = terminalOutHeader();  // flags = 0, never sealed
    backend.feedBytes(encodedFrame(header, QByteArrayLiteral("plain text reply\n")));

    QCOMPARE(terminalSpy.count(), 1);
}

// A keyed direct session: the robot seals everything past the handshake, so
// an unsealed TERMINAL_OUT is a downgrade or a spoof and must not surface.
void TestDirectEndpointKey::keyedDirectSessionDropsAnUnsealedFrame() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport,
                       BtpBackend::SessionStartMode::DirectBtp);
    const QByteArray key = deriveChannelKey(QStringLiteral("senha-do-robo"));
    backend.setDirectEndpointKey(key);

    QSignalSpy terminalSpy(&backend, &Backend::terminalDataReceived);

    btp::Header plainHeader = terminalOutHeader();  // flags = 0, never sealed
    backend.feedBytes(encodedFrame(plainHeader, QByteArrayLiteral("spoofed\n")));
    QCOMPARE(terminalSpy.count(), 0);

    // The same session still opens a properly sealed one.
    btp::Header sealedHeader = terminalOutHeader();
    sealedHeader.sequence = 2;
    const QByteArray sealed = ChannelSeal::seal(key, sealedHeader, QByteArrayLiteral("real\n"));
    QVERIFY(!sealed.isEmpty());
    backend.feedBytes(encodedFrame(sealedHeader, sealed));
    QCOMPARE(terminalSpy.count(), 1);
    QCOMPARE(terminalSpy.at(0).at(0).toByteArray(), QByteArrayLiteral("real\n"));
}

namespace {

// Decodes the one pre-framed frame a bytesToWrite emission carried.
bool decodeWrittenFrame(const QByteArray& written, std::vector<std::uint8_t>* storage,
                        btp::DecodedFrame* out) {
    storage->assign(written.constBegin(), written.constEnd());
    return btp::decode(storage->data(), storage->size(), btp::kEspNowTransport, out) ==
           btp::Error::Ok;
}

}  // namespace

// A keyed robot drops unsealed input (BTP 2.46.0), so a keyed direct session
// seals TERMINAL_IN with the channel-B key -- the robot's RadioSeal::open_e
// opens it back to the typed bytes.
void TestDirectEndpointKey::keyedDirectSessionSealsTerminalIn() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport,
                       BtpBackend::SessionStartMode::DirectBtp);
    const QByteArray key = deriveChannelKey(QStringLiteral("senha-do-robo"));
    backend.setDirectEndpointKey(key);

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.sendTerminalIn(QByteArrayLiteral("ls\r"));
    QCOMPARE(written.count(), 1);

    std::vector<std::uint8_t> storage;
    btp::DecodedFrame decoded{};
    QVERIFY(decodeWrittenFrame(written.at(0).at(0).toByteArray(), &storage, &decoded));
    QCOMPARE(int(decoded.header.type), int(btp::MessageType::Terminal));
    QVERIFY((decoded.header.flags & btp::kFlagEncrypted) != 0U);
    const std::optional<QByteArray> plain = ChannelSeal::open(
        key, decoded.header,
        QByteArray(reinterpret_cast<const char*>(decoded.payload.data),
                   int(decoded.payload.size)));
    QVERIFY(plain.has_value());
    QCOMPARE(*plain, QByteArrayLiteral("ls\r"));
}

// No key configured: nothing to seal with, so TERMINAL_IN stays cleartext --
// which only an unkeyed robot/simulator accepts.
void TestDirectEndpointKey::unkeyedDirectSessionSendsTerminalInInTheClear() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport,
                       BtpBackend::SessionStartMode::DirectBtp);

    QSignalSpy written(&backend, &Backend::bytesToWrite);
    backend.sendTerminalIn(QByteArrayLiteral("ls\r"));
    QCOMPARE(written.count(), 1);

    std::vector<std::uint8_t> storage;
    btp::DecodedFrame decoded{};
    QVERIFY(decodeWrittenFrame(written.at(0).at(0).toByteArray(), &storage, &decoded));
    QVERIFY((decoded.header.flags & btp::kFlagEncrypted) == 0U);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>(decoded.payload.data),
                        int(decoded.payload.size)),
             QByteArrayLiteral("ls\r"));
}

QTEST_MAIN(TestDirectEndpointKey)
#include "test_directendpointkey.moc"
