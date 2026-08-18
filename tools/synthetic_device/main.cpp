// CLI harness that opens a real serial port and plays the *device* side of
// BTP v1 on it (see docs/SYNTHETIC_DEVICE.md) -- lets TraceView's Devices tab
// connect to something that behaves like a real Bally_dongle without any
// hardware. All protocol logic lives in SyntheticDeviceSession; this file is
// only QSerialPort/QCommandLineParser glue and stdout logging, the same split
// SerialManager/Backend keep in the main app.

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QSerialPort>
#include <QTextStream>

#include <cstdio>
#include <memory>

#include "solarpaneldevice.h"
#include "syntheticdevicesession.h"
#include "weatherstationdevice.h"

namespace {

void printLine(const QString& text) {
    QTextStream out(stdout);
    out << '[' << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << "] " << text << '\n';
    out.flush();
}

bool parseSourceId(const QString& text, quint32* out) {
    bool ok = false;
    const quint32 value = text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                               ? text.mid(2).toUInt(&ok, 16)
                               : text.toUInt(&ok, 10);
    if (!ok || value == 0) {
        return false;
    }
    *out = value;
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("synthetic_device"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Emulates the device side of BTP v1 over a real serial port, so TraceView's "
                        "telemetry pipeline can be tested without hardware. See docs/SYNTHETIC_DEVICE.md."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("Serial port to listen on (required)."),
                                   QStringLiteral("name"));
    QCommandLineOption baudOption(QStringLiteral("baud"),
                                   QStringLiteral("Baud rate (default: 921600, matches Bally_dongle)."),
                                   QStringLiteral("rate"), QStringLiteral("921600"));
    QCommandLineOption profileOption(
        QStringLiteral("profile"),
        QStringLiteral("Which simulated device to run: solar_panel (default) or weather_station. Run "
                        "one instance per profile/port to simulate several distinct devices at once."),
        QStringLiteral("name"), QStringLiteral("solar_panel"));
    QCommandLineOption sourceIdOption(
        QStringLiteral("source-id"),
        QStringLiteral("Device source_id, hex (0x...) or decimal, non-zero (default: per-profile)."),
        QStringLiteral("id"));
    parser.addOption(portOption);
    parser.addOption(baudOption);
    parser.addOption(profileOption);
    parser.addOption(sourceIdOption);
    parser.process(app);

    if (!parser.isSet(portOption)) {
        fprintf(stderr, "Error: --port is required.\n\n%s", qPrintable(parser.helpText()));
        return 1;
    }
    const QString portName = parser.value(portOption);

    bool baudOk = false;
    const qint32 baudRate = parser.value(baudOption).toInt(&baudOk);
    if (!baudOk || baudRate <= 0) {
        fprintf(stderr, "Error: --baud must be a positive integer.\n");
        return 1;
    }

    const QString profile = parser.value(profileOption);
    if (profile != QStringLiteral("solar_panel") && profile != QStringLiteral("weather_station")) {
        fprintf(stderr, "Error: --profile must be solar_panel or weather_station.\n");
        return 1;
    }

    quint32 sourceId = (profile == QStringLiteral("weather_station")) ? WeatherStationDevice::kDefaultSourceId
                                                                       : SolarPanelDevice::kDefaultSourceId;
    if (parser.isSet(sourceIdOption) && !parseSourceId(parser.value(sourceIdOption), &sourceId)) {
        fprintf(stderr, "Error: --source-id must be a non-zero hex (0x...) or decimal value.\n");
        return 1;
    }

    QSerialPort port;
    port.setPortName(portName);
    port.setBaudRate(baudRate);
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    port.setFlowControl(QSerialPort::NoFlowControl);

    // Foreground tool the user launches when ready -- fail fast rather than
    // ambient-retrying like DeviceConnection does for the real app.
    if (!port.open(QIODevice::ReadWrite)) {
        fprintf(stderr, "Error: failed to open %s: %s\n", qPrintable(portName), qPrintable(port.errorString()));
        return 1;
    }

    std::unique_ptr<SyntheticDeviceSession> session;
    if (profile == QStringLiteral("weather_station")) {
        session = std::make_unique<WeatherStationDevice>(sourceId);
    } else {
        session = std::make_unique<SolarPanelDevice>(sourceId);
    }

    QObject::connect(session.get(), &SyntheticDeviceSession::bytesToWrite, &port,
                      [&port](const QByteArray& data) { port.write(data); });
    QObject::connect(&port, &QSerialPort::readyRead, session.get(),
                      [&port, &session] { session->feedBytes(port.readAll()); });
    QObject::connect(session.get(), &SyntheticDeviceSession::logMessage, &app,
                      [](const QString& text) { printLine(text); });
    QObject::connect(&port, &QSerialPort::errorOccurred, &app, [&portName](QSerialPort::SerialPortError error) {
        if (error == QSerialPort::NoError) {
            return;
        }
        printLine(QStringLiteral("port error on %1 (code %2)").arg(portName).arg(int(error)));
    });

    printLine(QStringLiteral("listening on %1 @ %2 baud, profile=%3, source_id=0x%4")
                  .arg(portName)
                  .arg(baudRate)
                  .arg(profile)
                  .arg(sourceId, 8, 16, QChar('0')));
    printLine(QStringLiteral("console: waiting for BTP/1 ENTER ..."));

    return app.exec();
}
