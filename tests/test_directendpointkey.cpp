#include <QSignalSpy>
#include <QtTest/QtTest>
#include <btp/codec.hpp>
#include <cstdint>
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
// effects (identity rewrite, outbound sealing, the "drop an unsealed frame"
// downgrade check) -- see its own comment in btpbackend.h for the full
// reasoning, including why the robot's receive side does NOT require
// TraceView's own outbound traffic to be sealed (confirmed by reading
// BTP/src/node.cpp's route_decoded()/finish(): open() only runs when the
// INCOMING frame already carries kFlagEncrypted).
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

// Confirmed by reading BallyRobot.cpp / BTP/src/node.cpp (Phase 1 of this
// task): bally_OS does not require inbound encryption for the message types
// it accepts over TCP, and TraceView's own downgrade protection
// (onSessionFrameReceived()'s "dropped an unsealed frame on a sealed hub
// channel" branch) is gated on m_peerSourceId != 0. A direct session never
// sets it, so an unsealed frame must still be delivered even with a key
// configured -- unlike a keyed hub child (contrast
// test_hubendpoint.cpp, which has no equivalent "still delivered" case for
// its own keyed children on that path).
void TestDirectEndpointKey::unsealedFrameStillReachesAConsoleRoleSessionEvenWithAKeyConfigured() {
    BtpBackend backend(BtpSession::Framing::PreFramed, btp::kEspNowTransport);
    backend.setDirectEndpointKey(deriveChannelKey(QStringLiteral("senha-do-robo")));

    QSignalSpy terminalSpy(&backend, &Backend::terminalDataReceived);

    btp::Header header = terminalOutHeader();  // flags = 0, never sealed
    backend.feedBytes(encodedFrame(header, QByteArrayLiteral("plain text reply\n")));

    QCOMPARE(terminalSpy.count(), 1);
}

QTEST_MAIN(TestDirectEndpointKey)
#include "test_directendpointkey.moc"
