#include <QSignalSpy>
#include <QtTest>
#include <btp/codec.hpp>
#include <btp/messages.hpp>
#include <cstring>
#include <vector>

#include "backend/backend.h"
#include "protocol/btpbackend.h"
#include "protocol/logseverity.h"

using namespace traceview;

// A robot's own serial port (setSerialRobotPeer(true)) is a cleartext direct
// session: control widgets reach it with COMMAND_REQUEST, and its result comes
// back into the device's log panel. Until 4.12.0 btp::Node had 16 bytes of scratch
// to encode the request in -- smaller than the request's own 20-byte prefix --
// so no command ever left, on any link.

namespace {

constexpr quint16 kControlHello = 0x0001;
constexpr quint16 kControlHelloResult = 0x0002;
constexpr quint32 kRobot = 0xD00D0001;
constexpr quint32 kRobotBoot = 0xB0070001;

void appendLe32(QByteArray& out, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(char((value >> shift) & 0xFF));
    }
}

QByteArray buildSerialPacket(const btp::Frame& frame) {
    std::vector<std::uint8_t> encoded(btp::kSerialMaxFrameSize);
    std::size_t encodedSize = 0;
    if (btp::encode(frame, btp::kSerialTransport, encoded.data(), encoded.size(),
                    &encodedSize) != btp::Error::Ok) {
        return {};
    }
    std::vector<std::uint8_t> cobs(btp::kSerialMaxCobsBlockSize);
    std::size_t cobsSize = 0;
    if (btp::cobs_encode(encoded.data(), encodedSize, cobs.data(), cobs.size(), &cobsSize) !=
        btp::CobsError::Ok) {
        return {};
    }
    QByteArray packet(1, char(0));
    packet.append(reinterpret_cast<const char*>(cobs.data()), int(cobsSize));
    packet.append(char(0));
    return packet;
}

bool decodeWritten(const QByteArray& written, btp::DecodedFrame* out,
                   std::vector<std::uint8_t>* storage) {
    if (written.size() < 3 || written.front() != char(0) || written.back() != char(0)) {
        return false;
    }
    const QByteArray block = written.mid(1, written.size() - 2);
    storage->assign(btp::kSerialMaxFrameSize, 0);
    std::size_t decodedSize = 0;
    if (btp::cobs_decode(reinterpret_cast<const std::uint8_t*>(block.constData()),
                         std::size_t(block.size()), storage->data(), storage->size(),
                         &decodedSize) != btp::CobsError::Ok) {
        return false;
    }
    storage->resize(decodedSize);
    return btp::decode(storage->data(), storage->size(), btp::kSerialTransport, out) ==
           btp::Error::Ok;
}

btp::Frame frameOf(btp::MessageType type, quint16 objectId, quint32 sequence,
                   const QByteArray& payload) {
    btp::Header header{};
    header.type = type;
    header.source_id = kRobot;
    header.boot_id = kRobotBoot;
    header.sequence = sequence;
    header.object_id = objectId;
    header.fragment_count = 1;
    return btp::Frame{header,
                      {reinterpret_cast<const std::uint8_t*>(payload.constData()),
                       std::size_t(payload.size())}};
}

// The real HELLO/HELLO_RESULT exchange, as test_sessionclose.cpp runs it.
void establishSession(BtpBackend& backend, QSignalSpy& written) {
    const int beforeHello = written.count();
    backend.onTransportConnectionChanged(true);
    QVERIFY(written.count() > beforeHello);

    btp::DecodedFrame hello{};
    std::vector<std::uint8_t> helloStorage;
    QVERIFY(decodeWritten(written.at(beforeHello).at(0).toByteArray(), &hello, &helloStorage));
    QCOMPARE(hello.header.object_id, kControlHello);

    QByteArray resultPayload;
    appendLe32(resultPayload, hello.header.source_id);
    appendLe32(resultPayload, hello.header.boot_id);
    appendLe32(resultPayload, hello.header.sequence);
    resultPayload.append(char(0));  // SUCCESS
    resultPayload.append(char(btp::kMinimumProtocolVersion));
    resultPayload.append(2, char(0));
    appendLe32(resultPayload, 2048);
    resultPayload.append(char(1));
    resultPayload.append(char(0));
    resultPayload.append(char(8));
    resultPayload.append(char(0));
    appendLe32(resultPayload, 16);
    appendLe32(resultPayload, 30000);
    resultPayload.append(16, char(0x5A));
    appendLe32(resultPayload, 1);
    backend.feedBytes(
        buildSerialPacket(frameOf(btp::MessageType::Control, kControlHelloResult, 1, resultPayload)));
}

// Every COMMAND_REQUEST written since `from`, decoded, with its frame header.
struct SentCommand {
    btp::Header header;
    QByteArray line;
};

QList<SentCommand> sentCommands(const QSignalSpy& written, int from) {
    QList<SentCommand> out;
    for (int i = from; i < written.count(); ++i) {
        btp::DecodedFrame frame{};
        std::vector<std::uint8_t> storage;
        if (!decodeWritten(written.at(i).at(0).toByteArray(), &frame, &storage) ||
            frame.header.type != btp::MessageType::Command ||
            frame.header.object_id != btp::object_id::kCommandRequest) {
            continue;
        }
        btp::CommandRequest request{};
        if (btp::decode_command_request(frame.payload.data, frame.payload.size, &request) !=
            btp::MessageError::Ok) {
            continue;
        }
        out.append({frame.header, QByteArray(reinterpret_cast<const char*>(request.parameters.data),
                                             int(request.parameters.size))});
    }
    return out;
}

QByteArray commandResultPacket(const btp::Header& request, quint32 sequence, quint8 status,
                               const QByteArray& message) {
    btp::CommandResult result{};
    result.request = {request.source_id, request.boot_id, request.sequence};
    result.action_id = 1;
    result.action_version = 1;
    result.status = status;
    result.error_code = status == 0 ? 0 : 1;
    result.message = {reinterpret_cast<const std::uint8_t*>(message.constData()),
                      std::size_t(message.size())};
    std::vector<std::uint8_t> payload(1024);
    std::size_t size = 0;
    if (btp::encode_command_result(result, payload.data(), payload.size(), &size) !=
        btp::MessageError::Ok) {
        return {};
    }
    const QByteArray bytes(reinterpret_cast<const char*>(payload.data()), int(size));
    return buildSerialPacket(
        frameOf(btp::MessageType::Command, btp::object_id::kCommandResult, sequence, bytes));
}

// "severity|message" per logReceived emission.
QStringList logLines(const QSignalSpy& log) {
    QStringList out;
    for (const QList<QVariant>& args : log) {
        out.append(QStringLiteral("%1|%2").arg(args.at(4).toUInt()).arg(args.at(5).toString()));
    }
    return out;
}

}  // namespace

class TestSerialRobotCommand : public QObject {
    Q_OBJECT

private slots:
    void aFullShellLineGoesOutAsACommand();
    void theResultLandsInTheLogNotTheTerminal();
    void aSecondCommandGoesOutWhileTheFirstIsPending();
};

// The regression itself: a real shell line (here the longest the robots take)
// is encoded, unsealed, addressed to the robot that answered HELLO.
void TestSerialRobotCommand::aFullShellLineGoesOutAsACommand() {
    BtpBackend backend;
    backend.setSerialRobotPeer(true);
    QVERIFY(backend.isSerialRobotPeer());
    QSignalSpy written(&backend, &Backend::bytesToWrite);
    establishSession(backend, written);

    QSignalSpy status(&backend, &Backend::statusMessage);

    int before = written.count();
    backend.sendCommand(QByteArrayLiteral("furuta -status"));
    const QList<SentCommand> sent = sentCommands(written, before);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(sent.at(0).line, QByteArrayLiteral("furuta -status"));
    QVERIFY((sent.at(0).header.flags & btp::kFlagEncrypted) == 0U);  // cleartext port
    QCOMPARE(sent.at(0).header.sequence > 0U, true);

    // The longest line the robots take (532-byte request) is encoded too; on
    // this link it goes out as several fragments, which the robot reassembles.
    status.clear();
    before = written.count();
    backend.sendCommand(QByteArray(512, 'x'));
    QVERIFY(written.count() - before >= 1);
    QVERIFY2(status.isEmpty(), qPrintable(status.isEmpty() ? QString()
                                                           : status.last().at(0).toString()));
}

// The result goes to the log panel, one line per entry, colours stripped --
// never into the terminal stream, where it would garble a line being typed.
void TestSerialRobotCommand::theResultLandsInTheLogNotTheTerminal() {
    BtpBackend backend;
    backend.setSerialRobotPeer(true);
    QSignalSpy written(&backend, &Backend::bytesToWrite);
    establishSession(backend, written);
    QSignalSpy terminal(&backend, &Backend::terminalDataReceived);
    QSignalSpy log(&backend, &Backend::logReceived);
    const QString command = QString::number(quint8(LogSeverity::Command));
    const QString error = QString::number(quint8(LogSeverity::Error));

    const int before = written.count();
    backend.sendCommand(QByteArrayLiteral("furuta -home"));
    const QList<SentCommand> sent = sentCommands(written, before);
    QCOMPARE(sent.size(), 1);
    QCOMPARE(logLines(log), QStringList{command + "|> furuta -home"});
    QCOMPARE(log.at(0).at(1).toUInt(), kRobot);

    // The robot's output, with a colour code cut short where it truncated it.
    backend.feedBytes(commandResultPacket(
        sent.at(0).header, 5, 0,
        QByteArrayLiteral("pronto: meio curso\r\n! \x1B[32mok\x1B[0m\r\n\x1B[3")));
    QCOMPARE(logLines(log), (QStringList{command + "|> furuta -home",
                                         command + "|pronto: meio curso", command + "|! ok"}));

    // A failed one says so, after whatever the shell printed.
    const int beforeFail = written.count();
    backend.sendCommand(QByteArrayLiteral("furuta -start"));
    const QList<SentCommand> failed = sentCommands(written, beforeFail);
    QCOMPARE(failed.size(), 1);
    log.clear();
    backend.feedBytes(commandResultPacket(failed.at(0).header, 6, 3,
                                          QByteArrayLiteral("erro: faça o homing antes")));
    const QStringList lines = logLines(log);
    QCOMPARE(lines.size(), 2);
    QVERIFY2(lines.at(0).startsWith(error + "|erro: fa"), qPrintable(lines.at(0)));
    QVERIFY2(lines.at(1).startsWith(error + "|furuta -start failed"), qPrintable(lines.at(1)));

    QCOMPARE(terminal.count(), 0);
}

// A stop pressed while a homing is still running must not be dropped here.
void TestSerialRobotCommand::aSecondCommandGoesOutWhileTheFirstIsPending() {
    BtpBackend backend;
    backend.setSerialRobotPeer(true);
    QSignalSpy written(&backend, &Backend::bytesToWrite);
    establishSession(backend, written);

    const int before = written.count();
    backend.sendCommand(QByteArrayLiteral("furuta -home"));
    backend.sendCommand(QByteArrayLiteral("furuta -stop"));
    const QList<SentCommand> sent = sentCommands(written, before);
    QCOMPARE(sent.size(), 2);
    QCOMPARE(sent.at(0).line, QByteArrayLiteral("furuta -home"));
    QCOMPARE(sent.at(1).line, QByteArrayLiteral("furuta -stop"));
}

QTEST_MAIN(TestSerialRobotCommand)
#include "test_serialrobotcommand.moc"
